#include "devices/shutdown.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "process.h"

static void syscall_handler(struct intr_frame *);

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
    
void syscall_init(void)
{
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f)
{
    printf("system call!\n");
    // Turn interrupts back on during system call
    intr_enable();
    // Get the system call number
    uint32_t num = *((uint32_t*)(f->esp));
    // Call appropriate system handler
    void *arg1 = *((void**)(f->esp +  4));
    void *arg2 = *((void**)(f->esp +  8));
    void *arg3 = *((void**)(f->esp + 12));
    syscall_table[num](f, arg1, arg2, arg3);
}

// Halts the system and shuts it down
void syscall_halt(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    shutdown_power_off();
}

// TODO
void syscall_exit(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    struct thread *current_thread = thread_current();

    // Set the exit status of the thread
    current_thread->exit_status = status;

    // Exit the thread
    thread_exit();
}

// TODO
void syscall_exec(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    char * cmd_line = (char*) arg1;
    // TODO
}

// TODO
void syscall_wait(struct intr_frame *f UNUSED, void * arg1 UNUSED, void * arg2 UNUSED, void * arg3 UNUSED)
{
    // TODO
}

// TODO
void syscall_create(struct intr_frame *f UNUSED, void * arg1, void * arg2, void * arg3 UNUSED)
{
<<<<<<< HEAD

    // TODO: modify to put into stack frame
    return process_wait(pid);

=======
    char * file = (char*) arg1;
    unsigned initial_size = (unsigned) arg2;
    // TODO
>>>>>>> 3a76b9699139b0b159fe05f175300a399937288d
}

// TODO
void syscall_remove(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    char * file = (char*) arg1;
    // TODO
}

// TODO
void syscall_open(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    char * file = (char*) arg1;
    // TODO
}

// TODO
void syscall_filesize(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    int fd = (int) arg1;
    // TODO
}

// TODO
void syscall_read(struct intr_frame *f UNUSED, void * arg1, void * arg2, void * arg3)
{
    int fd = (int) arg1;
    void *buffer = arg2;
    unsigned size = (unsigned) arg3;
    // TODO
}

// TODO
void syscall_write(struct intr_frame *f UNUSED, void * arg1, void * arg2, void * arg3)
{
    int fd = (int) arg1;
    void *buffer = arg2;
    unsigned size = (unsigned) arg3;
    // TODO
}

// TODO
void syscall_seek(struct intr_frame *f UNUSED, void * arg1, void * arg2, void * arg3 UNUSED)
{
    int fd = (int) arg1;
    unsigned position = (unsigned) arg2;
    // TODO
}

// TODO
void syscall_tell(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    int fd = (int) arg1;
    // TODO
}

// TODO
void syscall_close(struct intr_frame *f UNUSED, void * arg1, void * arg2 UNUSED, void * arg3 UNUSED)
{
    int fd = (int) arg1;
    // TODO
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
