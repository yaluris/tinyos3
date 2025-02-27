
#ifndef __KERNEL_PIPE_H
#define __KERNEL_PIPE_H

#include "kernel_streams.h"

#define PIPE_BUFFER_SIZE 8000 /* 8 kBytes buffer size */

typedef struct pipe_control_block {

	FCB *reader, *writer;

	CondVar has_space;    /* For blocking writer if no space is available */
	CondVar has_data;     /* For blocking reader until data are available */

	int w_position, r_position;  /* write, read position in buffer */
	char BUFFER[PIPE_BUFFER_SIZE];   /* bounded (cyclic) byte buffer */

} pipe_cb;

void* pipe_open(unsigned int minor);
int pipe_illegal_read(void* this, char *buf, unsigned int size);
int pipe_illegal_write(void* this, const char *buf, unsigned int size);
int pipe_read(void* this, char *buf, unsigned int size);
int pipe_write(void* this, const char* buf, unsigned int size);
int pipe_reader_close(void* this);
int pipe_writer_close(void* this);
int isEmpty(pipe_cb* pipe_con_block);
int isFull(pipe_cb* pipe_con_block);

#endif

