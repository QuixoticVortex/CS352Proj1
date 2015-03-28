all: main

main: uthread.o main.o 
	gcc -o main main.o uthread.o -lpthread

uthread.o: uthread.h uthread.c
	gcc -c uthread.c

main.o: uthread.h main.c
	gcc -c main.c

clean:
	rm *.o main



