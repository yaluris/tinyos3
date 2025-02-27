
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"


/* 
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid

 */

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->argl = 0;
  pcb->args = NULL;

  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);
/***********************************************************************************************************************************************/
  rlnode_init(&pcb->ptcb_list, NULL);                 /*Initialization of PTCB_LIST in PCB                                                    */
  pcb->thread_count = 0;                               /*Initialization of thread_count in PCB                                                 */
/***********************************************************************************************************************************************/
  pcb->child_exit = COND_INIT;
}


static PCB* pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) 
  {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) 
  {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB()
{
  PCB* pcb = NULL;

  if(pcb_freelist != NULL) 
  {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}


/*
 *
 * Process creation
 *
 */

/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;

  Task call =  CURPROC->main_task;
  int argl = CURPROC->argl;
  void* args = CURPROC->args;

  exitval = call(argl,args);
  Exit(exitval);
}


/*
  System call to create a new process.
 */
Pid_t sys_Exec(Task call, int argl, void* args)
{
  
  PCB *curproc, *newproc;

  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1)
  {
    /* Processes with pid<=1 (the scheduler and the init process) 
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) 
    {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }


  /* Set the main thread's function */
  newproc->main_task = call;

  /* Copy the arguments to new storage, owned by the new process */
  newproc->argl = argl;
  if(args!=NULL) {
    newproc->args = malloc(argl);
    memcpy(newproc->args, args, argl);
  }
  else
    newproc->args=NULL;

  /* 
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */
  if(call != NULL)
  {
    newproc->main_thread = spawn_thread(newproc, start_main_thread);
/**********************************************************************************************************************/
    PTCB* Ptcb = (PTCB*)xmalloc(sizeof(PTCB));                  /*               Allocates a PTCB-space               */
    Ptcb->task = call;                                          /* \                                                  */
    Ptcb->argl = argl;                                          /*  \                                                 */
    Ptcb->args = args;                                          /*   \                                                */
    Ptcb->tcb = newproc->main_thread;                           /*    \                                               */
    Ptcb->exited = 0;                                           /*     Initialization of the variables a PTCB has     */
    Ptcb->detached = 0;                                         /*    /                                               */
    Ptcb->exit_cv = COND_INIT;                                  /*   /                                                */
    Ptcb->refCount = 0;                                         /*  /                                                 */
    rlnode_init(&Ptcb->ptcb_list_node,Ptcb);                    /* /                                                  */
    newproc->thread_count += 1;                                 /* Increase(in TCB) everytime a new thread is created */
    newproc->main_thread->ptcb = Ptcb;                          /* Connect the TCB's PTCB, with our NewPTCB           */
    Ptcb->tcb = newproc->main_thread;                           /* Connect the PTCB's TCB, with our NewTCB            */
    rlist_push_back(&newproc->ptcb_list,&Ptcb->ptcb_list_node); /* Adds the new PTCB in the (end of) the list         */
/**********************************************************************************************************************/
    wakeup(newproc->main_thread);
  }


finish:
  return get_pid(newproc);
}


/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}


Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) 
  {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    kernel_wait(& parent->child_exit, SCHED_USER);
  
  cleanup_zombie(child, status);
  
finish:
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  int no_children, has_exited;
  while(1) 
  {
    no_children = is_rlist_empty(& parent->children_list);
    if( no_children ) break;

    has_exited = ! is_rlist_empty(& parent->exited_list);
    if( has_exited ) break;

    kernel_wait(& parent->child_exit, SCHED_USER);    
  }

  if(no_children)
    return NOPROC;

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

  return cpid;
}


Pid_t sys_WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void sys_Exit(int exitval)
{

  PCB *curproc = CURPROC;  /* cache for efficiency */

  /* First, store the exit status */
  curproc->exitval = exitval;

  /* 
    Here, we must check that we are not the init task. 
    If we are, we must wait until all child processes exit. 
   */
  if(get_pid(curproc)==1) 
    while(sys_WaitChild(NOPROC,NULL)!=NOPROC);
  
/**********************************************************************************************************************/
  sys_ThreadExit(exitval);
/**********************************************************************************************************************/
}

/**********************************************************************************************************************/
static file_ops procinfo_functions = {
  .Open = procinfo_open,
  .Read = procinfo_read,
  .Write = procinfo_write,
  .Close = procinfo_close
};

Fid_t sys_OpenInfo()
{
  /* See Fid_t OpenInfo() in tinyos.h */
  Fid_t fid[1];   //see console.c tinyos_pseudo_console()
  FCB* fcb[1];

  //Acquire a number of FCBs and corresponding fids.
  if(FCB_reserve(1, fid, fcb) == 0)
    return NOFILE;

  procinfo_cb* newProcinfo_cb = (procinfo_cb*)xmalloc(sizeof(procinfo_cb));          /* Allocates a procinfo_cb-space */

  fcb[0]->streamobj = newProcinfo_cb;
  fcb[0]->streamfunc = &procinfo_functions;

  newProcinfo_cb->pcb_cursor = 0;

  return fid[0];
}

void* procinfo_open(uint minor)
{ 
  return NULL; /* Open is "implemented" by the OpenInfo function */      
}

int procinfo_read(void* this, char *buf, unsigned int size)
{
  /*Read up to 'size' bytes from stream 'this' into buffer 'buf'.*/
  procinfo_cb* process_info_cb = (procinfo_cb*)this;

  if(process_info_cb == NULL)
    return -1;

  /* Search within the PT table to find an active process i.e. alive or zombie*/
  while(process_info_cb->pcb_cursor <= MAX_PROC-1)
  {
    if(PT[process_info_cb->pcb_cursor].pstate == ALIVE || PT[process_info_cb->pcb_cursor].pstate == ZOMBIE)
    {
      /* We found an active process*/
      /* Process id*/
      process_info_cb->process_info.pid = get_pid(&PT[process_info_cb->pcb_cursor]);
      /* Parent process id*/
      process_info_cb->process_info.ppid = get_pid(PT[process_info_cb->pcb_cursor].parent);

      if(PT[process_info_cb->pcb_cursor].pstate == ALIVE)
        process_info_cb->process_info.alive = 1; //alive
      else
        process_info_cb->process_info.alive = 0; //zombie

      process_info_cb->process_info.thread_count = PT[process_info_cb->pcb_cursor].thread_count;
      process_info_cb->process_info.main_task = PT[process_info_cb->pcb_cursor].main_task;
      process_info_cb->process_info.argl = PT[process_info_cb->pcb_cursor].argl;


      /* pi_cb->processInfo->args = The first 
      PROCINFO_MAX_ARGS_SIZE bytes of the argument of the main task.  */
      if(PT[process_info_cb->pcb_cursor].argl <= PROCINFO_MAX_ARGS_SIZE)
        memcpy(process_info_cb->process_info.args, PT[process_info_cb->pcb_cursor].args, PT[process_info_cb->pcb_cursor].argl);
      else
        memcpy(process_info_cb->process_info.args, PT[process_info_cb->pcb_cursor].args, PROCINFO_MAX_ARGS_SIZE);

      /* Copy the stream into the buffer*/
      memcpy(buf, (char*)&process_info_cb->process_info, sizeof(procinfo));

      process_info_cb->pcb_cursor++; /* Increase position, to be ready for the next Read()*/

      return sizeof(procinfo);
    }
    else
      process_info_cb->pcb_cursor++;
  }
  return 0; //end of pt
}

int procinfo_write(void* this, const char *buf, unsigned int size)
{
  return -1;
}

int procinfo_close(void* this)
{
  procinfo_cb* process_info_cb = (procinfo_cb*)this;
  
  if(process_info_cb == NULL)
    return -1;

  free(process_info_cb);

  return 0;
}
/**********************************************************************************************************************/

