#define _GNU_SOURCE
#include <stdio.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sched.h>
#include <sys/types.h>
#include <unistd.h>


#define STACK_SIZE 16384
#define DEBUG 0

void system_init();
int uthread_create(void (* func)());
int uthread_startIO();
int uthread_endIO();
int uthread_yield();
void uthread_exit();

struct list_n {
	struct list_n *next;
	ucontext_t *current;
	pid_t id;
};


typedef struct list_n list_node;

struct list {
	volatile list_node *head;
	volatile list_node *tail;
	volatile int size;	
};

sem_t lock;
volatile int kernel_threads;

struct list ready; 


void init_list(struct list *list) {
	list->head = NULL;
	list->tail = NULL;
	list->size = 0;
}

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


list_node* create_node_and_context() {
	ucontext_t *context = (ucontext_t *) malloc(sizeof(ucontext_t));
	context->uc_stack.ss_sp = (void*) malloc(STACK_SIZE);
	context->uc_stack.ss_size = STACK_SIZE;
	context->uc_stack.ss_flags = 0;
	context->uc_link = NULL;
	getcontext(context);
	
	list_node *new_node = (list_node *) malloc(sizeof(list_node));
	new_node->current = context;
	new_node->next = NULL;
	new_node->id = 0;
	return new_node;
}

void deep_free_node(list_node *cur) {
	//free(cur->current->uc_stack.ss_sp);
	//free(cur->current);
	//free(cur);
}

void purge_deletionq(struct list *list) {
	list_node *cur = NULL;	
	while( (cur = deq(list)) != NULL ) {
		deep_free_node(cur);
	}
}

int new_kernel_thread(void *my_node) {
	sem_wait(&lock);
	if (DEBUG) printf("acquired lock in thread_new\n");
	if(kernel_threads > 0) {
		if (DEBUG) printf("GETTOUT\n");
		sem_post(&lock);
		return 0;
	}

	if(ready.size < 1) {
		if (DEBUG) printf("nothing to do\n");
		sem_post(&lock);
		return 0;
	}
	else {
		if (DEBUG) printf("running buddy\n");
		kernel_threads = 1;
		list_node *next = deq(&ready);

		ucontext_t *context = next->current; 
		sem_post(&lock);
		setcontext(context);
		return 0;
	}
}


/*
This function has to be called before any other uthread library functions can be called. It initializes the uthread system. The library should maintain data structure of a ready queue, number of currently running kernel threads (should not exceed 1 in this project) and number of processes that are currently waiting for the I/O operation.

Assumption: This is only ever called once, from the main thread (which we will use for our kernel thread)
*/
void system_init() {
	sem_init(&lock, 0, 1);

	kernel_threads = 1;	

	init_list(&ready);
}


/*
The calling thread requests to create a user-level thread that runs the function func. The context of function should be properly created and stored on the ready queue for execution. The function returns 0 on success and -1 otherwise
*/
int uthread_create(void (* func)()) {
	sem_wait(&lock); // Ensure mutual exclusion when operating on shared data
	if(DEBUG) printf("acquired lock in create\n");

	list_node *new_node = create_node_and_context();
	makecontext(new_node->current, func, 0); // Create a new context with a new stack which will start execution of our function

	enq(&ready, new_node);

	sem_post(&lock);	
	return 0;
}

/*
The calling thread calls this function before it requests for I/O operations(scanf, printf, read, write etc.). We assume that when this function is called, the state of the calling thread transits from running state to waiting state and will not run on CPU actively. Therefore, it will create a new kernel thread and schedule the first thread in the ready queue to run(assuming the scheduling algorithm used is FCFS). This calling user-level thread will remain associated with its current kernel thread, initiating I/O and then waiting for it to complete. This function returns 0 on success and -1 otherwise
*/
int uthread_startIO() {
	sem_wait(&lock);
	if(DEBUG) printf("acquired lock in start\n");
	kernel_threads = 0;
	clone(new_kernel_thread, ((void*)malloc(STACK_SIZE)) + STACK_SIZE - 1, CLONE_VM|CLONE_FILES, NULL);
	if(DEBUG) printf("release lock in start\n");
	sem_post(&lock);

	return 0;
}

/*
This function should be called right after it finishes I/O operations. We assume that when this function is called, the state of the calling process is switched from waiting state to ready state. It should save the context of current thread and put it in the ready queue. Note that the kernel thread it is currently associated with needs to be terminated after this function is called, because its kernel thread is only for initiating I/O and waiting for the I/O to be completed. The function returns 0 on success and -1 otherwise.
*/
int uthread_endIO() {
	sem_wait(&lock); 
	if(DEBUG) printf("acquired lock in endio\n");

	// Search for our thread
	/*list_node *new_node = waiting.head;
	list_node *prev = NULL;

	while(new_node->id != getpid()) {
		prev = new_node;
		new_node = new_node->next;
	}
	
	if(prev == NULL) {
		waiting.head = new_node->next;
	}
	else {
		prev->next = new_node->next;
	}
	if(new_node->next == NULL) {
		waiting.tail = prev;
	}

	waiting.size--;

	if(new_node->id != getpid()){
		printf("\n\nCRITICAL ERROR\n\n");
	}

	new_node->next = NULL;
	*/
	if(kernel_threads == 0) { // No one else is running, so we will use this thread
		kernel_threads = 1;
		if(ready.size == 0) {
			// we just exit and keep on executing
			sem_post(&lock);
		}
		else {
			// we should take the first off the ready queue
			list_node *cur = create_node_and_context();
			enq(&ready, cur);
			list_node *next = deq(&ready);
			sem_post(&lock);
			swapcontext(cur->current, next->current);	
		}
	}
	else {
		list_node *cur = create_node_and_context();
		enq(&ready, cur);
		sem_post(&lock);
		getcontext(cur->current);
	}
	return 0;
}

/*
The calling thread requests to yield the kernel thread to another process. It should save the context of current running thread and load the first one on the ready queue(assuming the scheduling algorithm used is FCFS). The function returns 0 on success and -1 otherwise.
*/
int uthread_yield() {
	sem_wait(&lock);

	// Remove from head of queue and add to end
	if(ready.size == 0) {
		// Do nothing and keep executing
		sem_post(&lock);
		return 0;
	}
	else {
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
	if(DEBUG) printf("acquired lock in exit\n");

	if(ready.size == 0) { // No threads left to schedule, so we let this one die
		//if(DEBUG) printf("no threads left in exit\n");
		kernel_threads = 0;
		if(DEBUG) printf("releasing lock in exit\n");
		sem_post(&lock);
		exit(0);
	}
	else {
		// Get current head and start executing it
		//if(DEBUG) printf("some threads left in exit\n");
		list_node *next = deq(&ready); 
		//if(DEBUG) printf("next: %d\n", next);
		if(DEBUG) printf("releasing lock in exit\n");
		sem_post(&lock);
		int res = setcontext(next->current);
		if(DEBUG) printf("RES IS %d\n", res);
	}
}

