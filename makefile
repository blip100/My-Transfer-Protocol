init: libmsocket.a initmsocket.c
	gcc initmsocket.c -pg -o init -L. -lmsocket;

run: init
	./init;

libmsocket.a: msocket.o
	ar rcs libmsocket.a msocket.o

msocket.o: msocket.h msocket.c
	gcc -c msocket.c -o msocket.o;

u1: libmsocket.a user1.c
	gcc user1.c -o u1 -L. -lmsocket;

run1: u1
	./u1;

u2: libmsocket.a user2.c
	gcc user2.c -o u2 -L. -lmsocket;

run2: u2
	./u2;

u3: libmsocket.a user3.c
	gcc user3.c -o u3 -L. -lmsocket;

run3: u3
	./u3;

u4: libmsocket.a user4.c
	gcc user4.c -o u4 -L. -lmsocket;

run4: u4
	./u4;

clean:
	rm -f *.o *.a u1 u2 init;