#include <stdbool.h>
#include <stdint.h>
#include "mysh.h"
#include "gen.h"
#include "builtin_cmd.h"
#include <unistd.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>

#define NO_DUP_REDIR            0

#define _GNU_SOURCE

/*
 * TODO:
 *  - Finish up piping (DONE)
 *  - Implement case where redirection and duplication
 *  - Implement redirection commands (DONE)
 *  - Implement append redirection instead of truncation (DONE)
 *  - Implement output duplication (DONE)
 *  - Error handling
 *      - Executed command throws error (DONE)
 *      - Fork fails (DONE)
 *      - Creating pipe fails (DONE)
 *      - dup fails (DONE)
 *      - open/close fails (DONE)
 *      - wait throws error (DONE)
 *
 */

//// int8_t string [256];
////
//// void read_from_pipe (int file) {
////        FILE *stream;
////        int c;
////        stream = fdopen (file, "r");
////        while ((c = fgetc (stream)) != EOF)
////          putchar (c);
////        fclose (stream);
////     }
////
//// void write_to_pipe (int file) {
////        FILE *stream;
////        stream = fdopen (file, "w");
////        fprintf (stream, "hello, world!\n");
////        fprintf (stream, "goodbye, world!\n");
////        fclose (stream);
////     }

int32_t ExecCommand(cmd_struct *cmd, int32_t *inputPipe, int32_t *outputPipe) {

    // Input and output files
    int32_t outputFile = -1;
    int32_t inputFile = -1;

    // Error status
    int32_t execError = 0;

    int32_t testFile;

    // Append to file option flag
    int32_t appendFlag;
    
    fprintf(stdout, "Child: I RUN!\n");

    // Handle input
    // Replace input if last pipe exists
    if (inputPipe != NULL) {
        fprintf(stdout, "Child: input pipe\n");

        // Set read end of pipe to standard input
        if(dup2(inputPipe[PIPE_READ_SIDE], STDIN_FILENO) == -1) {
            // Failed to setup input pipe
            fprintf(stderr, "Child: failed to setup input pipe\n");
            return errno;
        }
    }
    // If there is a file specified for redirection of stdin, use it
    else if ((cmd->input != NULL) && (*(cmd->input) != ASCII_NULL)) {
        fprintf(stdout, "Child: input files\n");
        // Open the file to read from
        inputFile = open((char*) cmd->input, O_RDWR);

        // If opening file fails, report error
        if (inputFile == -1) {
            return errno;
        }
        
        // Set STDIN to use input file
        if(dup2(inputFile, STDIN_FILENO)) {
            // Failed to setup input file
            fprintf(stderr, "Child: failed to setup input file\n");
            return errno;
        }
    }
    // If there is no redirection or piping then use STDIN, no code needed
    else {fprintf(stdout, "Child: no input!\n");}

    // Handle output
    // If there is an ouput pipe, set it to STDOUT
    if (cmd->pipe_flag == true && outputPipe != NULL) {
        ////fprintf(stdout, "Child: output pipe\n");
        
        // If closing the pipe fails, report an error
        if(close(outputPipe[PIPE_READ_SIDE]) == -1) {
            return errno;
        }

        // Set write end of pipe to standard output
        if (dup2(outputPipe[PIPE_WRITE_SIDE], STDOUT_FILENO) == -1) {
            // Failed to setup output pipe from child
            fprintf(stderr, "Child: failed to setup output pipe\n");
            return errno;
        }
        // If closing the pipe fails, report an error
        if (close(outputPipe[PIPE_WRITE_SIDE]) == -1) {
            return errno;
        }
    }
    // If there is a file specified for redirection of stdout, use it
    else if ((cmd->output != NULL) && (*(cmd->output) != ASCII_NULL) && !(cmd->redir_desc1
        > NO_DUP_REDIR && cmd->redir_desc2 > NO_DUP_REDIR)) {
        ////fprintf(stdout, "Child: output file\n");
        // Check if truncating or appending
        if ((cmd->trun_flag) == false) {
            appendFlag = O_APPEND;
        }
        else {
            appendFlag = O_TRUNC;
        }

        // Open the file to write to, creating it if necessary
        outputFile = open((char*) cmd->output, O_CREAT | O_RDWR | appendFlag);

        // If opening the output file fails, report an error
        if (outputFile == -1) {
            return errno;
        }
        
        // Set STDOUT to use the output file
        if (dup2(outputFile, STDOUT_FILENO) == -1) {
            // Failed to setup output file
            fprintf(stderr, "Child: failed to setup output file.\n");
            return errno;
        }
    }
    // if there is duplication redirection, handle it
    else if (cmd->redir_desc1 > NO_DUP_REDIR && cmd->redir_desc2 > NO_DUP_REDIR &&
        !((cmd->output != NULL) && (*(cmd->output) != ASCII_NULL))) {
        ////fprintf(stdout, "Child: dup redir\n");

        // Duplicate the output
        if(dup2(cmd->redir_desc2, cmd->redir_desc1) == -1) {
            // Failed to setup duplicate redirection
            fprintf(stderr, "Child: failed to setup dup redirection.\n");
            return errno;
        };
    }
    // Handle case with duplicate redirection and normal redirection
    else if (cmd->redir_desc1 > NO_DUP_REDIR && cmd->redir_desc2 > NO_DUP_REDIR &&
        (cmd->output != NULL) && (*(cmd->output) != ASCII_NULL)) {
        
        // Check if truncating or appending
        if ((cmd->trun_flag) == false) {
            appendFlag = O_APPEND;
        }
        else {
            appendFlag = O_TRUNC;
        }
        
        // Open the file to write to, creating it if necessary
        outputFile = open((char*) cmd->output, O_CREAT | O_RDWR | appendFlag);
        
        // If opening the output file fails, report an error
        if (outputFile == -1) {
            return errno;
        }
        
        // Handle case for dup redirection first,
        if (cmd->redir_desc_first) {
            
            dup2(cmd->redir_desc2, cmd->redir_desc1);
            dup2(outputFile, STDOUT_FILENO);

        }
        else {
        
            dup2(outputFile, STDOUT_FILENO);
            dup2(cmd->redir_desc2, cmd->redir_desc1);
        }
        
    }
    // If there is no redirection or piping, the use normal STDOUT, no code here
    else {fprintf(stdout, "Child: no output!\n");}
    
    // Run the command, returning any errors
    fprintf(stdout, "Child: executing\n");
    if (execvp((char*) cmd->arg_array[0], (char**) cmd->arg_array) == -1) {
        // Failed to execute command
        fprintf(stderr, "Child: failed to execute command.\n");
        return errno;
    }
    fprintf(stdout, "Child: I live!\n");

    // Close input file
    if (inputFile != -1) {
        close(inputFile);
    }
    // Close output file
    if (outputFile != -1) {
        close(outputFile);
    }
    
    return execError;
}

/*
int32_t main(uint32_t argc, int8_t *argv[]) {

    uint8_t outputFileStr[14] = "redir_out.txt\0";
    uint8_t inputFileStr[13] =  "redir_in.txt\0";

    pid_t childPID;

    int32_t execError;

    // Define pipes for piping in and out of child (executed command)
    int32_t inputPipe[2];
    int32_t outputPipe[2];

    int32_t testError;

    int32_t shirTest;

    int32_t in_fd;

    // Define input and output files
    int32_t inputFileDescr;
    int32_t outputFileDescr;

    uint32_t status;

    // Define a command structure for testing
    cmd_struct test_cmd_struct;
    cmd_struct *cmd;

    bool last_pipe_flag = false;

    cmd = &test_cmd_struct;

    cmd->pipe_flag = false;
    cmd->output = outputFileStr;
    // *(cmd->output) = ASCII_NULL;
    cmd->input = inputFileStr;
    *(cmd->input) = ASCII_NULL;

    //cmd->redir_desc1 = STDERR_FILENO;
    cmd->redir_desc1 = NO_DUP_REDIR;
    //cmd->redir_desc2 = STDOUT_FILENO;
    cmd->redir_desc2 = NO_DUP_REDIR;

    // Truncate file by default
    cmd->trun_flag = true;

    // Create the pipes
    pipe(inputPipe);
    pipe(outputPipe);

    cmd->arg_array = argv;

    // Fork off the child process to execute command
    childPID = fork();

    if (childPID >= 0) {
    // If there is no error in forking off the process, proceed

        if (childPID == CHILD_PROCESS) {
        // If child process, handle command

            printf("Child process \n");
            execError = ExecCommand(cmd, NULL, NULL);
            printf("End child process \n");

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
*/

