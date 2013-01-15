#ifndef PARSER_H
#define PARSER_H

#include "mysh.h"


typedef struct
{
    struct str_ll*  next;
    uint8_t*        str;
} str_ll;

enum parse_states
{
 //   INIT,           // Initialisation state
    ARG,            // Argument
   /* WHITE_SP,       // White space
    IN_REDIR,       // Redirect input
    DUP_REDIR,      // Duplicate redirection
    OUT_REDIR,      // Redirect output
    OUT_REDIR_APP,  // Redirect output with append
	EXCL,           // Got an exclaimation command
    QUOTE,          // Parsing a quote
    QUOTE_END,      // Finishing parsing a quote
    AMP,            // Got an ampersand (not preceded by '>')
    ERROR,          // Exit with error*/
    EXIT            // Exit with no error
};

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
    AMP_CHAR,
    DUP_REDIR_CHAR, // Duplicate redirection char >&
    NULL_CHAR,      //
    ERROR,
    DONE
};

uint8_t* parse(uint8_t*, cmd_struct*);
str_ll* split(uint8_t*);

#endif
