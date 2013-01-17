/* This file contains the main loop functions to run a command shell.
 *
 * Team: Shir, Steven, Reggie
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "gen.h"
#include "mysh.h"
#include "parser.h"
#include "builtin_cmd.h"
#include "shell_cmd.h"

// Return the shell prompt in the format username:current/directory>
void get_prompt(uint8_t* prompt) {
    // Allocate space for username and current path
    uint8_t curr_path[PATH_MAX]; // Use maximum system path length
    // Copy the username into the prompt
    strcpy((char*) prompt, getlogin());
    getcwd((char*) curr_path, PATH_MAX);

    // Concatenate strings
    strcat((char*) prompt, ":");
    strcat((char*) prompt, (char*) curr_path);
    strcat((char*) prompt, "> ");
}

// This function prints out human readable error messages for any errors that
// occur
void handle_errors(int32_t error_code) {

    switch (error_code) {

        // No error, do nothing
        case 0:
            break;

        case EACCES:
            fprintf(stderr, "Error:%d File access denied.\n", error_code);
            break;

        case EEXIST:
            fprintf(stderr, "Error:%d Path already exists.\n", error_code);
            break;

        case EISDIR:
            fprintf(stderr, "Error:%d Path is not a file.\n", error_code);
            break;

        case EMFILE:
            fprintf(stderr, "Error:%d Maximum number of open files reached.\n",
                    error_code);
            break;

        case ENAMETOOLONG:
            fprintf(stderr, "Error:%d Pathname is too long.\n", error_code);
            break;

        case ENFILE:
            fprintf(stderr, "Error:%d Maximum number of open files reached.\n",
                    error_code);
            break;

        case ENOENT:
            fprintf(stderr, "Error:%d File does not exist.\n", error_code);
            break;

        case ENOSPC:
            fprintf(stderr, "Error:%d Device out of space.\n", error_code);
            break;

        case ENOTDIR:
            fprintf(stderr, "Error:%d Path is not a directory.\n", error_code);
            break;

        case EOVERFLOW:
            fprintf(stderr, "Error:%d File too large.\n", error_code);
            break;

        case EROFS:
            fprintf(stderr, "Error:%d Path is read-only.\n", error_code);
            break;

        case ETXTBSY:
            fprintf(stderr, "Error:%d Requested path busy.\n", error_code);
            break;

        case EBADF:
            fprintf(stderr, "Error:%d Invali file descriptor.\n", error_code);
            break;

        case EINTR:
            fprintf(stderr, "Error:%d Close interrupted.\n", error_code);
            break;

        case EIO:
            fprintf(stderr, "Error:%d I/O error.\n", error_code);
            break;

        case EFAULT:
            fprintf(stderr, "Error:%d Invalid pipe file descriptor.\n",
                    error_code);
            break;

        case EINVAL:
            fprintf(stderr, "Error:%d (pipe2()) Invalid flag value.\n",
                    error_code);
            break;

        case E2BIG:
            fprintf(stderr,
                    "Error:%d Environment and/or argument list too large.\n",
                    error_code);
            break;

        case ECHILD:
            fprintf(stderr, "Error:%d Liar, waitpid() does not work!!!\n",
                    error_code);
            break;

        default:
            fprintf(stderr, "Error:%d Liar, waitpid() does not work!!!\n",
                    error_code);
            break;
    }
    return;
}
// Main function to run shell
int main() {
    // Current command string
    uint8_t* cmd_str = NULL;
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
    int32_t i;
    // Flag for whether some error was seen
    bool err_flag;
    // Entry in the history table
    HIST_ENTRY* hist_entry;
    // Prompt for user
    uint8_t prompt[PATH_MAX];

    // Run the shell loop of getting user input and handling it
    while (true) {
        // Get command from user if nothing left to parse
        if ((curr_str == NULL) || (*curr_str == ASCII_NULL)) {
            // Save previous command to history if it is non-zero
            if(cmd_str != NULL)
            {
                if(strlen((char*) cmd_str) > 0)
                {
                    add_history((char*) cmd_str);
                }
            }

            // Get new command
            get_prompt(prompt);
            cmd_str = (uint8_t*) readline((char*) prompt);
            curr_str = cmd_str;
        }

        // Start with no errors seen
        err_flag = false;

        // Parse command and get location to continue from
        curr_str = parse(curr_str, &cmd);
        // If parsing had an error, inform user and get new input
        if (cmd.error_code != NO_ERROR) {
            fprintf(stderr, "Parsing error: %d\n", cmd.error_code);
            curr_str = NULL;
            err_flag = true;
        }

        // Check for null command, and ensure that its not a ! command which
        // will have no arg_array
        if (cmd.arg_array[0] == NULL && cmd.history_num == 0) {
            curr_str = NULL;
            continue;
        }

        // Catch rerun command here, errors already parsed out
        if ((!err_flag) && (cmd.history_num != 0)) {
            // Search for command (accounts for partially filled history)
            hist_entry = history_get(cmd.history_num + history_base - 1);
            curr_str = hist_entry == NULL ? NULL : (uint8_t*) hist_entry->line;

            if (curr_str == NULL) {
                fprintf(stderr, "ERROR: Command requested not in history\n");
                err_flag = true;
            }
            continue;
        }

        // Set up output pipe if needed
        if ((!err_flag) && (cmd.pipe_flag)) {
            if(pipe(output_pipe) != -1) {
                output_pipe_pass = output_pipe; // Open output pipe if needed
            }
            else { // In case of error openning output pipe, report
                fprintf(stderr, "ERROR: Parent: failed to setup output pipe\n");
                err_flag = true;
            }
        }
        else if (!err_flag) {
            output_pipe_pass = NULL;
        }

        // Check for internal commands and handle them
        // Ignores redirects
        if ((!err_flag) && (check_shell_cmd(&cmd))) {
            // Execute internal command
            exec_shell_cmd(&cmd);
        }
        // Run external commands
        else if (!err_flag) {
            proc_ID = fork();       // Fork off the child process to run command
            if (proc_ID == 0) {     // Call child process function if child
                err_code_child = ExecCommand(&cmd, input_pipe_pass,
                                             output_pipe_pass);
                // child terminates here, so it returns the error code it has
                if (err_code_child != 0) {
                    err_flag = true;
                }
                exit(err_code_child);
            }
            else if (proc_ID > 0) { // Parent code
                // Wait for child executing to terminate
                proc_ID = wait(&err_code_child);
                WEXITSTATUS(err_code_child);    // Get child return value
                if (err_code_child != 0) {
                    err_flag = true;
                }
            }
            else {                      // Fork failed, give error message
                fprintf(stderr, "ERROR: Forking: %d\n", proc_ID);
                err_flag = true;
                curr_str = NULL;        // Get new user input
            }
            if (err_code_child != NO_ERROR) {
                err_flag = true;
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
                err_flag = false;
                err_code_child = 0;
                curr_str = NULL;
            }

            for (i = 0; i < 2; i++) {
                if (input_pipe[i] != NULL) {
                    close(input_pipe[i]);
                }
                if (output_pipe[i] != NULL) {
                    close(output_pipe[i]);
                }
            }
        }

        // Clean up memory allocated
        if (cmd.arg_array != NULL) {
            free(cmd.arg_array);
        }
    }

    // Never actually gets here, would terminate from inside parse_input_cmd
    return 0;
}
