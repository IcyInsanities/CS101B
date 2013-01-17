# Define constants
OBJS = mysh.o parser.o builtin_cmd.o shell_cmd.o
CC = gcc
CFLAGS = -Wall -W -g -lreadline

all: mysh

mysh: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o mysh -lreadline

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean: 
	rm -f *.o *~ mysh

.PHONY: all clean
