
#include "tinyos.h"
#include "util.h"
#include "kernel_streams.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"
#include "kernel_socket.h"


static file_ops socket_functions = {
  .Open = socket_open,
  .Read = socket_read,
  .Write = socket_write,
  .Close = socket_close
};

Fid_t sys_Socket(port_t port)
{
	if(port < 0 || port > MAX_PORT)
		return NOFILE;

  Fid_t fid[1];   //see console.c tinyos_pseudo_console()
  FCB* fcb[1];

  if(FCB_reserve(1, fid, fcb) == 0)
    return NOFILE;

  SCB* scb = (SCB*)xmalloc(sizeof(SCB));          /* Allocates a socket cb-space */

  fcb[0]->streamobj = scb;
  fcb[0]->streamfunc = &socket_functions;

  scb->refcount = 0;
	scb->fcb = fcb[0];

  scb->type = SOCKET_UNBOUND;
	scb->port = port;
	
  return fid[0];
}

void* socket_open(uint minor)
{ 
  return NULL; /* Open is "implemented" by the Socket function */      
}

int socket_read(void* this, char *buf, unsigned int size)
{
	SCB* scb = (SCB*)this;

	if(scb == NULL)
		return -1;

	if(scb->peer_s.read_pipe != NULL && scb->type == SOCKET_PEER){
		return pipe_read(scb->peer_s.read_pipe, buf, size); 
	}else
		return -1;
}

int socket_write(void* this, const char *buf, unsigned int size)
{
	SCB* scb = (SCB*)this;

	if(scb == NULL)
		return -1;

	if(scb->peer_s.write_pipe != NULL && scb->type == SOCKET_PEER){
		return pipe_write(scb->peer_s.write_pipe, buf, size); 
	}
	else
		return -1;
}

int socket_close(void* this)
{
	SCB* scb = (SCB*)this;

	if(scb == NULL)
		return -1;

	if(scb->type == SOCKET_PEER){
		pipe_reader_close(scb->peer_s.read_pipe);
		pipe_writer_close(scb->peer_s.write_pipe);
	}
	else if(scb->type == SOCKET_LISTENER){
		PORT_MAP[scb->port] = NULL;
		kernel_broadcast(&scb->listener_s.req_available);
	}
	scb->refcount--;
	if(scb->refcount == 0)
		free(scb);

	return 0;
}

int sys_Listen(Fid_t sock)      //sock == Socket to initialize as a listening socket
{
	PCB* curproc = CURPROC;                     
	assert(curproc != NULL);			//Checks if there is a current process

	if(sock > MAX_FILEID || curproc->FIDT[sock] == NULL) 
		return -1;         //Checking if we are over the number of Files(we can handle) || the socket of FIDT, of current process exists
	
	SCB* scb = curproc -> FIDT[sock]-> streamobj; 
	
	if(scb == NULL || scb->port == NOPORT || PORT_MAP[scb->port] != NULL || scb->type == SOCKET_PEER || scb->type == SOCKET_LISTENER )
		return -1; //Checks the socket control block, its port, and its availability in port[map], and its type(PEER || LISTENER)   

	PORT_MAP[scb->port] = scb;	    //Load the current scb to the port of port-map 
	scb->type = SOCKET_LISTENER;    //Make the type of curr scb to a listener 
	scb->listener_s.req_available = COND_INIT; 
	rlnode_init(&scb->listener_s.queue, NULL);

	return 0;
}

Fid_t sys_Accept(Fid_t lsock)
{
	return NOFILE;
}

int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}

int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	FCB* fcb = get_fcb(sock);
	SCB* scb = fcb->streamobj;

	if(scb == NULL || scb->type != SOCKET_PEER)
		return -1;

	switch(how)
	{
		case SHUTDOWN_READ:
		  pipe_reader_close(scb->peer_s.read_pipe);
			break;
		case SHUTDOWN_WRITE:
			pipe_writer_close(scb->peer_s.write_pipe);
			break;
		case SHUTDOWN_BOTH:
			pipe_reader_close(scb->peer_s.read_pipe);
			pipe_writer_close(scb->peer_s.write_pipe);
			break;
		default:
			return -1;
	}
	return 0;
}

