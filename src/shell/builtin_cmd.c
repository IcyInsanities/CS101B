#include <stdbool.h>
#include <stdint.h>
#include "mysh.h"
#include <unistd.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>

#define CHILD_PROCESS   0
#define NO_CMD_ERROR    0
#define PIPE_READ_SIDE  0
#define PIPE_WRITE_SIDE 1
#define ASCII_NULL      0
#define _GNU_SOURCE

void read_from_pipe (int file) {
       FILE *stream;
       int c;
       stream = fdopen (file, "r");
       while ((c = fgetc (stream)) != EOF)
         putchar (c);
       fclose (stream);
    }
     
void write_to_pipe (int file) {
       FILE *stream;
       stream = fdopen (file, "w");
       fprintf (stream, "hello, world!\n");
       fprintf (stream, "goodbye, world!\n");
       fclose (stream);
    }

int32_t main(uint32_t argc, int8_t *argv[]) {

    pid_t childPID;
    
    int32_t execError;
    int32_t inputPipe[2];
    int32_t outputPipe[2];
    int32_t testError;
    
    int32_t testFile;
    int32_t shirTest;
    
    int32_t in_fd;
    
    int32_t inputFile;
    int32_t outputFile;
    
    uint32_t status;
    int8_t string [256];
    int8_t output [256];
    cmd_struct test_cmd_struct;
    cmd_struct *cmd;

    bool last_pipe_flag = true;
    
    cmd = &test_cmd_struct;
    
    cmd->pipe_flag = true;
    cmd->output = output;
    *(cmd->output) = ASCII_NULL;
    
    // Create the pipes
    pipe(inputPipe);
    pipe(outputPipe);

    // Fork off the child process to execute command
    childPID = fork();
    
    if (childPID >= 0) {
    // If there is no error in forking off the process, proceed
    
        if (childPID == CHILD_PROCESS) {
        // If child process, handle command
            
            
            // Handle STDIN
            if (last_pipe_flag) {
            // Replace input if last pipe exists    
                
                // Close the write end of the pipe
                close(inputPipe[PIPE_WRITE_SIDE]);
                
                // Set read end of pipe to standard input
                dup2(inputPipe[PIPE_READ_SIDE], STDIN_FILENO);
                //read_from_pipe(inputPipe[PIPE_READ_SIDE]);
                 gets (string);
                    printf ("Input pipe works: %s\n",string);
                close(inputPipe[PIPE_READ_SIDE]);

            } else if (*(cmd->input) != ASCII_NULL) {
            // If there is a file specified for stdin, use it
                
                // Open the file to read from
                inputFile = open(cmd->input, O_RDONLY);
                
                // Set STDIN to use input file
                dup2(inputFile, STDIN_FILENO);
                
            } else {
            // If there is no redirection or piping then use STDIN
             
                
                
            }
            
            // Handle STDOUT
            if (cmd->pipe_flag) {
            // If there is an ouput pipe, set it to STDOUT
            
                printf("stdout->pipe\n");
                //close(outputPipe[PIPE_READ_SIDE]);
                
                testFile = open("temp.txt",O_CREAT | O_TRUNC | O_WRONLY, 0);
                printf("reggieISAFUCKER\n");
                // Set write end of pipe to standard output
                //if (dup2(outputPipe[PIPE_WRITE_SIDE], STDOUT_FILENO) == -1) {
                
                errno = 0;
                if (dup2(testFile, STDOUT_FILENO) == -1) {
                    if (errno != 0) {
                    
                        execError = 3;
                    }
                    
                }
                
                write_to_pipe(outputPipe[PIPE_WRITE_SIDE]);
                //close(outputPipe[PIPE_WRITE_SIDE]);
                
                close(testFile);
            
            } else if (*(cmd->output) != ASCII_NULL) {
            // If there is a file specified for stdout, use it
            
                // Open the file to write to, creating it if necessary
                outputFile = open(cmd->output, O_WRONLY);
                
                // Set STDOUT to use the output file
                dup2(outputFile, STDOUT_FILENO);
                
                printf("stdout->file\n");
            
            } else {
            // If there is no redirection or piping, the use normal STDOUT
                
                printf("stdout->default\n");
            }
            
            printf("Child process \n");
            // Run the command, returning any errors
            execvp(argv[1], argv + 1);
            
        } else {
            
            printf("Parent process \n");

            // Setup input pipe if necessary
            if (last_pipe_flag) {
                // Close the read end of the pipe
                close(inputPipe[PIPE_READ_SIDE]);
                
                // Set write end of pipe to standard output
                //shirTest = open("output.txt", O_RDONLY); 
                //dup2(shirTest, inputPipe[PIPE_WRITE_SIDE]);
                write_to_pipe(inputPipe[PIPE_WRITE_SIDE]);
                close(inputPipe[PIPE_WRITE_SIDE]);
                close(shirTest);
                
            }
            
            
            
            // Setup output pipe
            if (cmd->pipe_flag) {
            
                // Close the write end of the pipe
                //close(outputPipe[PIPE_WRITE_SIDE]);
                
                // Set read end of pipe to standard input
                //dup2(outputPipe[PIPE_READ_SIDE], STDIN_FILENO);
                //gets (string);
                 //   printf ("Output pipe works: %s\n",string);
                //close(outputPipe[PIPE_READ_SIDE]);
                
                // Mark that must pipe into next command
                last_pipe_flag = true;

            }
            
            // Wait for child executing command to terminate
            childPID = wait();
            //execError = waitpid(-getgid(), &status, 0);
            if (childPID == -1) {
            
                execError = 2;
                
            }
            
        }
    
    } else {
    // Otherwise, a fork failed
        
        printf("Fork failed \n");
        return 1;
    
    }
    
    return execError;

}
