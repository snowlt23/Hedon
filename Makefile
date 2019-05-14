build: hedon ;

hedon: ffi.s hedon.c
	gcc -Wall -o hedon ffi.s hedon.c -ldl

test: build
	./test.sh

clean:
	rm hedon
