#ifndef UTHREAD_H
#define UTHREAD_H
#include <ucontext.h>

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

struct list {
   list_node *head;
	list_node *tail;
	int size;   
}; 

#endif
