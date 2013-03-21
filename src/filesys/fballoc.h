#ifndef VM_FBALLOC_H
#define VM_FBALLOC_H

#include "off_t.h"
#include <stddef.h>
#include <list.h>
#include "devices/block.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
  
#define FBLOCK_U    0x1             // 1 = used,     0 = not used
#define FBLOCK_D    0x2             // 1 = accessed, 0 = not accessed
#define FBLOCK_A    0x4             // 1 = dirty,    0 = not dirty
#define NUM_FBLOCKS 64              // Note: must be power of 2, may be partially hardcoded

// A file block entry struct
struct fblock_entry
{
    uint32_t status;                // Bits used to denote fblock status
    struct inode* inode;            // File inode fblock belongs to
    off_t start;                    // Start offset for fblock in file
    block_sector_t sector;          // Sector for fblock on disk
    struct lock in_use;             // Lock a fblock while modifying data
};

// A file block struct
struct fblock
{
    uint8_t data[BLOCK_SECTOR_SIZE];    // Wrapper for a sector size data chunk
};

void fballoc_init(void);
uint32_t fballoc_load_fblock(struct inode*, off_t, block_sector_t);
void fballoc_free_fblock(uint32_t);

void fballoc_write_back(uint32_t);
void fballoc_write_all(void);

uint32_t fballoc_evict(void);
uint32_t fballoc_evict_save(uint32_t);

void * fballoc_idx_to_addr(uint32_t);
void fblock_mark_read(uint32_t);
void fblock_mark_write(uint32_t);
void fblock_lock_acquire(uint32_t);
void fblock_lock_release(uint32_t);
bool fblock_lock_owner(uint32_t);

uint32_t fblock_is_cached(struct inode*, off_t);

// Functions to set the status bits
static inline void fblock_set_used(uint32_t *status)
{
    *status |= FBLOCK_U;
}
static inline void fblock_set_dirty(uint32_t *status)
{
    *status |= FBLOCK_D;
}
static inline void fblock_set_accessed(uint32_t *status)
{
    *status |= FBLOCK_A;
}

static inline void fblock_set_not_used(uint32_t *status)
{
    *status &= ~FBLOCK_U;
}
static inline void fblock_set_not_dirty(uint32_t *status)
{
    *status &= ~FBLOCK_D;
}
static inline void fblock_set_not_accessed(uint32_t *status)
{
    *status &= ~FBLOCK_A;
}

// Functions to check the status bits
static inline bool fblock_is_used(uint32_t status)
{
    return (bool) (status & FBLOCK_U);
}
static inline bool fblock_is_dirty(uint32_t status)
{
    return (bool) (status & FBLOCK_D);
}
static inline bool fblock_is_accessed(uint32_t status)
{
    return (bool) (status & FBLOCK_A);
}

#endif // vm/fballoc.h
