#include <stdint.h>
#include "mysh.h"

// Executes command based on passed command struct
int32_t ExecCommand(cmd_struct *cmd, int32_t *inputPipe, int32_t *outputPipe);

