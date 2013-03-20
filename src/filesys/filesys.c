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

    // Initialize the lock for the file system
    lock_init(&filesys_lock);
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
    struct dir *dir = dir_open_root();
    bool success = (dir != NULL &&
                    free_map_allocate(1, &inode_sector) &&
                    inode_create(inode_sector, initial_size) &&
                    dir_add(dir, name, inode_sector));
    if (!success && inode_sector != 0) 
        free_map_release(inode_sector, 1);
    dir_close(dir);

    return success;
}
/*! Create variant that creates a directory, default size space for 5 entries.
    This is arbitrary, but will get extended as needed. */
bool filesys_create_dir(const char *name) {
    block_sector_t inode_sector = 0;
    struct dir *dir = dir_open_root();
    bool success = (dir != NULL &&
                    free_map_allocate(1, &inode_sector) &&
                    dir_create(inode_sector, 5, dir) &&
                    dir_add_dir(dir, name, inode_sector));
    if (!success && inode_sector != 0) 
        free_map_release(inode_sector, 1);
    dir_close(dir);

    return success;
}

/*! Opens the file or directory with the given NAME.  Returns the new file if
    successful or a null pointer otherwise.  Fails if no file named NAME exists,
    or if an internal memory allocation fails. */
struct file * filesys_open(const char *name) {
    struct dir *dir = dir_open_root();
    struct inode *inode = NULL;

    if (dir != NULL)
        dir_lookup_any(dir, name, &inode);
    dir_close(dir);

    return file_open(inode);
}

/*! Deletes the file named NAME.  Returns true if successful, false on failure.
    Fails if no file named NAME exists, or if an internal memory allocation
    fails. */
bool filesys_remove(const char *name) {
    struct dir *dir = dir_open_root();
    bool success = dir != NULL && dir_remove(dir, name);
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
    // TODO: NEED TO IMPLEMENT THIS...
    return false;
}

//filesys_parse_path_split(char* path, dir * directory_containing_name, char *name_of_file_or_dir)
    // Make a copy of the path string
    // length = strlen(string) + 1
    // use strlcpy(buffer, string_2_cpy,lengh)
    // bool slash_at_end;
    
    // if (last_char == '/') {
    //      slash_at_end = true;
    //  }
    //  else {
    //      slash_at_end = false;
    //  }
    
    // thread_dir
    // if (first char is '/') {
    //      curr_dir = root;
    //      containing_dir = root; // this might be a problem...might have to do a special case in the loop for this
    // } else {
    //      curr_dir = thread_dir
    //      containing_dir = one_back(thread_dir)
    // }
    // 
    // Initialize tokenizer and get first obj_name
    // if ('.') {
    //      do nothing
    //  } else {
    //      if (dir_lookup_dir(token)) {
    //          if (curr_dir != root) {
    //              containing_dir = curr_dir;
    //          }
    //          curr_dir = looked_up_dir;
    //
    //      } else {
    //          name_of_file_or_dir = token;                // FIX: This is only true if we have actually reached the end of the path!!!!
    //          directory_containing_name = containing_dir;
    //      }
    //  }
    //
    
    // Loop
        // Get a token (dir or file name)
        // if ('.') {
        //      do nothing
        //      if (dir_lookup_dir(token)) {
        //          if (curr_dir != root) {
        //              containing_dir = curr_dir;
        //          }
        //          curr_dir = looked_up_dir;
        //
        //      } else {
        //          name_of_file_or_dir = token;        // FIX: This is only true if we have actually reached the end of the path!!!!
        //          directory_containing_name = containing_dir;
        //      }
        //  }
        
    // return slash_at_end;
        
bool filesys_parse_path_split(char *path, struct dir *dir, char *name) {

    void *path_tokens;
    size_t path_len = strlen(path) + 1; // Need to get the NULL char
    struct thread *t = thread_current();
    char *curr_name;
    char *new_name;
    char *save_ptr;
    struct inode *curr_inode;
    struct dir *thread_dir;
    bool in_root;
    bool slash_at_end;
    
    /* The starting parent directory is the current directory. */
    dir = t->curr_dir;
    
    /* Copy the path so it can be tokenized. */
    strlcpy(path_tokens, path, path_len);
    
    /* Check if the path ends with a '/' */
    slash_at_end = (bool) (path[strlen(path)-1] == '/');
    
    /* Preserve the current directory. */
    thread_dir = dir_open(dir_get_inode(dir)); // TODO: Might have to NOT do this if path string is empty?
    
    /* If there is a '/' at the start, must go to root. */
    if (path[0] == '/') {
        dir_close(dir);
        dir = dir_open_root();
    }
    
    /* Get the first name in the path. */
    curr_name = strtok_r(path_tokens, "/", &save_ptr);
    
    /* If there is no name, nothing to extract from path. */
    if (curr_name == NULL) {
        dir = NULL;
        name = NULL;
        t->curr_dir = thread_dir;
        return slash_at_end;
    }
    
    /* Loop until we hit the end of the path. */
    for (new_name = strtok_r(NULL, "/", &save_ptr); new_name != NULL;
         new_name = strtok_r(NULL, "/", &save_ptr))
    {
        if (!streq(curr_name, ".")) {
            /* If the directory name was found, continue through path. */
            if (dir_lookup_dir(dir, curr_name, &curr_inode)) {
                // TODO: do we need to close the old inode as well, or does closing the directory do that?
                /* Close the "old" directory. */
                dir_close(dir);

                /* Get the next directory. */
                dir = dir_open(curr_inode);
            }
            /* If it wasn't found, cannot parse path. */
            else {
                dir = NULL;
                name = NULL;
                t->curr_dir = thread_dir;
                return NULL;
            }
        }
        
        /* Update the name we are looking for. */
        curr_name = new_name;
    }
    
    /* If the name is not a true name, cannot parse. */
    if (streq(curr_name, "..") || streq(curr_name, ".")) {
        name = NULL;
        dir = NULL;
        t->curr_dir = thread_dir;
        return slash_at_end;
    }
    
    /* Return the name, parent directory, and whether path ended with '/'. */
    strlcpy(name, curr_name, strlen(name) + 1);
    t->curr_dir = thread_dir;
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
    
struct dir *filesys_parse_path(char *path) {

    void *path_tokens;
    size_t path_len = strlen(path) + 1; // Need to get the NULL char
    struct thread *t = thread_current();
    struct dir *curr_dir = t->curr_dir;
    //char dir_name[NAME_MAX + 1];
    char *dir_name;
    char *save_ptr;
    struct inode *curr_inode;
    struct dir *thread_dir;
    strlcpy(path_tokens, path, path_len);
   
    // TODO: need a way to preserve the curr_dir of the thread, so it can be reopened later
    
    /* Open another copy of the current directory to preserve it. */
    //thread_dir = dir_open(curr_dir->inode); // TODO: Might have to NOT do this if path string is empty?
    thread_dir = dir_open(dir_get_inode(curr_dir)); // TODO: Might have to NOT do this if path string is empty?
    
    if (path[0] == '/') {
        dir_close(curr_dir);
        curr_dir = dir_open_root();
    }
    
    dir_name = strtok_r(path_tokens, "/", &save_ptr);
    if (!streq(dir_name, ".")) { // NEED TO ADD NULL AT END OF CONSTANT STRING?
        
        if (dir_lookup_dir(curr_dir, dir_name, &curr_inode)) {
            // TODO: do we need to close the old inode as well, or does closing the directory do that?
            // Close the "old" directory
            dir_close(curr_dir);
            // Get the next directory
            curr_dir = dir_open(curr_inode);
        }
        else {
            t->curr_dir = thread_dir;
            return NULL;
        }
        
    }
    
    for (dir_name = strtok_r(NULL, "/", &save_ptr); dir_name != NULL;
         dir_name = strtok_r(NULL, "/", &save_ptr))
    {
        if (!streq(dir_name, ".")) {
            if (dir_lookup_dir(curr_dir, dir_name, &curr_inode)) {
            // TODO: do we need to close the old inode as well, or does closing the directory do that?
            // Close the "old" directory
            dir_close(curr_dir);
            // Get the next directory
            curr_dir = dir_open(curr_inode);
            }
            else {
                t->curr_dir = thread_dir;
                return NULL;
            }
        }
    }
    
    t->curr_dir = thread_dir;
    return curr_dir;
    
}
