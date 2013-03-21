#include "inode.h"
#include "fballoc.h"
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
    block_sector_t sector_list[NUM_DIRECT_FILE_SECTOR + 2];  /*!< List of file sectors. */
    unsigned magic;                     /*!< Magic number. */
};
/*! On-disk inode with only file sectors. Must be exactly BLOCK_SECTOR_SIZE
    bytes long, done by modifying NUM_INDIRECT_FILE_SECTOR */
struct inode_disk_fs {
    block_sector_t sector_list[NUM_INDIRECT_FILE_SECTOR];  /*!< List of file sectors. */
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
    off_t length;                       /*!< File size in bytes. */
    int open_cnt;                       /*!< Number of openers. */
    bool removed;                       /*!< True if deleted, false otherwise. */
    bool is_dir;                        /*!< True if directory, false otherwise. */
    int deny_write_cnt;                 /*!< 0: writes ok, >0: deny writes. */

    #ifdef FILESYS
    // TODO: note the below is a bitmap
    // NEED TO INITIALIZE and DESTROY
    uint8_t blocks_owned[16];           /*!< Blocks in cache owned. */
    struct lock extending;              /*!< Lock for extending files. */
    struct lock loading_to_cache;       /*!< Lock for loading into block cache. */

    #endif
};

/*! Returns the block device sector that contains byte offset POS
    within INODE.
    Returns -1 if INODE does not contain data for a byte at offset
    POS. */
// add_sector
block_sector_t byte_to_sector(struct inode *inode, off_t pos) {

    off_t cache_idx;
    off_t dbl_table_idx;
    struct inode_disk *direct_data;
    struct inode_disk_fs *indirect_data;
    size_t num_sectors;
    block_sector_t *sector_tbl;
    block_sector_t sector;

    ASSERT(inode != NULL);

    if (pos < inode->length) {
        /* Load the direct sector table. */
        cache_idx = inode_get_cache_block_idx(inode, DIRECT_BLOCK_OFFSET, inode->sector);
        direct_data = (struct inode_disk *) fballoc_idx_to_addr(cache_idx);
        sector_tbl = direct_data->sector_list;

        /* Find how many sectors the position would be. */
        num_sectors = pos / BLOCK_SECTOR_SIZE;

        /* Check if it is a direct sector. */
        if (num_sectors < NUM_DIRECT_FILE_SECTOR) {
            sector = sector_tbl[num_sectors];
        }
        /* Check if it is a single indirect sector. */
        else if (num_sectors < NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR) {
            /* Load the indirect sector table. */
            cache_idx = inode_get_cache_block_idx(inode, INDIRECT_BLOCK_OFFSET, sector_tbl[INDIRECT_ENTRY_IDX]);
            indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx);
            sector_tbl = indirect_data->sector_list;

            /* Get the sector. */
            sector = sector_tbl[num_sectors - NUM_DIRECT_FILE_SECTOR];
        }
        /* Check if it is a double indirect sector. */
        else {
            /* Remap sector number to start from 0 */
            num_sectors -= NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR;
            /* Load the 1st double indirect table. */
            cache_idx = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET, sector_tbl[DBL_INDIRECT_ENTRY_IDX]);
            indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx);
            sector_tbl = indirect_data->sector_list;

            /* Determine which nested indirect table to use. */
            dbl_table_idx = num_sectors / NUM_INDIRECT_FILE_SECTOR;

            /* Load the nested indirect table. */
            cache_idx = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET + (dbl_table_idx + 1)*BLOCK_SECTOR_SIZE, sector_tbl[dbl_table_idx]);
            indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx);
            sector_tbl = indirect_data->sector_list;

            /* Get the sector. */
            sector = sector_tbl[num_sectors % BLOCK_SECTOR_SIZE];
        }
        return sector;
    }
    return -1;
}

/*! This function adds a sector to an inode, and returns it success status.
    Length is externally synched. */
static bool inode_add_sector(struct inode * inode) {
    off_t cache_idx;
    off_t dbl_table_idx;
    struct inode_disk *direct_data;
    struct inode_disk_fs *indirect_data;
    size_t num_sectors;
    block_sector_t *sector_tbl;
    block_sector_t sector = 0;
    
    ASSERT(inode != NULL);
    
    if (!free_map_allocate(1, &sector)) {
        return false;
    }
    block_write(fs_device, sector, zeros);
    
    /* Add into meta data */
    /* Check if file has length of 0 */
    if (inode->length == 0) {
        num_sectors = 0;
    } else {
        num_sectors = (inode->length-1) / BLOCK_SECTOR_SIZE + 1;
    }
    /* Load the direct sector table. */
    cache_idx = inode_get_cache_block_idx(inode, DIRECT_BLOCK_OFFSET, inode->sector);
    direct_data = (struct inode_disk *) fballoc_idx_to_addr(cache_idx);
    sector_tbl = direct_data->sector_list;

    /* Check if it is a direct sector. */
    if (num_sectors < NUM_DIRECT_FILE_SECTOR) {
        sector_tbl[num_sectors] = sector;
    }
    /* Check if it is a single indirect sector. */
    else if (num_sectors < NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR) {
        /* Check if need to create the indirect sector */
        if (num_sectors == NUM_DIRECT_FILE_SECTOR) {
            block_sector_t meta_sector = 0;
            if (free_map_allocate(1, &meta_sector)) {
                return false;
            }
            block_write(fs_device, meta_sector, zeros);
            sector_tbl[INDIRECT_ENTRY_IDX] = meta_sector;
        }
    
        /* Load the indirect sector table. */
        cache_idx = inode_get_cache_block_idx(inode, INDIRECT_BLOCK_OFFSET, sector_tbl[INDIRECT_ENTRY_IDX]);
        indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx);
        sector_tbl = indirect_data->sector_list;

        /* Get the sector. */
        sector_tbl[num_sectors - NUM_DIRECT_FILE_SECTOR] = sector;
    }
    /* Check if it is a double indirect sector. */
    else {
        /* Remap sector number to start from 0 */
        num_sectors -= NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR;
        /* Load the 1st double indirect table. */
        cache_idx = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET, sector_tbl[DBL_INDIRECT_ENTRY_IDX]);
        indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx);
        sector_tbl = indirect_data->sector_list;

        /* Determine which nested indirect table to use. */
        dbl_table_idx = num_sectors / NUM_INDIRECT_FILE_SECTOR;

        /* Load the nested indirect table. */
        cache_idx = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET + (dbl_table_idx + 1)*BLOCK_SECTOR_SIZE, sector_tbl[dbl_table_idx]);
        indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx);
        sector_tbl = indirect_data->sector_list;

        /* Get the sector. */
        sector_tbl[num_sectors % BLOCK_SECTOR_SIZE] = sector;
    }
    return sector;
    
}
/*! This function removes a sector from an inode, length is externally synched. */
static void inode_remove_sector(struct inode * inode) {
    block_sector_t sector = 0;
    free_map_release(sector, 1);
}
static off_t length_from_disk(struct inode * inode) {
    uint32_t idx = inode_get_cache_block_idx(inode, DIRECT_BLOCK_OFFSET, inode->sector);
    struct inode_disk * disk = (struct inode_disk *) fballoc_idx_to_addr(idx);
    return disk->length;
}
static void length_set_on_disk(struct inode * inode, off_t length) {
    uint32_t idx = inode_get_cache_block_idx(inode, DIRECT_BLOCK_OFFSET, inode->sector);
    struct inode_disk * disk = (struct inode_disk *) fballoc_idx_to_addr(idx);
    disk->length = length;
}


/*! List of open inodes, so that opening a single inode twice
    returns the same `struct inode'. */
static struct list open_inodes;

static struct inode_disk disk_zero;

/*! Initializes the inode module. */
void inode_init(void) {
    list_init(&open_inodes);
    disk_zero.magic = INODE_MAGIC;
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
    size_t i, j;

    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);
    ASSERT(sizeof *disk_inode_fs == BLOCK_SECTOR_SIZE);

    /* Create an empty file on the disk */
    block_write(fs_device, sector, &disk_zero);

    /* Open the empty file and zero it */
    struct inode *inode = inode_open(sector);
    if (inode != NULL) {
        size_t sectors = bytes_to_sectors(length);
        for (i = 0; i < sectors; i++) {
            if (!inode_add_sector(inode)) {
                for (j = 0; j < i; j++) {
                    inode_remove_sector(inode);
                    inode->length -= BLOCK_SECTOR_SIZE;
                }
                return false;
            }
            inode->length += BLOCK_SECTOR_SIZE;
        }
        inode->length = length; /* Just in case, inode should be deleted anyway */
        length_set_on_disk(inode, length);
        success = true;
    }
    inode_close(inode);

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
    inode->length = length_from_disk(inode);
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    inode->is_dir = false;
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

/*! Returns if INODE is a directory. */
bool inode_is_dir(const struct inode *inode) {
    return inode->is_dir;
}
/*! Sets an INODE to be a directory. */
void inode_set_dir(struct inode *inode) {
    inode->is_dir = true;
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

        /* Remove from inode list and release lock. */
        list_remove(&inode->elem);

        /* Deallocate blocks if removed. */
        if (inode->removed) {
            size_t sectors = bytes_to_sectors(inode->length);
            size_t i;
            for (i = 0; i < sectors; i++) {
                inode_remove_sector(inode);
                inode->length -= BLOCK_SECTOR_SIZE; /* Exact size unimportant */
            }
            free_map_release(inode->sector, 1);
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
bool inode_is_removed(struct inode *inode) {
    ASSERT(inode != NULL);
    return (inode->removed);
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
        uint32_t sector = byte_to_sector(inode, offset);
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

    if (inode->deny_write_cnt) {
        return 0;
    }

    // If new file is extended, need to update length
    if (offset + size > inode->length) {

        /* Get how many file sectors long the file currently is. */
        curr_file_len = bytes_to_sectors(inode->length);

        /* Get how many file sectors long file must be to complete write. */
        ext_file_len = bytes_to_sectors(offset + size);

        /* Determine how many more file sectors must be allocated. */
        num_sec_to_alloc = ext_file_len - curr_file_len;

        /* If more sectors must be allocated, allocate. */
        for (i = 0; i < num_sec_to_alloc; i++) {
            // TODO: need to check for success
            inode_add_sector(inode);
            inode->length += BLOCK_SECTOR_SIZE;
        }

        inode->length = offset + size;
        length_set_on_disk(inode, inode->length);

        /* NOTE: do not need to put last block into cache, it will get loaded by
         * the write below. */
    }

    while (size > 0) {
        /* Sector to write, starting byte offset within sector. */
        uint32_t sector = byte_to_sector(inode, offset);
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
    return inode->length;
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

uint32_t inode_get_cache_block_idx(struct inode *inode, off_t offset, block_sector_t sector) {
    // If it is not present, need to pull it from disk into the cache.
    uint32_t idx = fblock_is_cached(inode, offset);
    if (idx == (uint32_t) -1) {
        idx = fballoc_load_fblock(inode, offset, sector);
    }
    // Get the block index into the cache
    return idx;
}

/* Force all inodes to close */
void inode_force_close_all(void) {
    while (!list_empty(&open_inodes)) {
        struct inode *inode = list_entry(list_begin(&open_inodes), struct inode, elem);
        inode_close(inode);
    }
}
