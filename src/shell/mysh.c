/* This file contains the main loop functions to run a command shell.
 *
 * Team: Shir, Steven, Reggie
*/
 // TODO: handle close and pipe errors
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "gen.h"
#include "mysh.h"
#include "parser.h"
#include "builtin_cmd.h"
#include "shell_cmd.h"

#define PARENT_OUTPUT_PIPE_ERROR   1

// Print the shell prompt in the format username:current/directory>
void print_prompt() {
    // Allocate space for username and current path
    uint8_t* username;
    uint8_t curr_path[PATH_MAX]; // Use maximum system path length

    // System calls to obtain username and path
    username = (uint8_t*) getlogin();
    getcwd( (char*) curr_path, PATH_MAX);

    // Print out prompt and return1
    printf("%s:%s> ", username, curr_path);
    return;
}

void handle_errors(int32_t error_code) {

    switch (error_code) {
    
        // No error, do nothing
        case 0:
            break;
            
        case EACCES:
            printf("Error:%d File access denied.\n", error_code);
            break;
            
        case EEXIST:
            printf("Error:%d Path already exists.\n", error_code);
            break;
            
        case EISDIR:
            printf("Error:%d Path is not a file.\n", error_code);
            break;
            
        case EMFILE:
            printf("Error:%d Maximum number of open files reached.\n", error_code);
            break;
            
        case ENAMETOOLONG:
            printf("Error:%d Pathname is too long.\n", error_code);
            break;
            
        case ENFILE:
            printf("Error:%d Maximum number of open files reached.\n", error_code);
            break;
            
        case ENOENT:
            printf("Error:%d File does not exist.\n", error_code);
            break;
            
        case ENOSPC:
            printf("Error:%d Device out of space.\n", error_code);
            break;
            
        case ENOTDIR:
            printf("Error:%d Path is not a directory.\n", error_code);
            break;
            
        case EOVERFLOW:
            printf("Error:%d File too large.\n", error_code);
            break;
            
        case EROFS:
            printf("Error:%d Path is read-only.\n", error_code);
            break;
            
        case ETXTBSY:
            printf("Error:%d Requested path busy.\n", error_code);
            break;
            
        case EBADF:
            printf("Error:%d Invali file descriptor.\n", error_code);
            break;
            
        case EINTR:
            printf("Error:%d Close interrupted.\n", error_code);
            break;
            
        case EIO:
            printf("Error:%d I/O error.\n", error_code);
            break;
            
        case EFAULT:
            printf("Error:%d Invalid pipe file descriptor.\n", error_code);
            break;
            
        case EINVAL:
            printf("Error:%d (pipe2()) Invalid flag value.\n", error_code);
            break;
            
        case E2BIG:
            printf("Error:%d Environment and/or argument list too large.\n", error_code);
            break;
            
        case ECHILD:
            printf("Error:%d Liar, waitpid() does not work!!!\n", error_code);
            break;
            
        default:
            printf("Error:%d Liar, waitpid() does not work!!!\n", error_code);
            break;
    }
    return;
}
// Main function to run shell
int main() {
    // Allocate a history buffer
    uint8_t cmd_history[HISTORY_LENGTH][MAX_CMD_LENGTH];
    // Index to the current location in the command history for cicular array
    int32_t cmd_history_idx = 0;
    // Current command string
    uint8_t cmd_str[MAX_CMD_LENGTH];
    // Location of parsing (for piped commands)
    uint8_t* curr_str = NULL;
    // Command struct to hold parsed details
    cmd_struct cmd;
    // Define pipes for piping in and out of child (executed command)
    int32_t pipe1[2];
    int32_t pipe2[2];
    // Pointer elements to the pipes for swapping purpose
    int32_t* input_pipe  = pipe1;
    int32_t* output_pipe = pipe2;
    int32_t* swap_dummy;
    // Pointer elements to the pipes for passing to child (allows NULL)
    int32_t* input_pipe_pass  = NULL;
    int32_t* output_pipe_pass = NULL;
    // Process ID
    pid_t proc_ID;
    // Child error code
    int32_t err_code_child = 0;
    // Variables for use in rerun
    int32_t i, curr_idx, cmd_count;
    // Error value for parent pipes
    int32_t pipe_error = 0;
    // Flag for whether some error was seen
    bool err_flag;

    int offset;
    str_ll* list;

    // Initialize command to NULL
    cmd_str[0] = ASCII_NULL;
    // Initialize cmd_history to null command strings (i.e. no past commands)
    memset(cmd_history, HISTORY_LENGTH * MAX_CMD_LENGTH, ASCII_NULL);

    // Run the shell loop of getting user input and handling it
    while (true) {
        // Get command from user if nothing left to parse
        if ((curr_str == NULL) || (*curr_str == ASCII_NULL)) {
            // Save previous command to history first
            strcpy((char*) cmd_history[cmd_history_idx], (char*) cmd_str);
            cmd_history_idx++;
            if (cmd_history_idx == HISTORY_LENGTH) {
                cmd_history_idx = 0;    // Wrap around when needed
            }
            // Get new command
            print_prompt();
            fgets((char*) cmd_str, MAX_CMD_LENGTH, stdin);
            curr_str = cmd_str;
        }
        
        // Start with no errors seen
        err_flag = false;

        // WHEEEEEEEEEEEEEEEEEEEEEE
        printf("Split function test\n");
        offset = 0;
        list = split(curr_str, &offset);
        while(list != NULL)
        {
            printf("%s\n", (char*) list->str);
            list = list->next;
        }
        printf("Offset: %d\n", offset);

        // Parse command and get location to continue from
        curr_str = parse(curr_str, &cmd);
        // If parsing had an error, inform user and get new input
        if (cmd.error_code != NO_ERROR) {
            printf("Parsing error: %d\n", cmd.error_code);
            curr_str = NULL;
            err_flag = true;
        }
        
        // WHEEEEEEEEEEEEEEEEEEEEEE
        if (!err_flag) {
            printf("Parse function test\n");
            i = 0;
            while (cmd.arg_array[i] != NULL) {
                printf("Arg %d: %s\n", i, (char*) cmd.arg_array[i]);
                i++;
            }
            printf("In file:   %s\n", (char*) cmd.input);
            printf("Out file:  %s\n", (char*) cmd.output);
            printf("Double redir:  %d >& %d\n", cmd.redir_desc1, cmd.redir_desc2);
            printf("Reder first:   %d\n", cmd.redir_desc_first);
            printf("Pipe:    %d\n", cmd.pipe_flag);
            printf("Trun:    %d\n", cmd.trun_flag);
            printf("Bkgd:    %d\n", cmd.bkgd_flag);
            printf("History: %d\n", cmd.history_num);
        }
        
        // Catch rerun command here, errors already parsed out
        if ((!err_flag) && (cmd.history_num != 0)) {
            // Search for command (accounts for partially filled history)
            curr_str = NULL;    // Set to default as not found
            cmd_count = 0;
            curr_idx = cmd_history_idx;
            for (i = 0; i < HISTORY_LENGTH; i++) {
                if (cmd_history[curr_idx][0] != 0) {
                    cmd_count++;
                    printf("%2d: %s", cmd_count, cmd_history[curr_idx]);
                    // Command found
                    if (cmd_count == cmd.history_num) {
                        // Set curr_str to command to rerun and then continue
                        curr_str = cmd_history[curr_idx];
                        break;
                    }
                }
                curr_idx++;
                if (curr_idx == HISTORY_LENGTH) {curr_idx = 0;}
            }
            if (curr_str == NULL) {
                fprintf(stderr, "ERROR: Command requested not in history\n");
            }            
            continue;
        }

        // Set up output pipe if needed
        if ((!err_flag) && (cmd.pipe_flag)) {
            if(pipe(output_pipe) != -1) {
                output_pipe_pass = output_pipe; // Open output pipe if needed
            }
            else { // In case of error openning output pipe, report
                fprintf(stderr, "Parent: failed to setup output pipe\n");
                pipe_error = PARENT_OUTPUT_PIPE_ERROR;
            }
        }
        else if (!err_flag) {
            output_pipe_pass = NULL;
        }
        
        // Check for internal commands and handle them
        if ((!err_flag) && (check_shell_cmd(&cmd))) {
            // Redirect input as needed
            // TODO
            // Execute internal command
            exec_shell_cmd(&cmd, cmd_history, cmd_history_idx);
            // Restore stdin, stdout, and stderr to original
            // TODO
        }
        // Run external commands
        else if (!err_flag) {
            proc_ID = fork();       // Fork off the child process to run command
            if (proc_ID == 0) {     // Call child process function if child
                fprintf(stdout, "Child says hi\n");
                err_code_child = ExecCommand(&cmd, input_pipe_pass, output_pipe_pass);
                // child terminates here, so it returns the error code it has
                fprintf(stdout, "Child: %d\n", err_code_child);
                exit(err_code_child);
            }
            else if (proc_ID > 0) { // Parent code
                fprintf(stdout, "Parent says hi %d\n", err_code_child);
                proc_ID = wait(&err_code_child); // Wait for child executing to terminate
                fprintf(stdout, "Parent testing %d\n", err_code_child);
                WEXITSTATUS(err_code_child);    // Get child return value
                fprintf(stdout, "Parent says bye %d\n", err_code_child);
            }
            else {                      // Fork failed, give error message
                fprintf(stderr, "Forking error: %d\n", proc_ID);
                curr_str = NULL;        // Get new user input
            }
            if (err_code_child != NO_ERROR) {
                fprintf(stderr, "Process error: %d\n", err_code_child);
                curr_str = NULL;        // Get new user input
            }
        }
        
        // Set up pipes for next iteration
        if (input_pipe_pass != NULL) {
            // Close remaining handle
            close(input_pipe[PIPE_READ_SIDE]);
        }
        // Swap pipes
        swap_dummy = output_pipe;
        output_pipe = input_pipe;
        input_pipe = swap_dummy;
        // Reassign input_pipe_pass correctly
        if (output_pipe_pass != NULL) {
            input_pipe_pass = input_pipe;
            close(input_pipe[PIPE_WRITE_SIDE]);
        }
        else {
            input_pipe_pass = NULL;
        }
        
        // If at the end of the string, or n error occured, close all pipes
        if ((curr_str == NULL) || (*curr_str == ASCII_NULL) || err_flag) {
        
            // Print errors if present
            if (err_flag == true) {
                handle_errors(err_code_child);
                handle_errors(pipe_error);
                err_flag = false;
                err_code_child = 0;
                pipe_error = 0;
            }
            
            for (i = 0; i < 2; i++) {
                close(input_pipe[i]);
                close(output_pipe[i]);
            }
        }

        // Close and reset all pipes if error seen or done with command
        ////if ((err_code_child != NO_ERROR) && ())
        
        // Clean up memory allocated
        if (cmd.arg_array != NULL) {
            free(cmd.arg_array);
        }
    }

    // Never actually gets here, would terminate from inside parse_input_cmd
    return 0;
}
