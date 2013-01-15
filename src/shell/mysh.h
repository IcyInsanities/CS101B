#ifndef MYSH_H
#define MYSH_H

/* // Implement booleans for convenience
#define TRUE    1
#define FALSE   0
typedef bool uint8_t; */
#include <stdbool.h>
#include <stdint.h>

/*
- array of arguments
- stdin filename
    '' for default STDIO
- stdout filename
    '' for default STDOUT
- file descriptor 1
    0 for default none
- file descriptor 2
    0 for default none
- pipe flag (for output)
- truncation flag (handles >>)
    0 for default
    1 for truncate
- background flag
    0 for default
    1 for run on background
- history number
    0 for none ! command
- error flag
*/
typedef struct {
    uint8_t**   arg_array;
    uint8_t*    input;
    uint8_t*    output;
    bool        reder_desc1;
    bool        reder_desc2;
    bool        pipe_flag;
    bool        trun_flag;
    bool        bkgd_flag;
    uint8_t     history_num;
    uint8_t     error_code;
} cmd_struct;

#endif
