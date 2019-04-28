build: hedon ;

hedon: ffi.s hedon.c
	gcc -Wall -o hedon ffi.s hedon.c -ldl

shell-test: build
	./test.sh

lang-test: build
	./hedon -l test.hedon -c

test: shell-test lang-test ;

clean:
	rm hedon
