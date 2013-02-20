#include "devices/shutdown.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "process.h"

#define INVALID_FILE_ID -1

static void syscall_handler(struct intr_frame *);
void kill_current_thread(int status);

// Prototypes for system call functions
void syscall_halt    (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_exit    (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_exec    (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_wait    (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_create  (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_remove  (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_open    (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_filesize(struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_read    (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_write   (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_seek    (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_tell    (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_close   (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_mmap    (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_munmap  (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_chdir   (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_mkdir   (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_readdir (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_isdir   (struct intr_frame *, void * arg1, void * arg2, void * arg3);
void syscall_inumber (struct intr_frame *, void * arg1, void * arg2, void * arg3);

// Table of function pointers for system calls. The order here must match the
// order of constants in the enum declaration in syscall-nr.h exactly.
static void (*syscall_table[])(struct intr_frame *, void *, void *, void *) = {syscall_halt, syscall_exit,
    syscall_exec, syscall_wait, syscall_create, syscall_remove, syscall_open,
    syscall_filesize, syscall_read, syscall_write, syscall_seek, syscall_tell,
    syscall_close, syscall_mmap, syscall_munmap, syscall_chdir, syscall_mkdir,
    syscall_readdir, syscall_isdir, syscall_inumber};
// Argument number for each system call. Again, order must match exactly
static uint32_t syscall_num_arg[] = {0, 1, 1, 1, 2, 1, 1, 1, 3, 3, 2, 1, 1, 2, 1, 1, 1, 2, 1, 1};
static uint32_t num_syscalls = 20;

void syscall_init(void)
{
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f)
{
    // Turn interrupts back on during system call
    intr_enable();
    // Get the system call number
    uint32_t num = *((uint32_t*)(f->esp));
    // Call appropriate system handler
    void *arg1 = *((void**)(f->esp +  4));
    void *arg2 = *((void**)(f->esp +  8));
    void *arg3 = *((void**)(f->esp + 12));
    // Check that system call number is valid and stored in user space
    // Check the the needed arguments are in user space
    if ((num < num_syscalls) && (
        ((syscall_num_arg[num] == 0) && is_user_vaddr(f->esp +  3)) || 
        ((syscall_num_arg[num] == 1) && is_user_vaddr(f->esp +  7) && is_user_vaddr(arg1)) || 
        ((syscall_num_arg[num] == 2) && is_user_vaddr(f->esp + 11) && is_user_vaddr(arg1) && is_user_vaddr(arg2)) || 
        ((syscall_num_arg[num] == 3) && is_user_vaddr(f->esp + 15) && is_user_vaddr(arg1) && is_user_vaddr(arg2) && is_user_vaddr(arg3))))
    {
        syscall_table[num](f, arg1, arg2, arg3);
    }
    // Kill the process if passed invalid pointer
    else
    {
        thread_current()->status = -1; // Indicate error and exit
        thread_exit();
    }
}

// Helper function to kill the current thread
void kill_current_thread(int status) {
    struct thread *t = thread_current();
    // Release filesys lock if owned
    if (filesys_access_held()) {
        release_filesys_access();
    }
    // Print out message
    printf ("%s: exit(%d)\n", t->name, status);
    // Set the exit status of the thread
    t->exit_status = status;
    // Exit the thread
    thread_exit();
}

// Halts the system and shuts it down
void syscall_halt(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    shutdown_power_off();
}

// TODO
void syscall_exit(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    int status = (int) arg1;
    kill_current_thread(status);
}

// TODO
void syscall_exec(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    char * cmd_line = (char*) arg1;
    f->eax = (uint32_t) process_execute(cmd_line);
}

// TODO
void syscall_wait(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    f->eax = process_wait(*((tid_t*) arg1));
}

// TODO
void syscall_create(struct intr_frame *f UNUSED, void * arg1, void * arg2, void * arg3 UNUSED)
{
    char * file = (char*) arg1;
    unsigned initial_size = (unsigned) arg2;
    // Check if empty file name and fail
    if (file == NULL) {
        kill_current_thread(-1);
    // Otherwise create file
    } else {
        acquire_filesys_access();
        // Create the file, return if successful
        f->eax = (uint32_t) filesys_create(file, initial_size);
        release_filesys_access();
    }
}

// TODO
void syscall_remove(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    char * file = (char*) arg1;
    // TODO
    //printf("remove\n");
    // Acquire lock to access file system; block until acquired
    if (file == NULL) {
        f->eax = (uint32_t) -1;
    } else {
        acquire_filesys_access();

        // remove the file, return if successful
        f->eax = (uint32_t) filesys_remove(file);

        // TODO: Done, relinquish access to the file system. */
        release_filesys_access();
    }
}

// TODO
void syscall_open(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    char * file = (char*) arg1;
    struct file_id *new_file_id;
    struct file *file_pt;
    struct thread *t = thread_current();

    if (file == NULL) {
        f->eax = (uint32_t) -1; // Return an error if passed NULL file

    } else {

        // Allocate a new file ID
        new_file_id = (struct file_id *) malloc(sizeof(struct file_id));

        if (new_file_id == NULL) {

            f->eax = (uint32_t) -1; // Return an error if allocation fails

        } else {

            // Acquire lock to access file system; block until acquired
            acquire_filesys_access();

            // Open the file, return the file pointer
            file_pt = filesys_open(file);

            // Check if open failed
            if (file_pt == NULL) {
                f->eax = -1;    // Return an error
                free((void *) new_file_id); // Free file ID, it won't be used

            } else {

                new_file_id->fid = allocate_fid();  // Get a file ID number
                new_file_id->f = file_pt;           // Get the file pointer

                // Add to list of opened files
                list_push_back(&(t->files_opened), &(new_file_id->elem));

                f->eax = (uint32_t) file_pt;    // Return the file pointer
            }
            // Done, relinquish access to the file system.
            release_filesys_access();
        }
    }
}

// TODO
void syscall_filesize(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    int fd = (int) arg1;
    struct file *file_to_access;
    struct thread *t = thread_current();
    // TODO
    //printf("filesize\n");
    // Get the file pointer
    file_to_access = file_fid_to_f(fd, &(t->files_opened));

    // If there is an invalid file descriptor, return an error
    if (file_to_access == NULL) {
        f->eax = (uint32_t) -1;
    } else {

        // TODO: Acquire lock to access file system; block until acquired
        acquire_filesys_access();

        // Read from the file
        f->eax = (uint32_t) file_length(file_to_access);

        // Done, relinquish access to the file system.
        release_filesys_access();

    }
}

// TODO
void syscall_read(struct intr_frame *f, void * arg1, void * arg2, void * arg3)
{
    int fd = (int) arg1;
    void *buffer = arg2;
    unsigned size = (unsigned) arg3;
    struct file *file_to_access;
    struct thread *t = thread_current();
    // TODO
    //printf("read\n");

    // Get the file pointer
    file_to_access = file_fid_to_f(fd, &(t->files_opened));

    // If there is an invalid file descriptor, return an error
    if (file_to_access == NULL) {
        f->eax = (uint32_t) -1;
    } else {

        // TODO: implement reading from stdin!!!!

        // TODO: Acquire lock to access file system; block until acquired
        acquire_filesys_access();

        // Read from the file
        f->eax = (uint32_t) file_read(file_to_access, buffer, (off_t) size);

        // TODO: Done, relinquish access to the file system.
        release_filesys_access();
    }
}

// TODO
void syscall_write(struct intr_frame *f, void * arg1, void * arg2, void * arg3)
{
    int fd = (int) arg1;
    void *buffer = arg2;
    unsigned size = (unsigned) arg3;
    struct file *file_to_access;
    struct thread *t = thread_current();

    // Get the file pointer
    file_to_access = file_fid_to_f(fd, &(t->files_opened));

    // Write out to console if given fd 1 (stdout)
    if (fd == 1)
    {
        putbuf(buffer, size);
        f->eax = (uint32_t) size;
    }
    // If there is an invalid file descriptor, return an error
    else if (file_to_access == NULL) {
        f->eax = (uint32_t) -1;
    } else {
        // TODO: Acquire lock to access file system; block until acquired
        acquire_filesys_access();

        // Write to the file
        f->eax = (uint32_t) file_write(file_to_access, buffer, (off_t) size);

        // TODO: Done, relinquish access to the file system.
        release_filesys_access();

    }
}

// TODO
void syscall_seek(struct intr_frame *f UNUSED, void * arg1, void * arg2, void * arg3 UNUSED)
{
    int fd = (int) arg1;
    unsigned position = (unsigned) arg2;
    struct file *file_to_access;
    struct thread *t = thread_current();
    // TODO
    //printf("seek\n");

    // Get the file pointer
    file_to_access = file_fid_to_f(fd, &(t->files_opened));

    // If there is an invalid file descriptor, kill the thread
    if (file_to_access == NULL) {
        kill_current_thread(-1);
    } else {
        // TODO: Acquire lock to access file system; block until acquired
        acquire_filesys_access();

        // Go to position in file
        file_seek(file_to_access, (off_t) position);

        // TODO: Done, relinquish access to the file system.
        release_filesys_access();
    }
}

// TODO
void syscall_tell(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    int fd = (int) arg1;
    struct file *file_to_access;
    struct thread *t = thread_current();
    // TODO
    //printf("tell\n");

    // Get the file pointer
    file_to_access = file_fid_to_f(fd, &(t->files_opened));

    // If there is an invalid file descriptor, kill th thread
    if (file_to_access == NULL) {
        kill_current_thread(-1);
    } else {
        // TODO: Acquire lock to access file system; block until acquired
        acquire_filesys_access();

        // Get position in file
        f->eax = (uint32_t) file_tell(file_to_access);

        // TODO: Done, relinquish access to the file system.
        release_filesys_access();
    }
}

// TODO
void syscall_close(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    int fd = (int) arg1;
    struct file *file_to_access;
    struct file_id *closed_file_id;
    struct thread *t = thread_current();

    // Get the file pointer
    file_to_access = file_fid_to_f(fd, &(t->files_opened));
    ASSERT(file_to_access != NULL); // Should not pass a bad fd

    // If there is an invalid file descriptor, kill the thread
    if (file_to_access == NULL) {
        kill_current_thread(-1);
    } else {
        // TODO: Acquire lock to access file system; block until acquired
        acquire_filesys_access();

        // Close the file
        file_close(file_to_access);

        // SHIR:
        // TODO: free file id in open list????
        // TODO: change exit function to use file ids instead of files!
        // TODO: write function to get file_id struct based on fid
        // Remove the file (id struct) from open list
        // list_remove(&(closed_file_id->elem));
        // free((void *) closed_file_id);  // Free the file ID, no longer used

        // TODO: Done, relinquish access to the file system.
        release_filesys_access();
    }
}

// TODO - Project 3
void syscall_mmap(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    // TODO
}

// TODO - Project 3
void syscall_munmap(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    // TODO
}

// TODO - Project 4
void syscall_chdir(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    // TODO
}

// TODO - Project 4
void syscall_mkdir(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    // TODO
}


// TODO - Project 4
void syscall_readdir(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    // TODO
}

// TODO - Project 4
void syscall_isdir(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    // TODO
}

// TODO - Project 4
void syscall_inumber(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    // TODO
}
