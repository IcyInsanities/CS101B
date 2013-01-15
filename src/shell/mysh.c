/* This file contains the main loop functions to run a command shell.
 *
 * Team: Shir, Steven, Reggie
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

// Define maximum command length, and how large a history to save
#define HISTORY_LENGTH   100
#define MAX_CMD_LENGTH  1024

// Print the shell prompt in the format username:current/directory> 
void print_prompt() {
    // Allocate space for username and current path
    int8_t* username;
    int8_t curr_path[PATH_MAX]; // Use maximum system path length
    
    // System calls to obtain username and path
    username = (int8_t*) getlogin();
    getcwd( (char*) curr_path, PATH_MAX);
    
    // Print out prompt and return1
    printf("%s:%s> ", username, curr_path);
    return;
}

// This function prints out the shell history
void print_history(int8_t cmd_history[HISTORY_LENGTH][MAX_CMD_LENGTH],
                   int32_t cmd_history_idx) {
    int i, curr_idx = cmd_history_idx;
    for (i = 0; i < HISTORY_LENGTH; i++) {
        if (cmd_history[curr_idx][0] != 0) {
            printf("  %s", cmd_history[curr_idx]);
        }
        curr_idx++;
        if (curr_idx == HISTORY_LENGTH) {curr_idx = 0;}
    }
}

void parse_input_cmd(int8_t* cmd_str,
                     int8_t cmd_history[HISTORY_LENGTH][MAX_CMD_LENGTH],
                     int32_t cmd_history_idx) {
    if (cmd_str[0] == 'e') {
        exit(0);
    }
    if (cmd_str[0] == 'h') {
        print_history(cmd_history, cmd_history_idx);
    }
    return;
}

// Main function to run shell
int main() {
    // Allocate a history buffer
    int8_t cmd_history[HISTORY_LENGTH][MAX_CMD_LENGTH];
    // Index to the current location in the command history for cicular array
    int32_t cmd_history_idx = 0;
    // Current command string
    int8_t cmd_str[MAX_CMD_LENGTH];

    // Initialize cmd_history to null command strings (i.e. no past commands)
    memset(cmd_history, HISTORY_LENGTH * MAX_CMD_LENGTH, 0);

    // Run the shell loop of getting user input and handling it
    while (true) {
        print_prompt();
        fgets((char*) cmd_str, MAX_CMD_LENGTH, stdin);
        parse_input_cmd(cmd_str, cmd_history, cmd_history_idx);
        // cmd_str from fgets so safe to use strcpy
        strcpy((char*) cmd_history[cmd_history_idx], (char*) cmd_str);
        cmd_history_idx++;
    }

    // Never actually gets here, would terminate from inside parse_input_cmd
    return 0;
}
