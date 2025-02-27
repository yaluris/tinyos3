
#include "kernel_pipe.h"
#include "kernel_cc.h"


static file_ops pipe_reader_functions = {
	.Open = pipe_open,
	.Read = pipe_read,
	.Write = pipe_illegal_write,
	.Close = pipe_reader_close
};

static file_ops pipe_writer_functions = {
	.Open = pipe_open,
	.Read = pipe_illegal_read,
	.Write = pipe_write,
	.Close = pipe_writer_close
};

int sys_Pipe(pipe_t* pipe)
{
	//construct and return a pipe
	//! sth diagrafh prepei na kanoume free ektos apo to pipe struct kai to char BUFFER[] opws 
	//stis shm. front. pointers sel. 17 / 36 

	Fid_t fid[2];		//see console.c tinyos_pseudo_console()
	FCB* fcb[2];
	/* Acquire a number of FCBs and corresponding fids */
	/* Since FCB_reserve allocates fids in increasing order,
	   we expect pair[0]==0 and pair[1]==1 */
	if(FCB_reserve(2, fid, fcb) == 0)
		return -1; /* the available file ids for the process are exhausted */

	pipe_cb* newPipe_cb = (pipe_cb*)xmalloc(sizeof(pipe_cb));          /* Allocates a pipe_cb-space */

/* Connect the two FCBs with the new pipe control block and its functions */
	fcb[0]->streamobj = newPipe_cb;
	fcb[1]->streamobj = newPipe_cb;

	fcb[0]->streamfunc = &pipe_reader_functions;
	fcb[1]->streamfunc = &pipe_writer_functions;

	newPipe_cb->reader = fcb[0];
	newPipe_cb->writer = fcb[1];

	newPipe_cb->has_space = COND_INIT; 		/* Initialization of the new pipe control block */
	newPipe_cb->has_data = COND_INIT; 
	
	newPipe_cb->w_position = 0;
	newPipe_cb->r_position = 0;				/* Initialy writer and reader ends are in BUFFER[0] */

/* Connect the two ends of pipe with the proper FIDs , pipe_t (tinyos.h)*/
	pipe->read = fid[0];
	pipe->write = fid[1];

	return 0; /* returns 0 on success */
}

void* pipe_open(uint minor)
{	
	return NULL; /* Open is "implemented" by the Pipe function */         
}

int pipe_illegal_read(void* this, char *buf, unsigned int size)
{	
	return -1;
}

int pipe_illegal_write(void* this, const char *buf, unsigned int size)
{
	return -1;
}

int pipe_read(void* this, char *buf, unsigned int size)
{
	/* Similar function to serial_read (kernel_dev.c) */
	/*Read up to 'size' bytes from stream 'this' into buffer 'buf'.*/
	pipe_cb* pipe_con_block = (pipe_cb*)this;
	/* if pipe_cb is invalid or reader end is closed 
	 *if writer is closed we can still read from the pipe*/
	if(pipe_con_block == NULL || pipe_con_block->reader == NULL)
		return -1;

	/* While pipe is empty and we can't write in it */
	if(isEmpty(pipe_con_block) && pipe_con_block->writer == NULL)
 		return 0;  

	/* While pipe is empty, we must wait until something is written */
	while(isEmpty(pipe_con_block)){
		kernel_broadcast(&pipe_con_block -> has_space);	/*buffer is empty, wake up writers */
		kernel_wait(&pipe_con_block -> has_data, SCHED_PIPE);
	}

	int ctr = 0;			/* Counter for the bytes to return */
	int i = 0;				/*Position of buf*/
	/* While fewer than size bytes have been read and the pipe is not empty */
	while(ctr < size && !isEmpty(pipe_con_block)){
		buf[i] = pipe_con_block->BUFFER[pipe_con_block->r_position];
		ctr++;														/*Increase returned bytes*/
		i++;														/*Increase position of buf*/
		pipe_con_block->r_position = (pipe_con_block->r_position + 1) % PIPE_BUFFER_SIZE;	/*Increase r_position of pipe*/
	}
	
	kernel_broadcast(&pipe_con_block -> has_space);			/*wake up writers */

	return ctr;
}

int pipe_write(void* this, const char* buf, unsigned int size)
{
	/* Write up to 'size' bytes from 'buf' to the stream 'this'. */
	pipe_cb* pipe_con_block = (pipe_cb*)this;

	/* if pipe_cb is invalid or writer end is closed 
	 *or reader end is closed(thus we will not be able to read what we wrote)*/
	if(pipe_con_block == NULL || pipe_con_block->writer == NULL|| pipe_con_block->reader == NULL)
		return -1;

	while(isFull(pipe_con_block)){				
		kernel_broadcast(&pipe_con_block -> has_data);	/*buffer is full, wake up readers */
		kernel_wait(&pipe_con_block -> has_space, SCHED_PIPE);
	}

	int ctr = 0;			/* Counter for the bytes to return */
	int i = 0;				/*Position of buf*/
	/*While the BUFFER is not full and less than size bytes have been written,
	 *write in the pipe */
	while(ctr < size && !isFull(pipe_con_block)){
		pipe_con_block->BUFFER[pipe_con_block->w_position] = buf[i];
		ctr++;														/*Increase returned bytes*/
		i++;														/*Increase position of buf*/
		pipe_con_block->w_position = (pipe_con_block->w_position + 1) % PIPE_BUFFER_SIZE;	/*Increase w_position of pipe*/
	}

	kernel_broadcast(&pipe_con_block -> has_data);			/*wake up readers */

	return ctr;
}

int pipe_reader_close(void* this)
{
	/*  Here we will close the reader end of the pipe */
	pipe_cb* pipe_con_block = (pipe_cb*)this;
	
	if(pipe_con_block == NULL)
		return -1;
	
	pipe_con_block->reader = NULL;		/* Close reader end */
	kernel_broadcast(& pipe_con_block->has_space);	/*  Wake up the writers */

	if(pipe_con_block->writer == NULL){		/* If both ends are now closed free the pipe*/
		//pipe_con_block = NULL;
		free(pipe_con_block);
	}

	return 0;
}

int pipe_writer_close(void* this)
{
	/*  Here we will close the writer end of the pipe */
	pipe_cb* pipe_con_block = (pipe_cb*)this;
	
	if(pipe_con_block == NULL)
		return -1;
	
	pipe_con_block->writer = NULL;		/* Close writer end */
	kernel_broadcast(& pipe_con_block->has_data);	/*  Wake up the readers */

	if(pipe_con_block->reader == NULL){		/* If both ends are now closed free the pipe*/
		//pipe_con_block = NULL;
		free(pipe_con_block);
	}

	return 0;
}

/*
function that checks if pipe buffer is empty
returns 1 for empty buffer, 0 for non empty buffer
*/
int isEmpty(pipe_cb* pipe_con_block)
{
	assert(pipe_con_block != NULL);

	if(pipe_con_block->r_position == pipe_con_block->w_position)
		return 1;

	return 0;
}

/*
function that checks if pipe buffer is full
returns 1 for full buffer, 0 for non full buffer
*/
int isFull(pipe_cb* pipe_con_block)
{
	assert(pipe_con_block != NULL);

	if(pipe_con_block->r_position == ((pipe_con_block->w_position +1) % PIPE_BUFFER_SIZE))
		return 1;
	
	return 0;
}

