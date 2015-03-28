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
		//printf ("ult %d yields \n",id );
		//uthread_yield ();
		//printf ("ult %d resumes \n",id);
		uthread_create(do_something);
		n_threads++;
	}
	printf("ult %d starts I/O \n",id);
	uthread_startIO();
	sleep (1); //simulate some long−time I/O operation
	// printf("calling endIO - %d \n", id);
	uthread_endIO();
	printf("ult %d returns from I/O \n",id); 
	//printf("ult %d exits\n", id);
	uthread_exit ();
	printf("After exit - %d?\n\n", id);
}

void do_nothing() {
	int id = myid++;
	printf("- enter child thread %d and start IO\n", id);
	uthread_startIO();
	//sleep(1);
	int b = 0;
	int i;
	for(i = 0; i < 1000; i++) {
		b++;
	}
	printf("- after sleep in child %d\n", id);
	uthread_endIO();
	printf("- child thread %d endIO\n", id);	
	uthread_exit();
	printf("- after exit - child %d\n", id);
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

/*	system_init();
	uthread_create(do_nothing);
	printf("- start IO\n");
	uthread_startIO();
	//sleep(1);
	printf("- after main sleep\n");
	uthread_endIO();
	printf("- ended IO\n");
	uthread_exit();
	printf("- after exit - main\n"); */
}
