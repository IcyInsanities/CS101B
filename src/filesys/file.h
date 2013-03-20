#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"
#include <list.h>
#include "devices/block.h"

struct inode;

/*! An open file. */
struct file {
    struct inode *inode;        /*!< File's inode. */
    off_t pos;                  /*!< Current position. */
    bool deny_write;            /*!< Has file_deny_write() been called? */
};

/*! File identifier type, use the integer as the identifier. */
typedef int fid_t;

/*! A wrapper for file identifiers so they can be used with lists. */
struct file_id {
    fid_t fid;                  /*!< File identifier. */
    struct file *f;             /*!< File struct pointer. */
    struct list_elem elem;      /*!< List element. */
};

/* Opening and closing files. */
struct file *file_open (struct inode *);
struct file *file_reopen (struct file *);
void file_close (struct file *);
struct inode *file_get_inode (struct file *);
block_sector_t file_get_inode_sector(struct file *);

/* Reading and writing. */
off_t file_read (struct file *, void *, off_t);
off_t file_read_at (struct file *, void *, off_t size, off_t start);
off_t file_write (struct file *, const void *, off_t);
off_t file_write_at (struct file *, const void *, off_t size, off_t start);

/* Preventing writes. */
void file_deny_write (struct file *);
void file_allow_write (struct file *);

/* File position. */
void file_seek (struct file *, off_t);
off_t file_tell (struct file *);
off_t file_length (struct file *);

/* Convert from fid to struct file pointers */
struct file * file_fid_to_f(fid_t, struct list *);
struct file_id * file_fid_to_f_id(fid_t, struct list *);
fid_t allocate_fid (void);

#endif /* filesys/file.h */

