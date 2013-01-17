#include "parser.h"
#include "mysh.h"
#include "gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// test code before integration, TO BE DELETED!!!
//
//  PROBLEM STRINGS
//      a > > b.txt     - eats whitespace
//      a > > > b.txt   - core dumps
//      whitespace extra?????????
//
// int main()
// {
//     //uint8_t test_str[] = "test < blah.c >& fuckit > lks\"l !ks\"ksj\0";
//     //uint8_t test_str[] = "this < is <a >test 1>&2 \"of <>!&Reggie's code\"  & ";
//     uint8_t test_str[] = "I want < a.txt >> working > shell.txt 1>&1 > corr.txt";
//     uint8_t* new_str;
//     uint32_t offset;
//     cmd_struct cmd;
//     str_ll* list;
//     list = NULL;
//     offset = 0;
//     uint32_t i;
//
//     // Test split function
//     printf("Split function test\n");
//     list = split(test_str, &offset);
//     while(list != NULL)
//     {
//         printf("%s\n", (char*) list->str);
//         list = list->next;
//     }
//     printf("Offset: %d\n", offset);
//
//     // Test parse function
//     printf("Parse function test\n");
//     printf("Orig: %s\n", (char*) test_str);
//     new_str = parse(test_str, &cmd);
//     printf("New:  %s\n", (char*) new_str);
//     if (cmd.error_code == NO_ERROR) {
//         i = 0;
//         while (cmd.arg_array[i] != NULL) {
//             printf("  %s\n", (char*) cmd.arg_array[i]);
//             i++;
//         }
//         printf("In file:   %s\n", (char*) cmd.input);
//         printf("Out file:  %s\n", (char*) cmd.output);
//         printf("Double redir:  %d >& %d\n", cmd.redir_desc1, cmd.redir_desc2);
//         printf("Reder first:   %d\n", cmd.redir_desc_first);
//         printf("Pipe:    %d\n", cmd.pipe_flag);
//         printf("Trun:    %d\n", cmd.trun_flag);
//         printf("Bkgd:    %d\n", cmd.bkgd_flag);
//         printf("History: %d\n", cmd.history_num);
//     }
//     else {
//         printf("Error: %d\n", cmd.error_code);
//     }
//
//     return 0;
// }

// This function takes a command string and fills in a struct with all values
// needed for execution. It returns a pointer to where parsing ended, with a
// pipe or end of string indicated a command has ended.
uint8_t* parse(uint8_t* cmd_str, cmd_struct* cmd) {
    uint32_t            offset;             // Offset within the input string
    uint32_t            arg_num;            // Current argument number
    str_ll*             split_list;         // Linked list of arguments
    str_ll*             cur_elem;           // Current element in the list
    str_ll*             prev_elem;          // Previous element in the list
    uint32_t            i;                  // Index

    // Set command struct to defaults
    cmd->input = NULL;          // No input redirection
    cmd->output = NULL;         // No output redirection
    cmd->redir_desc1 = 0;       // No descriptors given from >&
    cmd->redir_desc2 = 0;       // No descriptors given from >&
    cmd->pipe_flag = false;     // Output is not piped
    cmd->trun_flag = true;      // Output is truncated
    cmd->bkgd_flag = false;     // Output is not run in background
    cmd->history_num = 0;       // No history command given
    cmd->error_code = NO_ERROR; // Start with no errors

    // Parse the command into separate arguments in the form of a linked list
    offset = 0;
    split_list = split(cmd_str, &offset);
    if (split_list == NULL) {
        cmd->error_code = ERROR;    // Splitter encountered an error
        offset = 0;                 // Reset to start of string on error
    }

    // Parse redirects out of linked list
    // Any nodes used up are deleted by marking as NULL strings, which prevents
    // the descriptors from being taken from a previous argument when it is
    // should be an error in the command (i.e. a.txt > b.txt >& 2)
    arg_num = 0;
    cur_elem = split_list;
    while (cur_elem != NULL) {
        // Check if input redirection given
        if (cur_elem->str[0] == '<') {
            cur_elem->str = NULL;        // Delete this node
            cur_elem = cur_elem->next;   // Move to next element
            if (cur_elem == NULL) {
                cmd->error_code = ERROR;
                break;
            }
            cmd->input = cur_elem->str;  // Assign filename to input
            cur_elem->str = NULL;        // Delete this node
        }
        else if (cur_elem->str[0] == '>') {
            // Check if redirection descriptors given
            if (cur_elem->str[1] == '&') {
                // If output redirection was seen, it was first
                cmd->redir_desc_first = false;
                // Decrement argument count which is no longer an argument
                arg_num--;
                // Previous element is the first descriptors, check that not
                // already used
                if (prev_elem->str == NULL) {
                    cmd->error_code = ERROR;
                    break;
                }
                cmd->redir_desc1 = atoi((char*) prev_elem->str);
                prev_elem->str = NULL;   // Delete previous
                // Next element is the second descriptors
                cur_elem->str = NULL;        // Delete this node
                cur_elem = cur_elem->next;   // Move to next element
                if (cur_elem == NULL) {
                    cmd->error_code = ERROR;
                    break;
                }
                cmd->redir_desc2 = atoi((char*) cur_elem->str);
                cur_elem->str = NULL;       // Delete this node
            }
            // Output redirection given
            else {
                // Check if truncation
                if (cur_elem->str[1] == ASCII_NULL) {
                    cmd->trun_flag = true;
                }
                // Otherwise appending output (>> given)
                else {
                    cmd->trun_flag = false;
                }
                // Reassign output
                cur_elem->str = NULL;         // Delete this node
                cur_elem = cur_elem->next;    // Move to next element
                if (cur_elem == NULL) {
                    cmd->error_code = ERROR;
                    break;
                }
                cmd->output = cur_elem->str;  // Assign filename to output
                cur_elem->str = NULL;         // Delete this node
                // If descriptors were seen, they were first
                cmd->redir_desc_first = true;
            }
        }
        // Check for & or |, must be last from splitting function
        else if (cur_elem->str[0] == '&') {
            cmd->bkgd_flag = true;
            cur_elem->str = NULL;
        }
        else if (cur_elem->str[0] == '|') {
            cmd->pipe_flag = true;
            cur_elem->str = NULL;
        }
        // Current element is an argument, increment count
        else {
            arg_num++;
        }
        prev_elem = cur_elem;       // Move to next element
        cur_elem = cur_elem->next;

    }

    // Check if command was a ! command for history
    // split function already handled extra arguments
    if ((cmd->error_code == NO_ERROR) && (split_list->str[0] == '!')) {
        arg_num = 1;    // No arguments to pass here (1 accounts for decrement)
        cur_elem = split_list->next;
        cmd->history_num = atoi((char*) cur_elem->str);
        split_list->str = NULL; // Mark as deleted
        cur_elem->str = NULL;
    }

    // Create array of arguments from the command passed in
    if (cmd->error_code == NO_ERROR) {
        // Allocate space for arguments
        //arg_num--;  // Remove whitespace extra element
        cmd->arg_array = (uint8_t*)malloc(sizeof(uint8_t*) * arg_num);
        i = 0;
        cur_elem = split_list;
        while ((i < arg_num - 1) && (cur_elem != NULL)) {
            if (cur_elem->str != NULL) { // Check that not marked as deleted
                cmd->arg_array[i] = cur_elem->str;
                i++;    // Move to next array argument spot
            }
            cur_elem = cur_elem->next;
        }
        cmd->arg_array[arg_num-1] = NULL;
    }

    // Clean up the linked list
    while (split_list != NULL) {
        cur_elem = split_list;
        split_list = split_list->next;
        free(cur_elem);
    }

    // Return pointer to where parsing started
    return cmd_str + offset;
}

// This function takes a command string and parses, stopping if a pipe command
// is seen. The pointer to the start of the string is updated to where parsing
// stopped, and a linked list of the arguments of the command is returned.
// A null linked list is used to represent if an error was seen during parsing.
str_ll* split(uint8_t* cmd, uint32_t* offset) {
    enum split_states   state;              // FSM for splitting command
    uint8_t             temp[MAX_LENGTH];   // Temp buffer transcribe string
    uint32_t            temp_off;           // Offset within temp
    uint8_t*            null_end;           // NULL termination of array
    str_ll*             split_list;         // Linked list of arguments
    str_ll*             cur_elem;           // Current element in the list

    // Allocate and set the null terminator
    null_end = (uint8_t*)malloc(sizeof(uint8_t));
    *null_end = ASCII_NULL;

    // Allocate first element of linked list and set to null
    split_list = (str_ll*)malloc(sizeof(uint8_t));
    split_list->next = NULL;
    split_list->str = null_end;

    // Start working on the first element
    cur_elem = split_list;

    state = INIT;   // Start in the initial state
    temp_off = 0;

    while(state != ERROR_STATE && state != DONE) {
        switch(state) {
            case INIT:
                switch(cmd[*offset]) {
                    // Catch all invalid starting characters
                    case '|':
                    case '&':
                        state = ERROR_STATE;      // Generate an error
                        break;
                    case ASCII_NULL:
                    case '\n':
                        state = DONE;
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
                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
                        // Add string to list first
                        temp[temp_off] = ASCII_NULL;
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = DONE;
                        break;
                    case '|':
                        // Add string to list first
                        temp[temp_off] = ASCII_NULL;
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = PIPE_CHAR;
                        break;
                    case '&':
                        // Add string to list first
                        temp[temp_off] = ASCII_NULL;
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = AMP_CHAR;
                        break;
                    case '<':
                        // Add string to list first
                        temp[temp_off] = ASCII_NULL;
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = LT_CHAR;
                        break;
                    case '>':
                        // Add string to list first
                        temp[temp_off] = ASCII_NULL;
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
                if(cmd[*offset] != ' ') {
                    // NULL terminate str
                    temp[temp_off] = ASCII_NULL;
                    temp_off = 0;

                    // Add the element in
                    cur_elem = append(cur_elem, temp, null_end);
                }
                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
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
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                        state = NORM_CHAR;
                }
                break;
            case QUOTE:
                switch(cmd[*offset]) {
                    // Only way to break out of QUOTE is with another '\"' char
                    case '\"':
                        state = QUOTE_END;
                        break;
                    // Cannot terminate in the middle of a quote
                    case ASCII_NULL:
                    case '\n':
                        state = ERROR_STATE;
                        break;
                    // If still in the quote, just transcribe the character
                    default:
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                }
                break;
            case QUOTE_END:
                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
                        // NULL terminate str
                        temp[temp_off] = ASCII_NULL;
                        temp_off = 0;

                        // Add the element in
                        cur_elem = append(cur_elem, temp, null_end);

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

                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
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

                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
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
                        // Add string to list when leaving state
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        // Eat away white space
                        state = GT_WHITE;
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
            case GT_WHITE:
                switch(cmd[*offset])
                {
                    case ASCII_NULL:
                    case '\n':
                    case '|':
                    case '&':
                    case '<':
                    case '>':
                    case '!':
                        state = ERROR_STATE;
                        break;
                    case '\"':
                        state = QUOTE;
                        break;
                    default:
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                        state = NORM_CHAR;
                }
                break;
            case PIPE_CHAR:
                // Load buffer with a single '|'
                temp[0] = '|';
                temp[1] = ASCII_NULL;

                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
                    // Throw error on ||
                    case '|':
                    case '&':
                    case '>':
                    case '<':
                        state = ERROR_STATE;
                        break;
                    default:
                         // Add string to list when leaving state
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = DONE;
                }
                break;
            case EXC_CHAR:
                // Load a single ! in the buffer
                temp[0] = '!';
                temp[1] = ASCII_NULL;

                switch(cmd[*offset]) {
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
                break;
            case EXC_NUM:
                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
                        // Add string to list when leaving state
                        temp[temp_off] = ASCII_NULL;
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
                        state = ERROR_STATE;
                }
                break;
            case AMP_CHAR:
                // Load a single & in the buffer
                temp[0] = '&';
                temp[1] = ASCII_NULL;

                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
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

                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
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
                        state = DUP_REDIR_CHAR;
                        break;
                    default:
                        // Add to the list before we terminate
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        state = NORM_CHAR;
                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
                }
                break;
            case APPEND_CHAR:
                // Set buffer to >> (the first > is from GT_CHAR)
                temp[1] = '>';
                temp[2] = ASCII_NULL;

                switch(cmd[*offset]) {
                    case ASCII_NULL:
                    case '\n':
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
                        state = APPEND_CHAR;
                        break;
                    default:
                        // Add to the list before we terminate
                        temp_off = 0;
                        cur_elem = append(cur_elem, temp, null_end);

                        temp[temp_off] = cmd[*offset];
                        temp_off ++;
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
        if(state != DONE && state != ERROR_STATE) {
            (*offset) ++;
        }
    }
    
    // Check if pointing to '\n' and move to following NULL, otherwise error
    if (cmd[*offset] == '\n') {
        (*offset) ++;
        if (cmd[*offset] != ASCII_NULL) {
            state = ERROR_STATE;
        }
    }

    // Indicate an error by returning NULL
    if(state == ERROR_STATE) {
        // First we free the linked list
        while(split_list != NULL) {
            cur_elem = split_list->next;
            free(split_list);
            split_list = cur_elem;
        }
    }
    

    return split_list;
}

str_ll* append(str_ll* cur_end, uint8_t* new_val, uint8_t* null_val) {
    str_ll* next_end;

    // Put string in list
    cur_end->str = (uint8_t*) strdup((char*) new_val);
    // Add a new element to the list
    cur_end->next = (str_ll*)malloc(sizeof(str_ll));
    // Null terminate list
    next_end = cur_end->next;
    next_end->str = null_val;
    next_end->next = NULL;

    return next_end;
}

