#include "inode.h"
#include "fballoc.h"
#include "file_sector.h"
#include "filesys.h"
#include "free-map.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "threads/malloc.h"

/*! Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
static char zeros[BLOCK_SECTOR_SIZE];

/*! On-disk inode. Must be exactly BLOCK_SECTOR_SIZE bytes long, done by
    modifying NUM_DIRECT_FILE_SECTOR */
struct inode_disk {
    off_t length;                       /*!< File size in bytes. */
    file_sector sector_list[NUM_DIRECT_FILE_SECTOR + 2];  /*!< List of file sectors. */
    unsigned magic;                     /*!< Magic number. */
};
/*! On-disk inode with only file sectors. Must be exactly BLOCK_SECTOR_SIZE
    bytes long, done by modifying NUM_INDIRECT_FILE_SECTOR */
struct inode_disk_fs {
    file_sector sector_list[NUM_INDIRECT_FILE_SECTOR];  /*!< List of file sectors. */
};

/*! Returns the number of sectors to allocate for an inode SIZE
    bytes long. */
static inline size_t bytes_to_sectors(off_t size) {
    return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/*! In-memory inode. */
struct inode {
    struct list_elem elem;              /*!< Element in inode list. */
    block_sector_t sector;              /*!< Sector number of disk location. */
    int open_cnt;                       /*!< Number of openers. */
    bool removed;                       /*!< True if deleted, false otherwise. */
    int deny_write_cnt;                 /*!< 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /*!< Inode content. */
    struct inode_disk_fs data2;         /*!< Inode content for indirection. */

    #ifdef FILESYS
    // TODO: note the below is a bitmap
    // NEED TO INITIALIZE and DESTROY
    uint8_t blocks_owned[16];           /*!< Blocks in cache owned. */
    struct lock extending;              /*!< Lock for extending files. */
    struct lock loading_to_cache;       /*!< Lock for loading into block cache. */
    //file_sector *file_sectors;          /*!< Array of file sectors. */
    // ^14 words?

    #endif
};

/*! Returns the block device sector that contains byte offset POS
    within INODE.
    Returns -1 if INODE does not contain data for a byte at offset
    POS. */
file_sector* byte_to_sector_ptr(struct inode *inode, off_t pos) {
    ASSERT(inode != NULL);
    // TODO: read inode structures correctly

    off_t fs_idx = pos / BLOCK_SECTOR_SIZE;

    // If direct, then just get file sector
    if (fs_idx < NUM_DIRECT_FILE_SECTOR) {
        return &((inode->data.sector_list)[fs_idx]);
    }
    else if (fs_idx < NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR) {
        return &((inode->data2.sector_list)[fs_idx - NUM_DIRECT_FILE_SECTOR]);
    }
    else {
        // TODO: DOUBLE INDIRECT CASE
        PANIC("byte_to_sector_ptr: Sorry, double indirection is not implemented");
    }
    return &((inode->data.sector_list)[0]);
}

block_sector_t byte_to_sector(struct inode *inode, off_t pos) {
    ASSERT(inode != NULL);
    if (pos < inode->data.length)
    {
        file_sector *sec = byte_to_sector_ptr(inode, pos);
        return file_sec_get_addr(*sec);
    } else {
        return -1;
    }
}

/*! List of open inodes, so that opening a single inode twice
    returns the same `struct inode'. */
static struct list open_inodes;

/*! Initializes the inode module. */
void inode_init(void) {
    list_init(&open_inodes);
}

/*! Initializes an inode with LENGTH bytes of data and
    writes the new inode to sector SECTOR on the file system
    device.
    Returns true if successful.
    Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length) {
    struct inode_disk *disk_inode = NULL;
    struct inode_disk_fs *disk_inode_fs = NULL;
    bool success = false;
    file_sector *sector_list = NULL;

    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);
    ASSERT(sizeof *disk_inode_fs == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    disk_inode_fs = calloc(1, sizeof *disk_inode_fs);
    if (disk_inode != NULL && disk_inode_fs != NULL) {
        size_t sectors = bytes_to_sectors(length);
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;

        /* Allocate sector for indirection layer */
        if (!free_map_allocate(1, &(disk_inode->sector_list[NUM_DIRECT_FILE_SECTOR]))) {
            free(disk_inode);
            free(disk_inode_fs);
            return false;
        }

        if (sectors > 0) {
            size_t i, j, sec = 0;
            for (i = 0; i < sectors; i++) {

                /* Select correct file_sector list */
                if (sectors < NUM_DIRECT_FILE_SECTOR) {
                    sector_list = disk_inode->sector_list;
                    sec = i;
                }
                else if (sectors < NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR) {
                    sector_list = disk_inode_fs->sector_list;
                    sec = i - NUM_DIRECT_FILE_SECTOR;
                }
                else { // TODO: need to handle large files correctly
                    PANIC("inode_create: Sorry, double indirection is not implemented");
                }
                /* Allocate blocks */
                if (free_map_allocate(1, &(sector_list[sec]))) {
                    block_write(fs_device, sector_list[sec], zeros);
                }
                else {
                    /* Clean up if previous failed */
                    for (j = 0; j < i; j++) {
                        /* Select correct file_sector list */
                        if (sectors < NUM_DIRECT_FILE_SECTOR) {
                            sector_list = disk_inode->sector_list;
                            sec = j;
                        }
                        else if (sectors < NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR) {
                            sector_list = disk_inode_fs->sector_list;
                            sec = j - NUM_DIRECT_FILE_SECTOR;
                        }
                        else { // TODO: need to handle large files correctly
                            PANIC("inode_create: Sorry, double indirection is not implemented");
                        }
                        free_map_release(sector_list[sec], 1);
                    }
                    free_map_release(disk_inode->sector_list[NUM_DIRECT_FILE_SECTOR], 1);
                    free(disk_inode);
                    free(disk_inode_fs);
                    return false;
                }
            }
        }
        /* After the entire file is written to disk, write meta data. */
        block_write(fs_device, sector, disk_inode);
        block_write(fs_device, disk_inode->sector_list[NUM_DIRECT_FILE_SECTOR], disk_inode_fs);
        success = true;
    }
    if (disk_inode != NULL) {
        free(disk_inode);
    }
    if (disk_inode_fs != NULL) {
        free(disk_inode_fs);
    }

    return success;
}

/*! Reads an inode from SECTOR
    and returns a `struct inode' that contains it.
    Returns a null pointer if memory allocation fails. */
// TODO: UPDATE TO PULL FIRST BLCOK OF FILE INTO CACHE
struct inode * inode_open(block_sector_t sector) {
    struct list_elem *e;
    struct inode *inode;

    /* Check whether this inode is already open. */
    for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
         e = list_next(e)) {
        inode = list_entry(e, struct inode, elem);
        if (inode->sector == sector) {
            inode_reopen(inode);
            return inode;
        }
    }

    /* Allocate memory. */
    inode = malloc(sizeof *inode);
    if (inode == NULL)
        return NULL;

    /* Initialize. */
    list_push_front(&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    block_read(fs_device, inode->sector, &inode->data);
    block_read(fs_device, inode->data.sector_list[NUM_DIRECT_FILE_SECTOR], &inode->data2);
    bitmap_create_in_buf(NUM_FBLOCKS, (void *) inode->blocks_owned, 16);
    lock_init(&(inode->extending));
    lock_init(&(inode->loading_to_cache));

    return inode;
}

/*! Reopens and returns INODE. */
struct inode * inode_reopen(struct inode *inode) {
    if (inode != NULL)
        inode->open_cnt++;
    return inode;
}

/*! Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode *inode) {
    return inode->sector;
}

/*! Closes INODE and writes it to disk.
    If this was the last reference to INODE, frees its memory.
    If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode *inode) {
    /* Ignore null pointer. */
    if (inode == NULL)
        return;

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0) {
        // TODO: need to lock file from being touched by others
        /* Write back blocks to disk and free them */
        uint32_t i;
        for (i = 0; i < NUM_FBLOCKS; ++i) {
            if (inode_is_block_owned(inode, i)) {
                fballoc_free_fblock(i);
            }
        }
        /* Write back inode to disk */
        block_write(fs_device, inode->data.sector_list[NUM_DIRECT_FILE_SECTOR], &inode->data2);
        block_write(fs_device, inode->sector, &inode->data);

        /* Remove from inode list and release lock. */
        list_remove(&inode->elem);

        /* Deallocate blocks if removed. */
        if (inode->removed) {
            free_map_release(inode->sector, 1);
            free_map_release(inode->data.sector_list[NUM_DIRECT_FILE_SECTOR], 1);
            size_t sectors = bytes_to_sectors(inode->data.length);
            size_t i;
            for (i = 0; i < sectors; i++) {
                file_sector *sec = byte_to_sector_ptr(inode, i * BLOCK_SECTOR_SIZE);
                free_map_release(file_sec_get_addr(*sec), 1);
            }
        }
        free(inode);
    }
}

/*! Marks INODE to be deleted when it is closed by the last caller who
    has it open. */
void inode_remove(struct inode *inode) {
    ASSERT(inode != NULL);
    inode->removed = true;
}

/*! Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset) {
    uint8_t *buffer = buffer_;
    off_t bytes_read = 0;

    // TODO: need to check length

    while (size > 0) {
        /* Disk sector to read, starting byte offset within sector. */
        uint32_t *sector = byte_to_sector_ptr(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        /* Get the pointer to the cache block containing file sector to write. */
        uint32_t block_idx = inode_get_cache_block_idx(inode, offset, sector);
        void *cache_block = fballoc_idx_to_addr(block_idx);

        /* Read the chunk from the cache block. */
        fblock_lock_acquire(block_idx);
        memcpy(buffer + bytes_read, cache_block + sector_ofs, chunk_size);
        fblock_mark_read(block_idx);
        fblock_lock_release(block_idx);

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
    }

    return bytes_read;
}

/*! Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
    Returns the number of bytes actually written, which may be
    less than SIZE if end of file is reached or an error occurs.
    (Normally a write at end of file would extend the inode, but
    growth is not yet implemented.) */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size, off_t offset) {
    const uint8_t *buffer = buffer_;
    off_t bytes_written = 0;
    int i;
    off_t curr_file_len;
    off_t ext_file_len;
    off_t num_sec_to_alloc;
    file_sector *curr_sector;

    // If new file is extended, need to update length
    if (offset + size > (inode->data).length) {

        /* Get how many file sectors long the file currently is. */
        curr_file_len = bytes_to_sectors((inode->data).length);

        /* Get how many file sectors long file must be to complete write. */
        ext_file_len = bytes_to_sectors(offset + size);

        /* Determine how many more file sectors must be allocated. */
        num_sec_to_alloc = ext_file_len - curr_file_len;

        (inode->data).length = offset + size;

        /* If more sectors must be allocated, allocate and then write 0's to it. */
        for (i = 0; i < num_sec_to_alloc; i++) {
            curr_sector = byte_to_sector_ptr(inode, BLOCK_SECTOR_SIZE*(i + curr_file_len + 1));
            /* Allocate sector on disk. */
            free_map_allocate(1, curr_sector);

            /* Zero the sector. */
            block_write(fs_device, *curr_sector, zeros);
        }
        /* NOTE: do not need to put last block into cache, it will get loaded by
         * the write below. */
    }

    if (inode->deny_write_cnt)
        return 0;
    while (size > 0) {
        /* Sector to write, starting byte offset within sector. */
        uint32_t *sector = byte_to_sector_ptr(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;

        /* Get the pointer to the cache block containing file sector to write. */
        uint32_t block_idx = inode_get_cache_block_idx(inode, offset, sector);
        void *cache_block = fballoc_idx_to_addr(block_idx);

        /* Write chunk to the cache block */
        fblock_lock_acquire(block_idx);
        memcpy(cache_block + sector_ofs, buffer + bytes_written, chunk_size);
        fblock_mark_write(block_idx);
        fblock_lock_release(block_idx);

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;
    }

    return bytes_written;
}

/*! Disables writes to INODE.
    May be called at most once per inode opener. */
void inode_deny_write (struct inode *inode) {
    inode->deny_write_cnt++;
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/*! Re-enables writes to INODE.
    Must be called once by each inode opener who has called
    inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write (struct inode *inode) {
    ASSERT(inode->deny_write_cnt > 0);
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
}

/*! Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode *inode) {
    return inode->data.length;
}


// TODO:

    // Write a "is_accessible() function based on locks?
    // Write a function to take ownership of blocks_owned
    // Write a function to check if any blocks are owned
    // Write a function to check if a specific block is owned?
    // Write a function to give up ownership of blocks

// MIGHT WANT TO CHANGE THESE NAMES
void inode_get_block(struct inode *inode, size_t block_num) {
    bitmap_mark((struct bitmap *) inode->blocks_owned, block_num);
}

void inode_release_block(struct inode *inode, size_t block_num) {
    bitmap_reset((struct bitmap *) inode->blocks_owned, block_num);
}

bool inode_is_block_owned(struct inode *inode, size_t block_num) {
    return bitmap_test((struct bitmap *) inode->blocks_owned, block_num);
}

uint32_t inode_get_cache_block_idx(struct inode *inode, off_t offset, file_sector *sector) {
    // If it is not present, need to pull it from disk into the cache.
    if (!file_sec_is_present(*sector)) {
        fballoc_load_fblock(inode, offset, sector);
    }

    // Get the block index into the cache
    return file_sec_get_block_idx(*sector);
}


// TODO: file extension (in write function)
// Write all new blocks directly to disk (bypass cache), then load
// the correct block into cache from disk
