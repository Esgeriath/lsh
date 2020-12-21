debug = -g
all: lsh.o util.o jobs.o
	gcc -o lsh util.o lsh.o jobs.o -lpthread

util.o: util.c util.h
	gcc $(debug) util.c -o util.o -c

jobs.o: jobs.c jobs.h util.h
	gcc $(debug) jobs.c -o jobs.o -c

lsh.o: lsh.c util.h jobs.h
	gcc $(debug) lsh.c -o lsh.o -c

clean:
	rm lsh.o lsh util.o jobs.o || true
