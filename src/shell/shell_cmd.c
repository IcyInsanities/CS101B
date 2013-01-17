#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "mysh.h"
#include "gen.h"
#include "shell_cmd.h"
////#include "builtin_cmd.h"


////#include <sys/types.h>
////#include <sys/stat.h>
////#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>

// This function prints out the shell history
void print_history() {
    int32_t i;                          // Loop variable
    HIST_ENTRY* hist_entry;             // Pointer to history construct
    
    i = where_history;

    while(i >= history_base)
    {
        hist_entry = history_get(i);
        printf("%2d: %s", i - history_base + 1, hist_entry->line);
        i --;
    }
}

int32_t change_dir(uint8_t* new_dir) {
    // Hold the error code if any
    int32_t error_code;
    
    // Buffer to hold default path
    int8_t home_dir[PATH_MAX];
    home_dir[0] = ASCII_NULL;
    
    // Switch to home directory if no argument
    if (new_dir == NULL) {
        strcat((char*) home_dir, "/home/");
        strcat((char*) home_dir, getlogin());
        error_code = chdir((char*) home_dir);
    }
    else {
        error_code = chdir((char*) new_dir);
    }
    
    // If chdir encountered an error, return the error value 
    if (error_code == -1) {
        return errno;
    }
    return NO_ERROR;
}


// Checks if the command given is a shell command
bool check_shell_cmd(cmd_struct *cmd) {
    // Check if the first argument is an internal shell command and return true
    if ((strcmp((char*) cmd->arg_array[0], "exit")    == 0) ||
        (strcmp((char*) cmd->arg_array[0], "history") == 0) ||
        (strcmp((char*) cmd->arg_array[0], "cd")      == 0) ||
        (strcmp((char*) cmd->arg_array[0], "chdir")   == 0) ) {
        return true;
    }
    return false;
}

// Executes command based on passed command struct which is an internal shell
// command
int32_t exec_shell_cmd(cmd_struct *cmd) {
    // Got exit command
    if (strcmp((char*) cmd->arg_array[0], "exit") == 0) {
        exit(0);
    }
    // Got history command
    else if (strcmp((char*) cmd->arg_array[0], "history") == 0) {
        if (cmd->arg_array[1] != NULL) {
            fprintf(stderr, "Too many arguments to history\n");
            return ERROR;
        }
        print_history();
        return NO_ERROR;
    }
    // Got rerun command, reset execution to that command
    else if ((strcmp((char*) cmd->arg_array[0], "cd")    == 0) ||
             (strcmp((char*) cmd->arg_array[0], "chdir") == 0)) {
        if ((cmd->arg_array[1] != NULL) && (cmd->arg_array[2] != NULL)) {
            fprintf(stderr, "Too many arguments to cd or chdir\n");
            return ERROR;
        }
        // Change directory and return any errors from that
        return change_dir(cmd->arg_array[1]);
    }
    // Got internal command by accident
    return ERROR;
}
