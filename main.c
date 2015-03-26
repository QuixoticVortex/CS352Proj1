#include "uthread.c" //suppose the uthread library code is in uthread.c
#include <stdio.h>
int n_threads=0;
int myid=0;

void do_something()
{
	int id;
	id=myid;
	myid++;
	printf ("This is ult %d\n", id); //just for demo purpose
	if(n_threads<5){
		uthread_create(do_something);
		n_threads++;
		printf ("ult %d yields \n",id );
		uthread_yield ();
		printf ("ult %d resumes \n",id);
		uthread_create(do_something);
		n_threads++;
	}
	/*printf ("ult %d starts I/O \n",id);
	uthread_startIO();
	sleep (2); //simulate some long−time I/O operation
	uthread_endIO();
	printf ("ult %d returns from I/O \n",id); */
	printf("ult %d exits\n", id);
	uthread_exit ();
	printf("After exit - %d?\n", id);
}

int main()
{
	int i ;
	system_init ();
	printf("create first thread\n");
	uthread_create(do_something);
	printf("main exits\n");
	uthread_exit ();
	printf("After exit - main?\n");
}
