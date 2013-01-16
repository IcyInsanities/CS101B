#include <stdbool.h>
#include <stdint.h>
#include "mysh.h"
#include "builtin_cmd.h"
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
#define NO_DUP_REDER    0
#define _GNU_SOURCE

/*
 * TODO:
 *  - Finish up piping
 *  - Implement redirection commands (DONE)
 *  - Implement append redirection instead of truncation (DONE)
 *  - Implement output duplication
 *  - Error handling
 *      - Executed command throws error
 *      - Fork fails
 *      - Creating pipe fails
 *      - dup fails?
 *      - open/close fails
 *      - wait throws error
 * 
 */

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

    uint8_t outputFileStr[14] = "redir_out.txt\0";
    uint8_t inputFileStr[13] =  "redir_in.txt\0";
    
    pid_t childPID;
    
    int32_t outputFile;
    int32_t inputFile;
    
    int32_t execError;
    
    // Define pipes for piping in and out of child (executed command)
    int32_t inputPipe[2];
    int32_t outputPipe[2];
    
    int32_t appendFlag;
    
    int32_t testError;
    
    int32_t testFile;
    int32_t shirTest;
    
    int32_t in_fd;
    
    // Define input and output files
    int32_t inputFileDescr;
    int32_t outputFileDescr;
    
    uint32_t status;
    int8_t string [256];
    
    // Define a command structure for testing
    cmd_struct test_cmd_struct;
    cmd_struct *cmd;

    bool last_pipe_flag = false;
    
    cmd = &test_cmd_struct;
    
    cmd->pipe_flag = false;
    cmd->output = outputFileStr;
    *(cmd->output) = ASCII_NULL;
    cmd->input = inputFileStr;
    *(cmd->input) = ASCII_NULL;
    
    cmd->reder_desc1 = STDERR_FILENO;
    cmd->reder_desc2 = STDOUT_FILENO;
    
    // Truncate file by default
    cmd->trun_flag = true;
    
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
            // If there is a file specified for redirection of stdin, use it
                
                // Open the file to read from
                inputFile = open(cmd->input, O_RDONLY);
                
                // Set STDIN to use input file
                dup2(inputFile, STDIN_FILENO);
                
                // Close input pipe if not used
                close(inputPipe[PIPE_READ_SIDE]);
                close(inputPipe[PIPE_WRITE_SIDE]);
                
            } else {
            // If there is no redirection or piping then use STDIN
             
                
                
            }
            
            // Handle STDOUT
            if (cmd->pipe_flag) {
            // If there is an ouput pipe, set it to STDOUT
            
                printf("stdout->pipe\n");
                //close(outputPipe[PIPE_READ_SIDE]);
                
                // Open a test file
                testFile = open("temp.txt",O_CREAT | O_TRUNC | O_WRONLY, 0);
                //testFile = open("temp.txt",O_CREAT | O_APPEND | O_WRONLY, 0);
                
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
            // If there is a file specified for redirection of stdout, use it

                // Check if truncating or appending
                if ((cmd->trun_flag) == false) {
                
                    appendFlag = O_APPEND;
                } else {
                
                    appendFlag = O_TRUNC;
                    
                }
                
                // Open the file to write to, creating it if necessary
                outputFile = open(cmd->output, O_CREAT | O_WRONLY | appendFlag);
                
                // Set STDOUT to use the output file
                dup2(outputFile, STDOUT_FILENO);
                
                printf("stdout->file\n");
                
                // Need to close the output pipe if not piping out
                close(outputPipe[PIPE_READ_SIDE]);
                close(outputPipe[PIPE_WRITE_SIDE]);
                
                
            } else if (cmd->reder_desc1 > NO_DUP_REDER && cmd->reder_desc2 > NO_DUP_REDER) {
            // if there is duplication redirection, handle it
            
                // Open the file to write to, creating it if necessary
                outputFile = open("dup_test.txt", O_CREAT | O_WRONLY | O_TRUNC);
                
                // Duplicate the output
                dup2(cmd->reder_desc2, cmd->reder_desc1);
                
                printf("I am in stdout\n");
                fprintf(stderr, "I am in stderr\n");

            
            } else {
            // If there is no redirection or piping, the use normal STDOUT
                
                printf("stdout->default\n");
            }
            
            printf("Child process \n");
            
            // Run the command, returning any errors
            execvp(argv[1], argv + 1);
            
            // Close output file
            close(outputFile);
            
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
            
            // Wait for child executing command to terminate
            childPID = wait();
            //execError = waitpid(-getgid(), &status, 0);
            if (childPID == -1) {
            
                execError = 2;
                
            }
            
                        
            // Setup piping into next command
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
                
                // Need to properly pipe output of command into next command
                // Set input pipe to previous output pipe
                inputPipe[PIPE_WRITE_SIDE] = outputPipe[PIPE_READ_SIDE];
                // TODO: May have to make new read side for input pipe so child process has something to read...
                
            }
            
            // TODO: Overwrite (make new) read side for output pipe for the new command
            
        }
    
    } else {
    // Otherwise, a fork failed
        
        printf("Fork failed \n");
        return 1;
    
    }
    
    return execError;

}


