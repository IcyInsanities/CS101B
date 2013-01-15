CC = gcc
CFLAGS = -Wall -W

all: mysh

mysh: mysh.o

parser: parser.o
	$(CC) $(CFLAGS) -c parser.c

clean: 
	rm -f *.o *~ mysh

.PHONY: all clean

