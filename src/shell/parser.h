#ifndef PARSER_H
#define PARSER_H

#include "mysh.h"


typedef struct
{
    struct str_ll*  next;
    uint8_t*        str;
} str_ll;

enum split_states
{
    INIT,           // Initial state
    NORM_CHAR,      // Normal character
    WHITE_SP,       // White space
    QUOTE,          // Quotation received
    QUOTE_END,      // End of quotation
    LT_CHAR,
    GT_CHAR,
    PIPE_CHAR,
    EXC_CHAR,
    EXC_NUM,        // Number after ! character 
    AMP_CHAR,
    DUP_REDIR_CHAR, // Duplicate redirection char >&
    APPEND_CHAR,    // Append character >>
    ERROR_STATE,
    DONE
};

uint8_t* parse(uint8_t*, cmd_struct*);
str_ll* split(uint8_t*, uint32_t*);
str_ll* append(str_ll*, uint8_t*, uint8_t*);

#endif
