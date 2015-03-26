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

sem_t lock;
int kernel_threads = 0;
int list_size;
list_node *head;
list_node *tail;
list_node *waiting_head;

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

void push(list_node *node) {
	node->next = head;
	head = node;
	if(list_size == 0) {
		tail = node;
	}
	list_size++;
	return;
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
	context->uc_stack.ss_sp = (void*) malloc(STACK_SIZE);
	context->uc_stack.ss_size = STACK_SIZE;
	
	list_node *new_node = (list_node *) malloc(sizeof(list_node));
	new_node->current = context;
	new_node->next = NULL;
	new_node->id = 0;
	return new_node;
}

void *deep_free_node(list_node *cur) {
	void* stack = cur->current->uc_stack.ss_sp;
	ucontext_t *current = cur->current;
	free(cur);
	free(current);
	return stack;
}

void free_kernel_stack(void *cur) {
	free(cur - STACK_SIZE + 1);
	return;
}

void* new_kernel_stack() {
	void *stack = (void*) malloc(STACK_SIZE);
	stack += STACK_SIZE - 1;
	return stack;
}

int new_kernel_thread(void *my_stack) {
	sem_wait(&lock);

	if(list_size < 1) {
		printf("New kernel thread usage error: Ensure 1 thread on ready queue when calling\n\n");
		sem_post(&lock);
		return 0;
	}
	else {
		list_node *next = head;
		free_kernel_stack(my_stack);

		ucontext_t *context = next->current; 
		sem_post(&lock);
		printf("Created new thread. Now executing\n");
		setcontext(context); // And we never return
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
	list_size = 0;
	head = tail = NULL;
	waiting_head = NULL;

	list_node *main_node = create_node_and_context(); 

	enq(main_node); // Currently executing main thread is only thread on queue
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
		// Start a new kernel thread for this user thread to run on 
		printf("Starting new kernel thread from create\n");	
		void* child_stack = new_kernel_stack();
		sem_post(&lock);
		clone(new_kernel_thread, child_stack, 
			CLONE_VM|CLONE_FILES, child_stack); 	// TODO -use return values

		return 0;
	}

	sem_post(&lock);	
	return 0;
}

/*
The calling thread calls this function before it requests for I/O operations(scanf, printf, read, write etc.). We assume that when this function is called, the state of the calling thread transits from running state to waiting state and will not run on CPU actively. Therefore, it will create a new kernel thread and schedule the first thread in the ready queue to run(assuming the scheduling algorithm used is FCFS). This calling user-level thread will remain associated with its current kernel thread, initiating I/O and then waiting for it to complete. This function returns 0 on success and -1 otherwise
*/
int uthread_startIO() {
	sem_wait(&lock);
	// Assume this function is at head of queue
	list_node *cur = deq();
	cur->next = waiting_head;
	waiting_head = cur;
	cur->id = getpid(); // This will stay the same when we endIO, so we can relocate this
	
	if(list_size == 0) {
		// Nothing to do 
	}
	else { 
		// Start a new kernel thread for next to run on
		
		void *child_stack = new_kernel_stack();	
		clone(new_kernel_thread, child_stack, CLONE_VM|CLONE_FILES, child_stack); 	
		sem_post(&lock);
		return 0;
	}
	
	sem_post(&lock);
}

/*
This function should be called right after it finishes I/O operations. We assume that when this function is called, the state of the calling process is switched from waiting state to ready state. It should save the context of current thread and put it in the ready queue. Note that the kernel thread it is currently associated with needs to be terminated after this function is called, because its kernel thread is only for initiating I/O and waiting for the I/O to be completed. The function returns 0 on success and -1 otherwise.
*/
int uthread_endIO() {

	sem_wait(&lock); // Ensure mutual exclusion when operating on shared data
	
	// Search for our thread
	list_node *new_node = waiting_head;
	list_node *prev = NULL;
	while(new_node->id != getpid()) {
		prev = new_node;
		new_node = new_node->next;
	}
	
	if(prev == NULL) {
		waiting_head = new_node->next;
	}
	else {
		prev->next = new_node->next;
	}
	
	enq(new_node);
	
	if(list_size == 1) { // There was no thread running previously, so we need to start one
		// Start a new kernel thread for this user thread to run on 
		
		//void* child_stack = new_kernel_stack();
		//clone(new_kernel_thread, child_stack, 
		//	CLONE_VM|CLONE_FILES, child_stack); 	// TODO -use return values

		sem_post(&lock);
		//getcontext(new_node->current);
		//swapcontext(new_node->current, head->current);
		return 0;
	}
	else {
		sem_post(&lock);
		swapcontext(new_node->current, head->current);
		return 0;
	}
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
	

	if(list_size == 0) { // No threads left to schedule, so we let this one die
		sem_post(&lock);
		void* stack = deep_free_node(cur); 
		free(stack);
		exit(0);
	}
	else {
		// Get current head and start executing it
		ucontext_t *context = head->current; 
		sem_post(&lock);
		void* stack = deep_free_node(cur); 
		setcontext(context); // And we never return
	}
	
	sem_post(&lock);
}


