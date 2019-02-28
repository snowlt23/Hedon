build: hedon ;

hedon: hedon.c
	gcc -o hedon hedon.c

test: hedon
	./test.sh
