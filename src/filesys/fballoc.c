#include "fballoc.h"
#include "inode.h"
#include "off_t.h"
#include <stddef.h>
#include <stdint.h>
#include "devices/block.h"
#include "devices/timer.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

static struct fblock *fblock_arr;
static struct fblock_entry *fblock_entry_arr;

// Block device that contains the file system
extern struct block *fs_device;

// Initializes the file block allocator.
void fballoc_init(void)
{
    uint32_t i, num_pages;

    // Initialize fblock array
    num_pages = sizeof(struct fblock) * NUM_FBLOCKS;
    num_pages = (uint32_t) pg_round_up((void *) num_pages) / PGSIZE;
    // Get pages for fblock array
    fblock_arr = palloc_get_multiple(PAL_ASSERT | PAL_ZERO, num_pages);

    // Initialize fblock_entry array
    num_pages = sizeof(struct fblock_entry) * NUM_FBLOCKS;
    num_pages = (uint32_t) pg_round_up((void *) num_pages) / PGSIZE;
    // Get pages for fblock_entry array
    fblock_entry_arr = palloc_get_multiple(PAL_ASSERT | PAL_ZERO, num_pages);

    // Initialize fblock entries
    for (i = 0; i < NUM_FBLOCKS; ++i)
    {
        fblock_entry_arr[i].inumber = -1;
        lock_init(&(fblock_entry_arr[i].in_use));
    }
}

// Loads the given file location into the file block cache
uint32_t fballoc_load_fblock(block_sector_t inumber, off_t start, block_sector_t sector)
{
    // Evict a block if necessary, and get it to load file sector.
    uint32_t idx = fballoc_evict();
    // Acquire the lock to access the block in the cache.
    lock_acquire(&(fblock_entry_arr[idx].in_use));
    // Set up block metadata.
    fblock_set_used(&fblock_entry_arr[idx].status);
    fblock_set_accessed(&fblock_entry_arr[idx].status);
    fblock_entry_arr[idx].inumber = inumber;
    fblock_entry_arr[idx].start = start & ~(BLOCK_SECTOR_SIZE-1);
    fblock_entry_arr[idx].sector = sector;
    ASSERT(fblock_entry_arr[idx].num_users == 0);
    // Read in data.
    block_read(fs_device, fblock_entry_arr[idx].sector, (void*) &fblock_arr[idx]);
    // Done with block setup, release lock.
    lock_release(&(fblock_entry_arr[idx].in_use));

    return idx;
}

// Frees file block at the passed index IDX in the file block cache.
void fballoc_free_fblock(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);  // Check that we do not go beyond end of cache.
    // If the block is in use, free it.
    if (fblock_is_used(fblock_entry_arr[idx].status))
    {
        // Acquire the lock to access the block in the cache.
        lock_acquire(&(fblock_entry_arr[idx].in_use));
        // Write block back to disk.
        fballoc_write_back(idx);
        fblock_set_not_used(&fblock_entry_arr[idx].status);
        fblock_set_not_accessed(&fblock_entry_arr[idx].status);
        fblock_entry_arr[idx].inumber = -1;
        fblock_entry_arr[idx].start = 0;
        fblock_entry_arr[idx].sector = 0;
        ASSERT(fblock_entry_arr[idx].num_users == 0);
        // Done with block, release lock.
        lock_release(&(fblock_entry_arr[idx].in_use));
    }
}

// Writes a file block at the passed index IDX in the file block cache back into
// the file on disk.
void fballoc_write_back(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);  // Check that we do not go beyond end of cache.
    // Need to check if thread already locked block and avoid using lock.
    bool got_lock = lock_held_by_current_thread(&(fblock_entry_arr[idx].in_use));
    // Write data back if dirty.
    if (fblock_is_used(fblock_entry_arr[idx].status) && fblock_is_dirty(fblock_entry_arr[idx].status))
    {
        ASSERT(fblock_entry_arr[idx].inumber != (block_sector_t) -1);
        if (!got_lock)
        {
            lock_acquire(&(fblock_entry_arr[idx].in_use));
        }
        block_sector_t sector = fblock_entry_arr[idx].sector;
        // Mark file block as not dirty, and write it back to disk.
        fblock_set_not_dirty(&fblock_entry_arr[idx].status);
        block_write(fs_device, sector, (void*) &fblock_arr[idx]);
        // Done with block, release lock if it was acquired.
        if (!got_lock)
        {
            lock_release(&(fblock_entry_arr[idx].in_use));
        }
    }
}

// Write the entire file block cache back to disk.
void fballoc_write_all(void)
{
    uint32_t i;
    // Write all entries in file block cache back to disk.
    for (i = 0; i < NUM_FBLOCKS; ++i)
    {
        fballoc_write_back(i);
    }
}

// Evict a block given that a specified block will not be removed. If a block is
// specified, then it is possible that no block will be evicted. This usage
// allows for the read_ahead to fail eviction and place priority on currently
// used sectors over the predicted next sector.
// Eviction will take the first not accessed, not dirty block it finds. If none
// exists, it will take the first not accessed, dirty block it finds. Next it
// will take the first accessed, not dirty block it finds. If none of these
// cases exist, then all blocks are accessed and dirty so the start block is
// returned. Note: if possible, a block that is not in use is returned first.
uint32_t fballoc_evict_save(uint32_t save_idx)
{
    static uint32_t start_idx = 0; // Points to last evicted block
    uint32_t status;
    uint32_t idx, evict_idx;

    ASSERT((save_idx == (uint32_t) -1) | (save_idx < NUM_FBLOCKS));
    uint32_t first_na_nd_idx = -1;
    uint32_t first_na_d_idx  = -1;
    uint32_t first_a_nd_idx  = -1;

    // Find first of the 3 categories and save idx
    idx = (start_idx + 1) & (NUM_FBLOCKS-1);
    for( ; idx != start_idx; idx = (idx + 1) & (NUM_FBLOCKS-1))
    {
        // Skip block if supposed to be saved
        if (idx == save_idx)
        {
            continue;
        }
        // If found an unused block, return without affecting any status bits
        status = fblock_entry_arr[idx].status;
        if (!fblock_is_used(status))
        {
            return idx;
        }
        if (!fblock_is_accessed(status))
        {
            if (!fblock_is_dirty(status) && first_na_nd_idx == (uint32_t) -1)
            {
                first_na_nd_idx = idx;
            }
            else if (first_na_d_idx == (uint32_t) -1)
            {
                first_na_d_idx = idx;
            }
        }
        else if (!fblock_is_dirty(status) && first_a_nd_idx == (uint32_t) -1)
        {
            first_a_nd_idx = idx;
        }
    }
    // Evict not accessed block if possible.
    evict_idx = (first_na_nd_idx != (uint32_t) -1) ? first_na_nd_idx : first_na_d_idx;
    // If all accessed, then fail eviction if save block specified.
    if ((evict_idx == (uint32_t) -1) && (save_idx != (uint32_t) -1))
    {
        return -1;
    }
    // Clean up accessed bits.
    if (evict_idx != (uint32_t) -1)
    {
        // Reset accessed bits for intervening pages.
        idx = (start_idx + 1) & (NUM_FBLOCKS-1);
        for( ; idx != evict_idx; idx = (idx + 1) & (NUM_FBLOCKS-1))
        {
            fblock_set_not_accessed(&fblock_entry_arr[idx].status);
        }
    }
    else
    {
        // All accessed, so clear all bits
        for(idx = 0; idx != NUM_FBLOCKS; ++idx)
        {
            fblock_set_not_accessed(&fblock_entry_arr[idx].status);
        }
        // Choose correct accessed block to evict.
        // Either not dirty, or block after last evicted
        evict_idx = (first_a_nd_idx != (uint32_t) -1) ? first_a_nd_idx : (start_idx + 1) & (NUM_FBLOCKS-1);
    }
    // Evict the selected block.
    fballoc_free_fblock(evict_idx);
    start_idx = evict_idx;
    return evict_idx;
}

// Evict a block from the file block cache.  A call to this function gurantees
// that a block will be evicted.  See fballoc_evict_save() above for eviction
// priority.
uint32_t fballoc_evict(void)
{
    return fballoc_evict_save(-1);
}

// Returns a pointer to the file block in the file block cache at the passed
// index IDX.
void * fballoc_idx_to_addr(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);
    return (void*) &fblock_arr[idx];
}

// Marks a file block at index IDX in the file block cache as read from.
void fblock_mark_read(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);  // Make sure we do not go past end of cache.
    fblock_entry_arr[idx].status |= FBLOCK_A;
}

// Marks a file block at index IDX in the file block cache as written to.
void fblock_mark_write(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);  // Make sure we do not go past end of cache.
    fblock_entry_arr[idx].status |= FBLOCK_A | FBLOCK_D;
}

// Acquire the lock for accessing the file block at index IDX in the file block
// cache.  Note this will block until the lock is acquired.
void fblock_lock_acquire(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);
    lock_acquire(&(fblock_entry_arr[idx].in_use));
}

// Release the lock for accessing the file block at index IDX in the file block
// cache.
void fblock_lock_release(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);
    lock_release(&(fblock_entry_arr[idx].in_use));
}

// Returns true if the lock for accessing the file block at index IDX in the
// file block cache is held by the current thread.
bool fblock_lock_owner(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);
    return lock_held_by_current_thread(&(fblock_entry_arr[idx].in_use));
}

// Adds a user to the count of users who have the file block at index IDX in the
// file block cache open.
void fblock_add_user(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);
    lock_acquire(&(fblock_entry_arr[idx].in_use));
    fblock_entry_arr[idx].num_users += 1;
    lock_release(&(fblock_entry_arr[idx].in_use));
}

// Decrement the count of users who have the file block at index IDX in the file
// block cache open.
void fblock_rm_user(uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);
    lock_acquire(&(fblock_entry_arr[idx].in_use));
    ASSERT(fblock_entry_arr[idx].num_users > 0);
    fblock_entry_arr[idx].num_users -= 1;
    lock_release(&(fblock_entry_arr[idx].in_use));
}

// Check if a file location is already in the cache and return the index to the
// block if it is present.  Otherwise, return -1.
uint32_t fblock_is_cached(block_sector_t inumber, off_t offset)
{
    off_t start = offset & ~(BLOCK_SECTOR_SIZE-1);
    uint32_t i;
    // Go through cache, looking for a match.
    for (i = 0; i < NUM_FBLOCKS; ++i)
    {
        struct fblock_entry *e = &(fblock_entry_arr[i]);
        if (e->inumber == inumber && e->start == start) {
            ASSERT(fblock_is_used(e->status)); // Ensure block is in use
            return i;
        }
    }
    return -1;
}

// Check if a cache block is owned by the given inumber.
bool fblock_cache_owned(block_sector_t inumber, uint32_t idx)
{
    ASSERT(idx < NUM_FBLOCKS);
    return (fblock_entry_arr[idx].inumber == inumber);
}

// This function runs the background tasks for the cache system, periodically
// writting back all data in the file block cache back to disk.
void fballoc_background(void * aux UNUSED)
{
    while (true) {
        // Every second, wake up and write entire block cache back to disk.
        timer_msleep(1000);
        fballoc_write_all();
    }
}
