#include <stdbool.h>
#include <stdint.h>
#include "mysh.h"
#include <unistd.h>
#include <stdio.h>

#include <sys/types.h>
#include <stdlib.h>

#define CHILD_PROCESS 0

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

int8_t main(uint32_t argc, int8_t *argv[]) {

    pid_t childPID;
    uint8_t execError;
    uint32_t fileDesc[2];
    
    int8_t *arg_list[] = {"ls", "-l", NULL};
    cmd_struct cmd;
    
    pipe(fileDesc);
    
    bool last_pipe_flag = true;

    childPID = fork();
    if (childPID >= 0) {
    
        if (childPID == CHILD_PROCESS) {
        // If child process, handle command
            
            
            if (last_pipe_flag) {
            // Replace input if last pipe exists    
                //cmd->input = last_pipe;
                //cmd->output = next_pipe;
                
                // Close the write end of the pipe
                close(fileDesc[1]);
                
                // Set read end of pipe to standard input
                dup2(fileDesc[0], STDIN_FILENO);
                read_from_pipe(fileDesc[0]);
                close(fileDesc[0]);
                
            }
            

            // If there is a pipe symbol at end of command, mark it
            if (cmd.pipe_flag) {

                last_pipe_flag = true;
                
            }
            
            // Run the command, returning any errors
            
            execError = execvp(argv[1], argv + 1);
            
            //execv("/bin/", argv);
            
            printf("Child process \n");
            
        } else {
            

            
            if (last_pipe_flag) {
                // Close the read end of the pipe
                close(fileDesc[0]);
                
                // Set write end of pipe to standard output
                dup2(fileDesc[1], STDOUT_FILENO);
                printf("Parent process \n");
                write_to_pipe(fileDesc[1]);
                close(fileDesc[1]);
                
            }
            
            
        }
    
    } else {
    // Otherwise, a fork failed
        
        printf("Fork failed \n");
        return 1;
    
    }
   
    return 0;
    
    
}
