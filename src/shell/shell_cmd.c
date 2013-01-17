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
//#include <errno.h>

// This function prints out the shell history
void print_history(uint8_t cmd_history[HISTORY_LENGTH][MAX_CMD_LENGTH],
                   int32_t cmd_history_idx) {
    int32_t i;                           // Loop variable
    int32_t curr_idx = cmd_history_idx;  // Index of current command in history
    int32_t cmd_count = 1;               // Count of commands printed
    for (i = 0; i < HISTORY_LENGTH; i++) {
        if (cmd_history[curr_idx][0] != 0) {
            printf("%2d: %s", cmd_count, cmd_history[curr_idx]);
            cmd_count++;
        }
        curr_idx++;
        if (curr_idx == HISTORY_LENGTH) {curr_idx = 0;}
    }
}

int32_t change_dir(uint8_t* new_dir) {
    int8_t home_dir[PATH_MAX];
    home_dir[0] = ASCII_NULL;
    // Switch to home directory if no argument
    if (new_dir == NULL) {
        strcat((char*) home_dir, "/home/");
        strcat((char*) home_dir, getlogin());
        chdir((char*) home_dir);
    }
    else {
        chdir((char*) new_dir);
    }
    return 0;
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
int32_t exec_shell_cmd(cmd_struct *cmd,
                       uint8_t cmd_history[HISTORY_LENGTH][MAX_CMD_LENGTH],
                       int32_t cmd_history_idx) {
    // Got exit command
    if (strcmp((char*) cmd->arg_array[0], "exit") == 0) {
        exit(0);
    }
    // Got history command
    else if (strcmp((char*) cmd->arg_array[0], "history") == 0) {
        print_history(cmd_history, cmd_history_idx);
        return 0;
    }
    // Got rerun command, reset execution to that command
    else if ((strcmp((char*) cmd->arg_array[0], "cd")    == 0) ||
             (strcmp((char*) cmd->arg_array[0], "chdir") == 0)) {
        change_dir(cmd->arg_array[1]);
        return 0;
    }
    // Got internal command by accident
    return ERROR;
}