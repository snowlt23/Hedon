build: hedon ;

hedon: ffi.s hedon.c
	gcc -Wall -o hedon -ldl ffi.s hedon.c

test: build
	./test.sh

clean:
	rm hedon
