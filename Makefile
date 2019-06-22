OBJS=utils.o stack.o typeapi.o typesystem.o codegen.o primitives.o

build: hedon ;

%.o: %.c hedon.h
	gcc -Wall -Wextra -c $<

hedon: ffi.s hedon.o $(OBJS)
	gcc -Wall -Wextra -o hedon ffi.s hedon.o $(OBJS) -ldl

test: build
	./test.sh

clean:
	rm hedon *.o
