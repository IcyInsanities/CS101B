#ifndef SHELL_CMD_H
#define SHELL_CMD_H

#include <stdint.h>
#include "mysh.h"


// Checks if the command given is a shell command
bool check_shell_cmd(cmd_struct *cmd);

// Executes command based on passed command struct which is an internal shell
// command
int32_t exec_shell_cmd(cmd_struct *cmd, 
                       uint8_t cmd_history[HISTORY_LENGTH][MAX_CMD_LENGTH],
                       int32_t cmd_history_idx);

#endif  // SHELL_CMD_H
