#include "parser.h"
#include "mysh.h"
#include "gen.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t* parse(uint8_t* argv, cmd_struct* cmd)
{
    uint32_t            offset;             // Offset within the input string
    uint32_t            arg_num;            // Current argument number
    uint8_t**           split_cmd;          // Command split into an array
    
    // Default to NULL input and output 
    cmd->input = NULL;
    cmd->output = NULL;

    offset = 0;
    arg_num = 0;
}

str_ll* split(uint8_t* cmd, uint32_t* offset)
{
    enum split_states   state;              // FSM for splitting command
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

    while(state != ERROR_STATE && state != DONE)
    {
        switch(state)
        {
            case INIT:
                switch(cmd[*offset])
                {
                    // Catch all invalid starting characters
                    case ASCII_NULL:
                    case '|':
                    case '&':
                        state = ERROR_STATE;      // Generate an error
                        break;
                    case '>':
                        state = GT_CHAR;
                        break;
                    case '<':
                        state = LT_CHAR;
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
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                }
                break;
            case NORM_CHAR:
                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                        state = DONE;
                        break;
                    case '|':
                        // Add string to list first
                        temp[temp_off + 1] = ASCII_NULL; 
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = PIPE_CHAR;
                        break;
                    case '&':
                        // Add string to list first
                        temp[temp_off + 1] = ASCII_NULL; 
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = AMP_CHAR;
                        break;
                    case '<':
                        // Add string to list first
                        temp[temp_off + 1] = ASCII_NULL; 
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = LT_CHAR;
                        break;
                    case '>':
                        // Add string to list first
                        temp[temp_off + 1] = ASCII_NULL; 
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = GT_CHAR;
                        break;
                    case '!':
                        state = ERROR_STATE;      // Cannot have stray !
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
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;

                }
                break;
            case WHITE_SP:
                // Transcribe the string after we remove the whitespace
                if(cmd[*offset] != ' ')
                {
                    // NULL terminate str
                    temp[temp_off + 1] = ASCII_NULL; 
                    // Null terminate list
                    temp_off = 0;

                    // Add the element in
                    cur_elem = append(cur_elem, temp, null_end);
                }
                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                        state = DONE;
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
                    // We cannot have floating !'s
                    case '!':
                        state = ERROR_STATE;
                        break;
                    case '\"':
                        state = QUOTE;
                        break;
                    // Ignore additional whitespace
                    case ' ':
                        state = WHITE_SP;
                        break;
                    default:
                        state = NORM_CHAR;
                }
                break;
            case QUOTE:
                switch(cmd[*offset])
                {
                    // Only way to break out of QUOTE is with another '\"' char
                    case '\"':
                        state = QUOTE_END;
                        break;
                    // Cannot terminate in the middle of a quote
                    case ASCII_NULL:
                        state = ERROR_STATE;
                        break;
                    // If still in the quote, just transcribe the character
                    default:
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                }
                break;
            case QUOTE_END:
                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                        state = DONE;
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
                        state = ERROR_STATE;
                        break;
                    case ' ':
                        state = WHITE_SP;
                        break;
                    default:
                        state = NORM_CHAR;
                }
                break;
            case LT_CHAR:
                // Load a single '<' into the buffer
                temp[0] = '<';
                temp[1] = ASCII_NULL;

                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                    case '|':
                    case '&':
                    case '<':
                    case '>':
                        state = ERROR_STATE;
                        break;
                    case '\"':
                        // Add string to list when leaving state
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = QUOTE;
                        break;
                    case ' ':
                        // Eat whitespace by staying in this state
                        state = LT_CHAR;
                        break;
                    default:
                        // Add string to list when leaving state
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                        state = NORM_CHAR;
                }
                break;
            case GT_CHAR:
                // Load buffer with a single '>'
                temp[0] = '>';
                temp[1] = ASCII_NULL;

                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                    case '|':
                    case '<':
                    case '!':
                        state = ERROR_STATE;
                        break;
                    case '&':
                        state = DUP_REDIR_CHAR;
                        break;
                    case '>':
                        state = APPEND_CHAR;
                        break;
                    case '\"':
                        // Add string to list when leaving state
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = QUOTE;
                        break;
                    case ' ':
                        // Eat away white space
                        state = GT_CHAR;
                        break;
                    default:
                        // Add string to list when leaving state
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);
                        
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                        state = NORM_CHAR;
                }
                break;
            case PIPE_CHAR:
                // Load buffer with a single '|'
                temp[0] = '|';
                temp[1] = ASCII_NULL;

                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                    // Throw error on ||
                    case '|':
                    case '&':
                    case '>':
                    case '<':
                        state = ERROR_STATE;
                        break;
                    default:
                        state = DONE;                       
                }
                break;
            case EXC_CHAR:
                // Load a single ! in the buffer
                temp[0] = '!';
                temp[1] = ASCII_NULL;
                switch(cmd[*offset])
                {
                    case '0':
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9':
                        // Add string to list when leaving state
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);
                        
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                        state = EXC_NUM;
                        break;
                    default:
                        state = ERROR_STATE;
                }
            case EXC_NUM:
                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                        // Add string to list when leaving state
                        temp[temp_off + 1] = ASCII_NULL;
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = DONE;
                        break;
                    case '0':
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9':
                        // Put character in the temp array
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                        state = EXC_NUM;
                        break;
                    default:
                        // Can only have numbers
                        state = ERROR;
                }
            case AMP_CHAR:
                // Load a single & in the buffer
                temp[0] = '&';
                temp[1] = ASCII_NULL;

                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                        // Add to the list before we terminate
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);
                        state = DONE;
                        break;
                    case ' ':
                        // Eat up white space
                        state = AMP_CHAR;
                        break;
                    default:
                        state = ERROR_STATE;
                }
                break;
            case DUP_REDIR_CHAR:
                // Set buffer to >& (the > is from the GT_CHAR)
                temp[1] = '&';
                temp[2] = ASCII_NULL;

                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                    case '|':
                    case '&':
                    case '>':
                    case '<':
                    case '!':
                        state = ERROR_STATE;
                        break;
                    case '\"':
                        // Add to the list before we terminate
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = QUOTE;
                        break;
                    case ' ':
                        // Add to the list before we terminate
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = WHITE_SP;
                        break;
                    default:
                        // Add to the list before we terminate
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = NORM_CHAR;
                }
                break;
            case APPEND_CHAR:
                // Set buffer to >> (the first > is from GT_CHAR)
                temp[1] = '>';
                temp[2] = ASCII_NULL;

                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                    case '|':
                    case '&':
                    case '>':
                    case '<':
                    case '!':
                        state = ERROR_STATE;
                        break;
                    case '\"':
                        // Add to the list before we terminate
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = QUOTE;
                        break;
                    case ' ':
                        // Add to the list before we terminate
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = APPEND_CHAR;
                        break;
                    default:
                        // Add to the list before we terminate
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = NORM_CHAR;
                }
                break;
            case ERROR_STATE:
                // Do nothing for error
                break;
            case DONE:
                // Do nothing when done
                break;
        }
        // Don't increment on the last iteration
        if(state != DONE && state != ERROR_STATE)
        {
            *offset ++;
        }
    }
    
    // Indicate an error by returning NULL
    if(state == ERROR_STATE)
    {
        // First we free the linked list
        while(split_list != NULL)
        {
            cur_elem = split_list->next;
            free(split_list);
            split_list = cur_elem;
        }
    }

    return split_list;
}

str_ll* append(str_ll* cur_end, uint8_t* new_val, uint8_t* null_val)
{
    str_ll* next_end;
    // Put string in list
    cur_end->str = strdup(new_val);
    // Add a new element to the list
    cur_end->next = (str_ll*)malloc(sizeof(str_ll));
    // Null terminate list
    next_end = cur_end->next;
    next_end->str = null_val;

    return next_end;
}

