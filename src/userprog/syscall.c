#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "process.h"

static void syscall_handler(struct intr_frame *);

// Prototypes for system call functions
void syscall_halt(struct intr_frame *);
void syscall_exit(struct intr_frame *);
void syscall_exec(struct intr_frame *);
void syscall_wait(struct intr_frame *);
void syscall_create(struct intr_frame *);
void syscall_remove(struct intr_frame *);
void syscall_open(struct intr_frame *);
void syscall_filesize(struct intr_frame *);
void syscall_read(struct intr_frame *);
void syscall_write(struct intr_frame *);
void syscall_seek(struct intr_frame *);
void syscall_tell(struct intr_frame *);
void syscall_close(struct intr_frame *);
void syscall_mmap(struct intr_frame *);
void syscall_munmap(struct intr_frame *);
void syscall_chdir(struct intr_frame *);
void syscall_mkdir(struct intr_frame *);
void syscall_readdir(struct intr_frame *);
void syscall_isdir(struct intr_frame *);
void syscall_inumber(struct intr_frame *);

// Table of function pointers for system calls. The order here must match the
// order of constants in the enum declaration in syscall-nr.h exactly.
static void (*syscall_table[])(struct intr_frame *) = {syscall_halt, syscall_exit, 
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
    uint32_t* esp = f->esp;
    uint32_t num = *esp;
    // Call appropriate system handler
    syscall_table[num](f);
}

// TODO
void syscall_halt(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_exit(struct intr_frame *f UNUSED)
{
    struct thread *current_thread = thread_current();

    // Set the exit status of the thread
    current_thread->exit_status = status;

    // Exit the thread
    thread_exit();
}

// TODO
void syscall_exec(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_wait(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_create(struct intr_frame *f UNUSED)
{

    // TODO: modify to put into stack frame
    return process_wait(pid);

}

// TODO
void syscall_remove(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_open(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_filesize(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_read(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_write(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_seek(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_tell(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_close(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_mmap(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_munmap(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_chdir(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_mkdir(struct intr_frame *f UNUSED)
{
    // TODO
}

<<<<<<< Updated upstream
// TODO
void syscall_readdir(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_isdir(struct intr_frame *f UNUSED)
{
    // TODO
}

// TODO
void syscall_inumber(struct intr_frame *f UNUSED)
{
    // TODO
}
