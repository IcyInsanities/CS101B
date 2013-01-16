#ifndef MYSH_H
#define MYSH_H

#include <stdbool.h>
#include <stdint.h>

// This struct holds all the details from a command necessary for executing that
// the parser determines
typedef struct {
    uint8_t**   arg_array;          // Array of arguments
    uint8_t*    input;              // input filename, NULL defaults for stdin
    uint8_t*    output;             // ouptut filename, NULL defaults for stdout
    uint8_t     redir_desc1;        // File descriptor 1 for >&, defaults 0
    uint8_t     redir_desc2;        // File descriptor 2 for >&, defaults 0
    bool        redir_desc_first;   // Determines if >& or > was first
    bool        pipe_flag;          // Determines if output was piped, def false
    bool        trun_flag;          // Determines if >> was given, default true
    bool        bkgd_flag;          // Determines if run in background, def false
    uint8_t     history_num;        // History number for ! command, default 0
    uint8_t     error_code;         // Error code from parser, default 0
} cmd_struct;

#endif
