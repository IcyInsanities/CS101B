/* This file contains the code the execution of system built in commands
 *
 * Team: Shir, Steven, Reggie
*/

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "mysh.h"
#include "gen.h"
#include "builtin_cmd.h"

#define _GNU_SOURCE

#define NO_DUP_REDIR    0

// This function takes a command struct and pointers to pipes (if existing) and
// executes the command. This function assumes that it is the child process.
int32_t ExecCommand(cmd_struct *cmd, int32_t *inputPipe, int32_t *outputPipe) {

    // Input and output files
    int32_t outputFile = -1;
    int32_t inputFile = -1;

    // Error status
    int32_t execError = 0;

    // Append to file option flag
    int32_t appendFlag;

    // Handle input
    // Replace input if last pipe exists
    if (inputPipe != NULL) {
        // Set read end of pipe to standard input
        if(dup2(inputPipe[PIPE_READ_SIDE], STDIN_FILENO) == -1) {
            // Failed to setup input pipe
            return errno;
        }
    }
    // If there is a file specified for redirection of stdin, use it
    else if ((cmd->input != NULL) && (*(cmd->input) != ASCII_NULL)) {
        // Open the file to read from
        inputFile = open((char*) cmd->input, O_RDWR);

        // If opening file fails, report error
        if (inputFile == -1) {
            return errno;
        }

        // Set STDIN to use input file
        if(dup2(inputFile, STDIN_FILENO)) {
            // Failed to setup input file
            return errno;
        }
    }
    // If there is no redirection or piping then use STDIN, no code needed

    // Handle output
    // If there is an ouput pipe, set it to STDOUT
    if (cmd->pipe_flag == true && outputPipe != NULL) {
        // If closing the pipe fails, report an error
        if(close(outputPipe[PIPE_READ_SIDE]) == -1) {
            return errno;
        }

        // Set write end of pipe to standard output
        if (dup2(outputPipe[PIPE_WRITE_SIDE], STDOUT_FILENO) == -1) {
            // Failed to setup output pipe from child
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
            return errno;
        }
    }
    // if there is duplication redirection, handle it
    else if (cmd->redir_desc1 > NO_DUP_REDIR && cmd->redir_desc2 > NO_DUP_REDIR &&
        !((cmd->output != NULL) && (*(cmd->output) != ASCII_NULL))) {

        // Duplicate the output
        if(dup2(cmd->redir_desc2, cmd->redir_desc1) == -1) {
            // Failed to setup duplicate redirection
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

    // Run the command, returning any errors
    if (execvp((char*) cmd->arg_array[0], (char**) cmd->arg_array) == -1) {
        // Close input file
        if (inputFile != -1) {
            close(inputFile);
        }
        // Close output file
        if (outputFile != -1) {
            close(outputFile);
        }
        
        // Failed to execute command
        return errno;
    }
    
    // Should never get here
    return 1;
}
