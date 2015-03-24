#include <stdio.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

void system_init();
int uthread_create(void (* func)());
int uthread_startIO();
int uthread_endIO();
int uthread_yield();
void uthread_exit();

void system_init() {
	printf("Test");
}

int uthread_create(void (* func)()) {

}

int uthread_startIO() {

}

int uthread_endIO() {

}

int uthread_yield() {

}

void uthread_exit() {

}


