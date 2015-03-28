#define _GNU_SOURCE
#include <stdio.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sched.h>
#include <sys/types.h>
#include <unistd.h>
#include "uthread.h"

#define STACK_SIZE 16384
#define DEBUG 0

// Lock we use for all operations on our shared state
static sem_t lock;

// Number of running kernel threads (0 or 1)
static int kernel_threads;

// Number of threads waiting on I/O operations
static int waiting_threads;

// The ready queue
static struct list ready; 

// Initialize a list
void init_list(struct list *list) {
	list->head = NULL;
	list->tail = NULL;
	list->size = 0;
}


// Dequeue operation on a list. Returns an entire node or null if none exist.
list_node* deq(struct list *list) {
	list_node* node = list->head;
	
	if(list->size == 0) {
		return NULL;
	}
	else if(list->size == 1) {
		list->size--;
		list->head = list->tail = NULL;
		node->next = NULL;
		return node;
	}
	else {
		list->size--;
		list->head = list->head->next;
		node->next = NULL;
		return node;	
	}	
}

// Enqueue operation on a list.
void enq(struct list *list, list_node *node) {
	if(list->size == 0) {
		list->head = list->tail = node;
		node->next = NULL;
	}
	else {
		list->tail->next = node;
		node->next = NULL;
		list->tail = node;
	}
	list->size++;
	return;
}


// Creates a new ucontxt, its stack, and a list_node for it. Returns the node.
list_node* create_node_and_context() {
	ucontext_t *context = (ucontext_t *) malloc(sizeof(ucontext_t));
	getcontext(context);
	context->uc_stack.ss_sp = (void*) malloc(STACK_SIZE);
	context->uc_stack.ss_size = STACK_SIZE;
	context->uc_stack.ss_flags = 0;
	context->uc_link = NULL;
	
	list_node *new_node = (list_node *) malloc(sizeof(list_node));
	new_node->current = context;
	new_node->next = NULL;
	return new_node;
}

// Frees a node, the context within, and its stack. Not used anymore.
void deep_free_node(list_node *cur) {
	free(cur->current->uc_stack.ss_sp);
	free(cur->current);
	free(cur);
}

// Loops through a list and frees each node and all inner data.
void purge_deletionq(struct list *list) {
	list_node *cur = NULL;	
	while( (cur = deq(list)) != NULL ) {
		deep_free_node(cur);
	}
}

// Defines the code to be run inside a new kernel thread.
int new_kernel_thread(void *a) {
	// For now, we can just call uthread_exit, which will start executing whatevers on
	// the ready queue
	uthread_exit();
}

/*
This function has to be called before any other uthread library functions can be called. It initializes the uthread system. The library should maintain data structure of a ready queue, number of currently running kernel threads (should not exceed 1 in this project) and number of processes that are currently waiting for the I/O operation.

Assumption: This is only ever called once, from the main thread (which we will use for our kernel thread)
*/
void system_init() {
	sem_init(&lock, 0, 1);

	sem_wait(&lock);
	kernel_threads = 1;	
	waiting_threads = 0;

	init_list(&ready);
	sem_post(&lock);
}


/*
The calling thread requests to create a user-level thread that runs the function func. The context of function should be properly created and stored on the ready queue for execution. The function returns 0 on success and -1 otherwise
*/
int uthread_create(void (* func)()) {
	list_node *new_node = create_node_and_context();
	makecontext(new_node->current, func, 0); // Create a new context with a new stack which will start execution of our function

	sem_wait(&lock); // Ensure mutual exclusion when operating on shared data
	enq(&ready, new_node); // Add it to our ready queue
	sem_post(&lock);	
	return 0;
}

/*
The calling thread calls this function before it requests for I/O operations(scanf, printf, read, write etc.). We assume that when this function is called, the state of the calling thread transits from running state to waiting state and will not run on CPU actively. Therefore, it will create a new kernel thread and schedule the first thread in the ready queue to run(assuming the scheduling algorithm used is FCFS). This calling user-level thread will remain associated with its current kernel thread, initiating I/O and then waiting for it to complete. This function returns 0 on success and -1 otherwise
*/
int uthread_startIO() {
	sem_wait(&lock);
	waiting_threads++;
	sem_post(&lock);
	// Start a new thread which will run new_kernel_thread
	clone(new_kernel_thread, ((void*)malloc(STACK_SIZE)) + STACK_SIZE, CLONE_VM, NULL); // TODO

	return 0;
}

/*
This function should be called right after it finishes I/O operations. We assume that when this function is called, the state of the calling process is switched from waiting state to ready state. It should save the context of current thread and put it in the ready queue. Note that the kernel thread it is currently associated with needs to be terminated after this function is called, because its kernel thread is only for initiating I/O and waiting for the I/O to be completed. The function returns 0 on success and -1 otherwise.
*/
int uthread_endIO() {
	sem_wait(&lock);
	waiting_threads--; // This thread is no longer waiting
	if(kernel_threads == 0) { // No one else is running, so we will use this thread as the new kernel thread
		kernel_threads = 1;
		if(ready.size == 0) {
			// we just exit and keep on executing
			sem_post(&lock);
			return 0;
		}
		else {
			// we should take the first off the ready queue
			list_node *cur = create_node_and_context();
			enq(&ready, cur);
			list_node *next = deq(&ready);
			sem_post(&lock);
			swapcontext(cur->current, next->current);	
			return 0;
		}
	}
	else {
		// Just add us to the ready queue
		list_node *cur = create_node_and_context();
		enq(&ready, cur);
		sem_post(&lock);
		getcontext(cur->current);
		return 0;
	}
	return 0;
}

/*
The calling thread requests to yield the kernel thread to another process. It should save the context of current running thread and load the first one on the ready queue(assuming the scheduling algorithm used is FCFS). The function returns 0 on success and -1 otherwise.
*/
int uthread_yield() {
	sem_wait(&lock);

	if(ready.size == 0) {
		// Do nothing and keep executing
		sem_post(&lock);
		return 0;
	}
	else {
		// Remove from head of queue and add to end
		list_node *next = deq(&ready); 
		list_node *cur = create_node_and_context();
		enq(&ready, cur);

		sem_post(&lock); 
		swapcontext(cur->current, next->current);
		return 0;
	}	
}

/*
This function is called when the calling user-level thread ends its execution. It should schedule the first user-level thread in the ready queue for running.
*/
void uthread_exit() {
	sem_wait(&lock);

	if(ready.size > 0) {
		// Get current head and start executing it
		list_node *next = deq(&ready); 
		sem_post(&lock);
		int res = setcontext(next->current);
		return;
	}
	else {
		kernel_threads--;
		sem_post(&lock);
		return;
	}
}

