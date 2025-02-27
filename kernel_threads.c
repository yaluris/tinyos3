
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"


void start_new_thread()
{
  assert(cur_thread()->ptcb != NULL);      /* Just to be sure that "ptcb != null", before accessing it  */
  int exitval;                             /* Return value of the particular task                       */
  Task call = cur_thread()->ptcb->task;    /* Set call as the task that is executing now                */
  int argl = cur_thread()->ptcb->argl;     /* Set argl as the argl from PTCB                            */
  void* args = cur_thread()->ptcb->args;   /* Set args as the args from PTCB                            */
  exitval = call(argl,args);               /* Load the result of "call(argl,args)", in our exit-integer */ 
  sys_ThreadExit(exitval);                 /* Terminate the current thread                              */
}

/** 
  @brief Create a new thread in the current process.
*/

Tid_t sys_CreateThread(Task task, int argl, void* args)
{
/**********************************************************************************************************************************************************************/
  assert(task != NULL);                                           /*            Just to be sure that "task != null", before accessing it                              */
  PTCB* NewPTCB= (PTCB*)xmalloc(sizeof(PTCB));                    /*                           Allocates a PTCB-space                                                 */
  TCB* NewTCB = spawn_thread(CURPROC,start_new_thread);           /* This call creates a new thread, initializing and returning its TCB.The thread will belong to PCB */
  NewPTCB->task = task;                                           /*  \                                                                                               */
  NewPTCB->argl = argl;                                           /*   \                                                                                              */
  NewPTCB->args = args;                                           /*    \                                                                                             */                         
  NewPTCB->exited = 0;                                            /*     Initialization of the variables a PTCB has                                                   */
  NewPTCB->detached = 0;                                          /*\   /                                                                                             */
  NewPTCB->exit_cv = COND_INIT;                                   /*\  /                                                                                              */
  NewPTCB->refCount = 0;                                          /*\ /                                                                                               */
  rlnode_init(&NewPTCB->ptcb_list_node,NewPTCB);                  /* /                                                                                                */
  CURPROC->thread_count += 1;                                     /* Increase(in PCB) everytime a new thread is created                                               */
  NewTCB->ptcb = NewPTCB;                                         /* Connect the TCB's PTCB, with our NewPTCB                                                         */
  NewPTCB->tcb = NewTCB;                                          /* Connect the PTCB's TCB, with our NewTCB                                                          */
  rlist_push_back(&CURPROC->ptcb_list,&NewPTCB->ptcb_list_node);  /* Adds the new PTCB in the (end of) the list                                                       */
  wakeup(NewTCB);                                                 /* Prepares the informed NewTCB and put it into the proper queue(with select...)                    */
/**********************************************************************************************************************************************************************/
  return (Tid_t)NewPTCB;
}

/**
  @brief Return the Tid of the current thread.
*/

Tid_t sys_ThreadSelf()
{
/*****************************************************************************************************************************************************************/
  return (Tid_t) cur_thread()->ptcb;         /*          Returns the Tid of the current thread.           */
/******************************************************************************************************************************************************/
}

/**
  @brief Join the given thread.
*/

int sys_ThreadJoin(Tid_t tid, int* exitval)
{
/*************************************************************************************************************************************************/
  PTCB* PTCB_to_join = (PTCB*)tid;

  if(rlist_find(&CURPROC->ptcb_list, PTCB_to_join, NULL) == NULL)                    /* Checks if the given thread belongs to this particular process */
    return -1;
  
  if(PTCB_to_join->detached || cur_thread()->ptcb == PTCB_to_join || tid == NOTHREAD) /* Checks if PTCB_to_join is detached, or tries to join itself, or the thread has no valid tid, returns -1*/
   return -1;
  
  PTCB_to_join->refCount += 1;                                                        /* We can join this thread(valid tid, not detached, neither the calling thread waits itself), and increase the refCount by one */

  while(!PTCB_to_join->exited && !PTCB_to_join->detached) /* While the PTCB_to_join is not exited and not detached*/
    kernel_wait(&PTCB_to_join->exit_cv, SCHED_USER);                                                         /* */

  PTCB_to_join->refCount--;   /* Decrease refCount */

  if(PTCB_to_join->detached)/* Checks if PTCB_to_join is detached and returns -1 */
    return -1;
    
  if(exitval != NULL)      /* Checks if exitval is NULL */
    *exitval = PTCB_to_join->exitval;           /* Returning exitval of ptcb_to_join*/

  if(PTCB_to_join->refCount == 0)          /* No threads waiting for PTCB_to_join => ready to be set free */
  {
      rlist_remove(&PTCB_to_join->ptcb_list_node);    /* Remove the PTCB_to_join from the ptcb_list_node(line 44)*/ 
      free(PTCB_to_join);                             /* Free the memory from PTCB_to_join*/
  }      
  return 0;
/**********************************************************************************************************************************************************************/
}

/**
  @brief Detach the given thread.
*/

int sys_ThreadDetach(Tid_t tid)
{ 
/***************************************************************************************************************************************************/
  PTCB* PTCB_to_be_Detached = (PTCB*) tid;

  if(rlist_find(&CURPROC->ptcb_list, PTCB_to_be_Detached, NULL) == NULL || PTCB_to_be_Detached->exited) /* If this thread has a valid tid(belongs to the CURPROC) or it is exited */
    return -1;                                              /* Failure */

  PTCB_to_be_Detached->detached = 1; 
  kernel_broadcast(&PTCB_to_be_Detached->exit_cv);          /*Broadcast exit_cv of PTCB_to_be_Detached */
  return 0;                                                 /* Success */
   
/********************************************************************************************************************************************************/
}

/**
  @brief Terminate the current thread.
*/

void sys_ThreadExit(int exitval)
{
/****************************************************************************************************************************************************/
  PCB* curproc = CURPROC;

  TCB* CurThread = cur_thread();
  PTCB* CurPTCB = CurThread->ptcb;
  CurPTCB->tcb = NULL;
  CurPTCB->exitval = exitval;
  CurPTCB->exited = 1;
 
  curproc->thread_count--; /* Decrease the threadCount by 1 */

  if(CurPTCB->refCount != 0)  /* Wake up waiting threads, if they exist */
    kernel_broadcast(&CurPTCB->exit_cv);
  
  if(curproc->thread_count == 0)  /* If this is last thread */
  {
    if(get_pid(curproc) != 1)
    {  

/******************************************************************************************************************************************************/

      /* Reparent any children of the exiting process to the initial task */
      PCB* initpcb = get_pcb(1);
      while(!is_rlist_empty(& curproc->children_list)) 
      {
        rlnode* child = rlist_pop_front(& curproc->children_list);
        child->pcb->parent = initpcb;
        rlist_push_front(& initpcb->children_list, child);
      }

      /* Add exited children to the initial task's exited list 
       and signal the initial task */
      if(!is_rlist_empty(& curproc->exited_list)) {
        rlist_append(& initpcb->exited_list, &curproc->exited_list);
        kernel_broadcast(& initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
      kernel_broadcast(& curproc->parent->child_exit);

    }

    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));


    /* 
    Do all the other cleanup we want here, close files etc. 
    */

    /* Release the args data */
    if(curproc->args) {
      free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curproc->FIDT[i] != NULL) {
        FCB_decref(curproc->FIDT[i]);
        curproc->FIDT[i] = NULL;
      }
    } 

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
  }

  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}

