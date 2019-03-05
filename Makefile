build: hedon ;

hedon: hedon.c
	gcc -o hedon hedon.c

test: build
	./test.sh

clean:
	rm hedon
