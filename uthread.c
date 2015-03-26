#include <stdio.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

#define STACK_SIZE 16384

void system_init();
int uthread_create(void (* func)());
int uthread_startIO();
int uthread_endIO();
int uthread_yield();
void uthread_exit();

struct list_n {
	struct list_n *next;
	ucontext_t *current;
};

typedef struct list_n list_node;

sem_t lock;
int kernel_threads = 0;
int list_size;
list_node *head;
list_node *tail;

list_node* deq() {
	list_node* node = head;
	
	if(list_size == 0) {
		return NULL;
	}
	else if(list_size == 1) {
		list_size--;
		head = tail = NULL;
		node->next = NULL;
		return node;
	}
	else {
		list_size--;
		head = head->next;
		node->next = NULL;
		return node;	
	}	
}

void enq(list_node *node) {
	if(list_size == 0) {
		head = tail = node;
		node->next = NULL;
	}
	else {
		tail->next = node;
		node->next = NULL;
		tail = node;
	}
	list_size++;
	return;
}


list_node* create_node_and_context() {
	ucontext_t *context = (ucontext_t *) malloc(sizeof(ucontext_t));
	getcontext(context);
	context->uc_stack.ss_sp = malloc(STACK_SIZE);
	context->uc_stack.ss_size = STACK_SIZE;
	
	list_node *new_node = (list_node *) malloc(sizeof(list_node));
	new_node->current = context;
	new_node->next = NULL;
	return new_node;
}

void deep_free_node(list_node *cur) {
	free(cur->current->uc_stack.ss_sp);
	free(cur->current);
	free(cur);
	return;
}

/*
This function has to be called before any other uthread library functions can be called. It initializes the uthread system. The library should maintain data structure of a ready queue, number of currently running kernel threads (should not exceed 1 in this project) and number of processes that are currently waiting for the I/O operation.

Assumption: This is only ever called once, from the main thread (which we will use for our kernel thread)
*/
void system_init() {
	sem_init(&lock, 0, 1);
	kernel_threads = 1;	
	list_size = 0;
	head = tail = NULL;

	list_node *main_node = create_node_and_context(); 

	enq(main_node);
}

/*
The calling thread requests to create a user-level thread that runs the function func. The context of function should be properly created and stored on the ready queue for execution. The function returns 0 on success and -1 otherwise
*/
int uthread_create(void (* func)()) {
	list_node *new_node = create_node_and_context();
	makecontext(new_node->current, func, 0); // Create a new context with a new stack which will start execution of our function

	sem_wait(&lock); // Ensure mutual exclusion when operating on shared data
	enq(new_node);
	
	if(list_size == 1) { // There was no thread running previously, so we need to start one
		// Start a new kernel thread for this user thread to run on TODO
	}

	sem_post(&lock);	
}

/*
The calling thread calls this function before it requests for I/O operations(scanf, printf, read, write etc.). We assume that when this function is called, the state of the calling thread transits from running state to waiting state and will not run on CPU actively. Therefore, it will create a new kernel thread and schedule the first thread in the ready queue to run(assuming the scheduling algorithm used is FCFS). This calling user-level thread will remain associated with its current kernel thread, initiating I/O and then waiting for it to complete. This function returns 0 on success and -1 otherwise
*/
int uthread_startIO() {
	sem_wait(&lock);
	// Assume this function is at head of queue
	list_node *cur = deq();
	
	if(list_size == 1) {
		// Nothing to do after we remove this guy	
	}
	else { 
		// Start a new kernel thread for this to run on
	}
	
	sem_post(&lock);
}

/*
This function should be called right after it finishes I/O operations. We assume that when this function is called, the state of the calling process is switched from waiting state to ready state. It should save the context of current thread and put it in the ready queue. Note that the kernel thread it is currently associated with needs to be terminated after this function is called, because its kernel thread is only for initiating I/O and waiting for the I/O to be completed. The function returns 0 on success and -1 otherwise.
*/
int uthread_endIO() {
	sem_wait(&lock);

	
	sem_post(&lock);
}

/*
The calling thread requests to yield the kernel thread to another process. It should save the context of current running thread and load the first one on the ready queue(assuming the scheduling algorithm used is FCFS). The function returns 0 on success and -1 otherwise.
*/
int uthread_yield() {
	sem_wait(&lock);
	// Remove from head of queue and add to end
	if(list_size == 0) {
		// This should never, ever happen. At the very least, this thread is still on the ready queue
		printf("ERROR: ENCOUNTERED EMPTY READY QUEUE IN UTHREAD_YIELD");
		sem_post(&lock);
		return -1;
	}
	if(list_size == 1) {
		// This is the only one on the ready queue, so let's just keep on executing
		sem_post(&lock);
		return 0;
	}
	else {
		// Save our context, remove from front of queue
		list_node *cur = deq(); // Assume we are on front of queue, since we are executing
		enq(cur);

		sem_post(&lock); 
		swapcontext(cur->current, head->current);
		return 0;
	}	
}

/*
This function is called when the calling user-level thread ends its execution. It should schedule the first user-level thread in the ready queue for running.
*/
void uthread_exit() {
	sem_wait(&lock);
	
	list_node *cur = deq();
	
	free(cur->current->uc_stack.ss_sp);
	free(cur->current);
	free(cur);
	
	if(list_size == 0) { // No threads left to schedule, so we let this one die
		sem_post(&lock);
		exit(0);
	}
	else {
		// Get current head and start executing it
		ucontext_t *context = head->current; 
		sem_post(&lock);
		setcontext(context); // And we never return
	}
	
	sem_post(&lock);
}


