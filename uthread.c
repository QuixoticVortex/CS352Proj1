#include <stdio.h>

void system_init();
int uthread_create(void (* func)());
int uthread_startIO();
int uthread_endIO();
int uthread_yield();
void uthread_exit();

void system_init() {
	printf("Test");
}


