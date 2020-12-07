all: lsh.o util.o
	gcc -o lsh util.o lsh.o -lpthread

util.o: util.c util.h
	gcc -g ./util.c -o util.o -c

lsh.o: lsh.c util.h
	gcc -g lsh.c -o lsh.o -c

clean:
	rm lsh.o lsh util.o
