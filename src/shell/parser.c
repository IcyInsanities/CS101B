#include "parser.h"
#include "mysh.h"
#include "gen.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t* parse(uint8_t* argv, cmd_struct* cmd)
{
    uint32_t            offset;             // Offset within the input string
    uint32_t            arg_num;            // Current argument number
    enum parse_states   state;              // Current state in the FSM
    uint8_t**           split_cmd;          // Command split into an array
    
    // Default to NULL input and output 
    cmd->input = NULL;
    cmd->output = NULL;

    offset = 0;
    arg_num = 0;
    state = INIT;
   /* 
    while(state != EXIT && state != ERROR)
    {
        switch(state)
        {
            case INIT:
                
                break;
            case ARG:
                break;
            case WHITE_SP:
                break;
            case IN_REDIR:
                break;
            case OUT_REDIR:
                break;
            case EXCL:
                break;
            case QUOTE:
                // Check for the end of the quote
                if(argv[offset] == '\"')
                {
                    // Go into a state to terminate the quotation sequence
                    state = QUOTE_END;
                }
                // Still inside the quote
                else
                {
                    // Copy the character to the temporary buffer
                }
                break;

            case QUOTE_END:
                if(argv[offset])
                {
                    
                }
                break;

            default:
                break;

            offset ++;
        }
    }*/
}

str_ll* split(uint8_t* cmd)
{
    enum split_states   state;              // FSM for splitting command
    uint32_t            offset;             // Offset within the command string
    uint8_t             temp[MAX_LENGTH];   // Temp buffer transcribe string
    uint32_t            temp_off;           // Offset within temp
    uint8_t*            null_end;           // NULL termination of array
    str_ll*             split_list;         // Linked list of arguments
    str_ll*             cur_elem;           // Current element in the list

    // Allocate and set the null terminator
    null_end = (uint8_t*)malloc(sizeof(uint8_t));
    null_end[0] = ASCII_NULL;

    // Allocate first element of linked list and set to null
    split_list = (str_ll*)malloc(sizeof(uint8_t));
    split_list->next = NULL;
    split_list->str = null_end;

    // Start working on the first element
    cur_elem = split_list;

    state = INIT;   // Start in the initial state
    offset = 0;     
    temp_off = 0;

    while(state != ERROR && state != DONE)
    {
        switch(state)
        {
            case INIT:
                switch(cmd[offset])
                {
                    // Catch all invalid starting characters
                    case ASCII_NULL:
                    case '|':
                    case '&':
                    case '>':
                    case '<':
                        state = ERROR;      // Generate an error
                        break;
                    case '!':
                        state = EXC_CHAR;   // Parse the '!' command
                        break;
                    case '\"':
                        state = QUOTE;      // Handle the quotation
                        break;
                    // Ignore whitespace at the start
                    case ' ':
                        break;
                    // All other characters are valid
                    default:
                        state = NORM_CHAR;  // Received a normal character
                        // Store the character in the temp buffer
                        temp[temp_off] = cmd[offset];
                        temp_off ++;
                }
                break;
            case NORM_CHAR:
                switch(cmd[offset])
                {
                    case ASCII_NULL:
                        state = NULL_CHAR;
                        break;
                    case '|':
                        state = PIPE_CHAR;
                        break;
                    case '&':
                        state = AMP_CHAR;
                        break;
                    case '<':
                        state = LT_CHAR;
                        break;
                    case '>':
                        state = GT_CHAR;
                        break;
                    case '!':
                        state = ERROR;      // Cannot have stray !
                        break;
                    case '\"':
                        state = QUOTE;
                        break;
                    // Handle whitespace
                    case ' ':
                        state = WHITE_SP;
                        break;
                    // All other characters are valid
                    default:
                        state = NORM_CHAR;
                }
                break;
            case WHITE_SP:
                // Transcribe the string after we remove the whitespace
                if(cmd[offset] != ' ')
                {
                    // NULL terminate str
                    temp[temp_off + 1] = ASCII_NULL; 
                    // Null terminate list
                    temp_off = 0;

                    // Add the element in
                    cur_elem = append(cur_elem, temp, null_end);
                }
                switch(cmd[offset])
                {
                    case '|':
                        state = PIPE_CHAR;
                        break;
                    case '&':
                        state = AMP_CHAR;
                        break;
                    case '>':
                        state = GT_CHAR;
                        break;
                    case '<':
                        state = LT_CHAR;
                        break;
                    // We cannot have floating !'s
                    case '!':
                        state = ERROR;
                        break;
                    case '\"':
                        state = QUOTE;
                        break;
                    // Ignore additional whitespace
                    case ' ':
                        state = WHITE-SP;
                        break;
                    default:
                        state = NORM_CHAR;
                }
                break;
            case QUOTE:
                switch(cmd[offset])
                {
                    // Only way to break out of QUOTE is with another '\"' char
                    case '\"':
                        state = QUOTE_END;
                        break;
                    // Cannot terminate in the middle of a quote
                    case ASCII_NULL:
                        state = ERROR;
                        break;
                    // If still in the quote, just transcribe the character
                    default:
                        temp[temp_off] = cmd[offset];
                        temp_off ++;
                }
                break;
            case QUOTE_END:
                switch(cmd[offset])
                {
                    case ASCII_NULL:
                        state = NULL_CHAR;
                        break;
                    case '|':
                        state = PIPE_CHAR;
                        break;
                    case '&':
                        state = AMP_CHAR;
                        break;
                    case '>':
                        state = GT_CHAR;
                        break;
                    case '<':
                        state = LT_CHAR;
                        break;
                    // Cannot have stray !
                    case '!':
                        state = ERROR;
                        break;
                    case ' ':
                        state = WHITE_SP;
                        break;
                    default:
                        state = NORM_CHAR;
                }
                break;
            case LT_CHAR:
                
                break;
            case GT_CHAR:
                break;
            case PIPE_CHAR:
                break;
            case EXC_CHAR:
                break;
            case AMP_CHAR:
                break;
            case DUP_REDIR_CHAR:
                break;
            case NULL_CHAR:
                break;
            case ERROR:
                break;
            case DONE:
                break;
            default:
        }
        offset ++;
    }
    
    // Indicate an error by returning NULL
    if(state == ERROR)
    {
        split_list = NULL;
    }

    return split_list;
}

str_ll* append(str_ll* cur_end, uint8_t* new_val, uint8_t* nul_val)
{
    // Put string in list
    cur_end->str = strdup(temp);
    // Add a new element to the list
    cur_end->next = (str_ll*)malloc(sizeof(str_ll));
    // Null terminate list
    cur_end->next->str = null_val;

    return cur_end->next;
}

