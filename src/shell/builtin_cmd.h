#ifndef BUILTIN_CMD_H
#define BUILTIN_CMD_H

#include <stdint.h>
#include "mysh.h"

#define PIPE_READ_SIDE  0
#define PIPE_WRITE_SIDE 1

// Executes command based on passed command struct
int32_t ExecCommand(cmd_struct *cmd, int32_t *inputPipe, int32_t *outputPipe);

#endif
