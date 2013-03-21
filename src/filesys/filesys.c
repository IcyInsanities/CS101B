#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/fballoc.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"

struct lock filesys_lock;

/*! Partition that contains the file system. */
struct block *fs_device;

static void do_format(void);

/*! Initializes the file system module.
    If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
    fs_device = block_get_role(BLOCK_FILESYS);
    if (fs_device == NULL)
        PANIC("No file system device found, can't initialize file system.");

    inode_init();
    free_map_init();

    if (format)
        do_format();

    free_map_open();

    /* Initialize the lock for the file system */
    lock_init(&filesys_lock);
    /* Set root dir to current directory for initial thread */
    thread_current()->curr_dir = dir_open_root();
}

/*! Shuts down the file system module, writing any unwritten data to disk. */
void filesys_done(void) {
    uint32_t i;
    // TODO: Prevent new blocks from being loaded?
    // TODO: Clear read_ahead queue
    for (i = 0; i < NUM_FBLOCKS; ++i) {
        fballoc_free_fblock(i);
    }
    free_map_close();
}

/*! Creates a file named NAME with the given INITIAL_SIZE.  Returns true if
    successful, false otherwise.  Fails if a file named NAME already exists,
    or if internal memory allocation fails. */
bool filesys_create(const char *name, off_t initial_size) {
    block_sector_t inode_sector = 0;
    struct dir *dir = NULL;
    char name_file[NAME_MAX + 1];

    bool success = (!filesys_parse_path_split(name, &dir, name_file) && // Can't / terminate
                    dir != NULL && !dir_is_removed(dir) &&
                    free_map_allocate(1, &inode_sector) &&
                    inode_create(inode_sector, initial_size) &&
                    dir_add(dir, name_file, inode_sector));
    if (!success && inode_sector != 0)
        free_map_release(inode_sector, 1);
    dir_close(dir);

    return success;
}
/*! Create variant that creates a directory, default size space for 5 entries.
    This is arbitrary, but will get extended as needed. */
bool filesys_create_dir(const char *name) {
    block_sector_t inode_sector = 0;
    struct dir *dir = NULL;
    char name_file[NAME_MAX + 1];
    filesys_parse_path_split(name, &dir, name_file); // Don't care if / terminate
    bool success = (dir != NULL && !dir_is_removed(dir) &&
                    free_map_allocate(1, &inode_sector) &&
                    dir_create(inode_sector, 5, dir) &&
                    dir_add_dir(dir, name_file, inode_sector));
    if (!success && inode_sector != 0)
        free_map_release(inode_sector, 1);
    dir_close(dir);

    return success;
}

/*! Opens the file or directory with the given NAME.  Returns the new file if
    successful or a null pointer otherwise.  Fails if no file named NAME exists,
    or if an internal memory allocation fails. */
struct file * filesys_open(const char *name) {
    struct dir *dir = NULL;
    struct inode *inode = NULL;
    char name_file[NAME_MAX + 1];

    bool slash_term = filesys_parse_path_split(name, &dir, name_file);
    if (dir != NULL && !dir_is_removed(dir)) {
        if (slash_term) {
            dir_lookup_dir(dir, name_file, &inode);
        } else {
            dir_lookup_any(dir, name_file, &inode);
        }
    }
    dir_close(dir);

    /* Open file or directory correctly */
    if (inode != NULL && inode_is_dir(inode)) {
        return (struct file *) dir_open(inode);
    } else {
        return file_open(inode);
    }
}

/*! Deletes the file or directoy named NAME.  Returns true if successful, false
    on failure. Fails if nothing by NAME exists, or if an internal memory
    allocation fails. */
bool filesys_remove(const char *name) {
    struct dir *dir = NULL;
    char name_file[NAME_MAX + 1];
    bool success = false;

    bool slash_term = filesys_parse_path_split(name, &dir, name_file);
    if (dir != NULL && !dir_is_removed(dir)) {
        if (slash_term) {
            success = dir_remove_dir(dir, name_file);
        } else {
            success = dir_remove(dir, name_file);
        }
    }
    dir_close(dir);
    
    return success;
}

/*! Formats the file system. */
static void do_format(void) {
    printf("Formatting file system...");
    free_map_create();
    if (!dir_create(ROOT_DIR_SECTOR, 16, NULL))
        PANIC("root directory creation failed");
    free_map_close();
    printf("done.\n");
}

/*! Acquires file system lock, blocking until successful. */
void acquire_filesys_access(void) {
    lock_acquire(&filesys_lock);
}

/*! Releases file system lock. */
void release_filesys_access(void) {
    lock_release(&filesys_lock);
}

/*! Attempts to acquire file system lock, returns if successful or not. */
bool try_acquire_filesys_access(void) {
    return lock_try_acquire(&filesys_lock);
}

/*! Retturns whether the current thread holds access to the file system. */
bool filesys_access_held(void) {
    return lock_held_by_current_thread(&filesys_lock);
}

/*! Changes the current working directory to the given directory */
bool filesys_change_cwd(const char *name) {
    struct thread *t = thread_current();
    /* Make a copy in case parsing path fails */
    struct dir * temp_cpy = t->curr_dir;
    /* Check if parsing path failed and restore original cwd */
    t->curr_dir = filesys_parse_path(name);
    if (t->curr_dir == NULL) {
        t->curr_dir = temp_cpy;;
        return false;
    } else {
        dir_close(temp_cpy);
        return true;
    }
}

/*! Parses PATH, returning whether there is a '/' at then end.  DIR is set
    to the parent directory of the directory or file name that NAME is set to.
    If parsing of the path fails, DIR and NAME are set to NULL. */
bool filesys_parse_path_split(const char *path, struct dir **dir, char *name) {

    void *path_tokens;
    size_t path_len = strlen(path) + 1; // Need to get the NULL char
    char *curr_name;
    char *new_name;
    char *save_ptr;
    struct inode *curr_inode;
    bool slash_at_end;

    path_tokens = malloc(path_len);
    ASSERT(path_tokens != NULL);
    /* The starting parent directory is the current directory. */
    *dir = dir_reopen(thread_current()->curr_dir);
    /* If there is a '/' at the start, must go to root. */
    if (path[0] == '/') {
        dir_close(*dir);
        *dir = dir_open_root();
        /* Handle special case of path = "/" */
        if (path_len-1 == 1) {
            name[0] = '.'; name[1] = '\0'; /* Set name to self and return as dir */
            free(path_tokens);
            return true;
        }
    }
    //TODO printf("start dir: %d\n", inode_get_inumber(dir_get_inode(*dir)));


    /* Copy the path so it can be tokenized. */
    strlcpy(path_tokens, path, path_len);

    /* Check if the path ends with a '/' */
    slash_at_end = (bool) (path[strlen(path)-1] == '/');

    /* Get the first name in the path. */
    curr_name = strtok_r(path_tokens, "/", &save_ptr);

    /* If there is no name, nothing to extract from path. */
    if (curr_name == NULL) {
        goto filesys_parse_path_split_done_fail;
    }

    /* Loop until we hit the end of the path. */
    for (new_name = strtok_r(NULL, "/", &save_ptr); new_name != NULL;
         new_name = strtok_r(NULL, "/", &save_ptr)) {

        if (dir_lookup_dir(*dir, curr_name, &curr_inode)) {
            /* Close the "old" directory. */
            dir_close(*dir);
            /* Get the next directory. */
            *dir = dir_open(curr_inode);
        }
        /* If it wasn't found, cannot parse path. */
        else {
            goto filesys_parse_path_split_done_fail;
        }

        /* Update the name we are looking for. */
        curr_name = new_name;
    }

    /* If asked to open the current directory, we have no parent dir to return. */
    //if (streq(curr_name, ".")) {
    //    strlcpy(name, curr_name, strlen(curr_name) + 1);
    //    *dir = NULL;
    //    return slash_at_end;
    //}
    if (streq(curr_name, "..")) {
        goto filesys_parse_path_split_done_fail;
    }

    /* Return the name, parent directory, and whether path ended with '/'. */
    if (strlen(curr_name) > NAME_MAX) {
        goto filesys_parse_path_split_done_fail;
    }
    strlcpy(name, curr_name, strlen(curr_name) + 1);
    free(path_tokens);
    
    //TODO printf("dir: %d, name: %s, path: %s\n", inode_get_inumber(dir_get_inode(*dir)), name, path);
    
    return slash_at_end;
    
filesys_parse_path_split_done_fail:
    dir_close(*dir);
    *dir = NULL;
    name[0] = '\0';
    free(path_tokens);
    return slash_at_end;
}

    //if (strcmp(name, ".") != 0) { // NEED TO ADD NULL AT END OF CONSTANT STRING?

    //if (dir_lookup_dir(curr_dir, name, &curr_inode)) {
    //    // TODO: do we need to close the old inode as well, or does closing the directory do that?
    //    // Close the "old" directory
    //    dir_close(curr_dir);
    //    // Get the next directory
    //    curr_dir = dir_open(curr_inode);
    //}
    //else {
    //    t->curr_dir = thread_dir;
    //    return NULL;
    //}

    //}


//dir *filesys_parse_path(char* path)
    // Make a copy of the path string
    // length = strlen(string) + 1
    // use strlcpy(buffer, string_2_cpy,lengh)
    // bool slash_at_end;

    // thread_dir
    // if (first char is '/') {
    //      curr_dir = root;
    // } else {
    //      curr_dir = thread_dir
    // }
    //
    // Initialize tokenizer and get first obj_name
    // if ('.') {
    //      do nothing
    //  } else {
    //      if (dir_lookup_dir(token)) {
    //          curr_dir = looked_up_dir;
    //      } else {
    //          return null;
    //      }
    //  }
    //

    // Loop
        // Get a token (dir or file name)
        // if ('.') {
        //      do nothing
        //  } else {
        //      if (dir_lookup_dir(token)) {
        //          curr_dir = looked_up_dir;
        //      } else {
        //          return null;
        //      }
        //  }

    // return curr_dir;

/*! Parse PATH, returning the directory at the end of the path.  Returns NULL if
    parsing is unsuccessful. */
struct dir *filesys_parse_path(const char *path) {

    void *path_tokens;
    size_t path_len = strlen(path) + 1; // Need to get the NULL char
    char *dir_name;
    char *save_ptr;
    struct inode *curr_inode;
    struct dir *dir = dir_reopen(thread_current()->curr_dir);

    /* Switch to root path if given absolute address */
    if (path[0] == '/') {
        dir_close(dir);
        dir = dir_open_root();
    }

    path_tokens = malloc(path_len);
    if (path_tokens == NULL) {
        dir_close(dir);
        return NULL;
    }
    strlcpy(path_tokens, path, path_len);

    dir_name = strtok_r(path_tokens, "/", &save_ptr);
    if (dir_lookup_dir(dir, dir_name, &curr_inode)) {
        dir_close(dir);
        // Get the next directory
        dir = dir_open(curr_inode);
    }
    else {
        dir_close(dir);
        free(path_tokens);
        return NULL;
    }

    for (dir_name = strtok_r(NULL, "/", &save_ptr); dir_name != NULL;
         dir_name = strtok_r(NULL, "/", &save_ptr)) {
        if (dir_lookup_dir(dir, dir_name, &curr_inode)) {
            dir_close(dir);
            // Get the next directory
            dir = dir_open(curr_inode);
        }
        else {
            dir_close(dir);
            free(path_tokens);
            return NULL;
        }
    }

    //TODO printf("dir: %d, path: %s\n", inode_get_inumber(dir_get_inode(dir)), path);
    
    free(path_tokens);
    return dir;
}
