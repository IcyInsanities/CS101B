/* This file contains the header for running the parser and splitter functions
 *
 * Team: Shir, Steven, Reggie
*/

#ifndef PARSER_H
#define PARSER_H

#include "mysh.h"

// Linked list structure to hold the token strings from the splitter
typedef struct {
    struct str_ll*  next;
    uint8_t*        str;
} str_ll;

// States for the finite state machine
enum split_states {
    INIT,           // Initial state
    NORM_CHAR,      // Normal character
    WHITE_SP,       // White space
    QUOTE,          // Quotation received
    QUOTE_END,      // End of quotation
    LT_CHAR,        // <
    GT_CHAR,        // >
    GT_WHITE,       // Whitespace after a >
    PIPE_CHAR,      // Pipe
    EXC_CHAR,       // ! character
    EXC_NUM,        // Number after ! character
    AMP_CHAR,       // Single & (not >&)
    DUP_REDIR_CHAR, // Duplicate redirection char >&
    APPEND_CHAR,    // Append character >>
    ERROR_STATE,    // Encountered an error
    DONE            // Finished with no error
};

// Parse the command string into a cmd_struct
uint8_t* parse(uint8_t*, cmd_struct*);
// Split the input command string by tokens
str_ll* split(uint8_t*, uint32_t*);
// Add an element to the linked list
str_ll* append(str_ll*, uint8_t*, uint8_t*);

#endif
