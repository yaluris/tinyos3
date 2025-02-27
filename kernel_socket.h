
#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H

#include "kernel_pipe.h"

typedef enum 
{
	SOCKET_LISTENER,
	SOCKET_UNBOUND,
	SOCKET_PEER

} socket_type;

typedef struct socket_control_block SCB; 

typedef struct listener_socket {
	
	rlnode queue;
	CondVar req_available;

} listener_socket;

typedef struct unbound_socket {

	rlnode unbound_socket;

} unbound_socket;

typedef struct peer_socket {

	SCB* peer;
	pipe_cb* write_pipe;
	pipe_cb* read_pipe;

} peer_socket;

typedef struct socket_control_block {

	uint refcount;
	FCB* fcb;
	socket_type type;
	port_t port;

	union {
		listener_socket listener_s;
		unbound_socket unbound_s;
		peer_socket peer_s;
	};

} SCB;

typedef struct connection_request {

	int admitted;
	SCB* peer;

	CondVar connected_cv;
	rlnode queue_node;

} con_req;

SCB* PORT_MAP[MAX_PORT+1];

void* socket_open(unsigned int minor);
int socket_read(void* this, char *buf, unsigned int size);
int socket_write(void* this, const char *buf, unsigned int size);
int socket_close(void* this);

#endif

