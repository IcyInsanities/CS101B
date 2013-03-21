#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include "off_t.h"
#include <stdbool.h>
#include "devices/block.h"

#define NUM_DIRECT_FILE_SECTOR      124 // Leave space for 2 indirect and 2 other
                                        // data elements in struct inode_disk
#define NUM_INDIRECT_FILE_SECTOR    128

#define INDIRECT_ENTRY_IDX          NUM_DIRECT_FILE_SECTOR
#define DBL_INDIRECT_ENTRY_IDX      NUM_DIRECT_FILE_SECTOR+1
#define DIRECT_BLOCK_OFFSET         0x10000000
#define INDIRECT_BLOCK_OFFSET       DIRECT_BLOCK_OFFSET + BLOCK_SECTOR_SIZE
#define DBL_INDIRECT_BLOCK_OFFSET   INDIRECT_BLOCK_OFFSET + BLOCK_SECTOR_SIZE

struct bitmap;

void inode_init(void);
bool inode_create(block_sector_t, off_t);
struct inode *inode_open(block_sector_t);
struct inode *inode_reopen(struct inode *);
block_sector_t inode_get_inumber(const struct inode *);
bool inode_is_dir(const struct inode *);
void inode_set_dir(struct inode *);
void inode_close(struct inode *);
void inode_remove(struct inode *);
bool inode_is_removed(struct inode *);
off_t inode_read_at(struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at(struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write(struct inode *);
void inode_allow_write(struct inode *);
off_t inode_length(const struct inode *);

block_sector_t byte_to_sector(struct inode *, off_t);

/* Block cache for file system. */
void inode_get_block(struct inode *, size_t);
void inode_release_block(struct inode *, size_t);
bool inode_is_block_owned(struct inode *, size_t);
uint32_t inode_get_cache_block_idx(struct inode *, off_t, block_sector_t);

void inode_force_close_all(void);

#endif /* filesys/inode.h */
