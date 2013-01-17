# Define constants
OBJS = mysh.o parser.o builtin_cmd.o shell_cmd.o
CC = gcc
CFLAGS = -Wall -W

all: mysh

mysh: $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean: 
	rm -f *.o *~ mysh

.PHONY: all clean