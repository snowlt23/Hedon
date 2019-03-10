build: hedon ;

hedon: hedon.c
	gcc -o hedon -ldl hedon.c

test: build
	./test.sh

clean:
	rm hedon
