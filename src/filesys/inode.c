#include "inode.h"
#include "fballoc.h"
#include "filesys.h"
#include "free-map.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "devices/block.h"
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
    struct lock in_use;                 /*!< Lock for marking inode in use. */
    struct lock extending;              /*!< Lock for extending files. */
    #endif
};

/* Acquire and release lock on an inode in_use */
void inode_in_use_acquire(struct inode *inode)
{
    lock_acquire(&(inode->in_use));
}
void inode_in_use_release(struct inode *inode)
{
    lock_release(&(inode->in_use));
}
bool inode_in_use_owner(struct inode *inode)
{
    return lock_held_by_current_thread(&(inode->in_use));
}

/*! Returns the block device sector that contains byte offset POS
    within INODE.
    Returns -1 if INODE does not contain data for a byte at offset
    POS. */
block_sector_t byte_to_sector(struct inode *inode, off_t pos) {

    off_t cache_idx1, cache_idx2, cache_idx3, cache_idx4;
    off_t dbl_table_idx;
    struct inode_disk *direct_data;
    struct inode_disk_fs *indirect_data;
    size_t num_sectors;
    block_sector_t *sector_tbl;
    block_sector_t sector;

    ASSERT(inode != NULL);

    if (pos < inode->length) {
        /* Load the direct sector table. */
        cache_idx1 = inode_get_cache_block_idx(inode, DIRECT_BLOCK_OFFSET, inode->sector);
        fblock_add_user(cache_idx1);
        direct_data = (struct inode_disk *) fballoc_idx_to_addr(cache_idx1);
        sector_tbl = direct_data->sector_list;
        fblock_mark_read(cache_idx1);

        /* Find how many sectors the position would be. */
        num_sectors = pos / BLOCK_SECTOR_SIZE;

        /* Check if it is a direct sector. */
        if (num_sectors < NUM_DIRECT_FILE_SECTOR) {
            sector = sector_tbl[num_sectors];
            fblock_mark_read(cache_idx1);
            fblock_rm_user(cache_idx1);
        }
        /* Check if it is a single indirect sector. */
        else if (num_sectors < NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR) {
            /* Load the indirect sector table. */
            cache_idx2 = inode_get_cache_block_idx(inode, INDIRECT_BLOCK_OFFSET, sector_tbl[INDIRECT_ENTRY_IDX]);
            fblock_mark_read(cache_idx1);
            fblock_rm_user(cache_idx1);
            fblock_add_user(cache_idx2);
            indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx2);
            sector_tbl = indirect_data->sector_list;
            fblock_mark_read(cache_idx2);

            /* Get the sector. */
            sector = sector_tbl[num_sectors - NUM_DIRECT_FILE_SECTOR];
            fblock_mark_read(cache_idx2);
            fblock_rm_user(cache_idx2);
        }
        /* Check if it is a double indirect sector. */
        else {
            /* Remap sector number to start from 0 */
            num_sectors -= NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR;
            /* Load the 1st double indirect table. */
            cache_idx3 = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET, sector_tbl[DBL_INDIRECT_ENTRY_IDX]);
            fblock_mark_read(cache_idx1);
            fblock_rm_user(cache_idx1);
            fblock_add_user(cache_idx3);
            indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx3);
            sector_tbl = indirect_data->sector_list;
            fblock_mark_read(cache_idx3);

            /* Determine which nested indirect table to use. */
            dbl_table_idx = num_sectors / NUM_INDIRECT_FILE_SECTOR;

            /* Load the nested indirect table. */
            cache_idx4 = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET + (dbl_table_idx + 1)*BLOCK_SECTOR_SIZE, sector_tbl[dbl_table_idx]);
            fblock_mark_read(cache_idx3);
            fblock_rm_user(cache_idx3);
            fblock_add_user(cache_idx4);
            indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx4);
            sector_tbl = indirect_data->sector_list;
            fblock_mark_read(cache_idx4);

            /* Get the sector. */
            sector = sector_tbl[num_sectors % NUM_INDIRECT_FILE_SECTOR];
            fblock_mark_read(cache_idx4);
            fblock_rm_user(cache_idx4);
        }
        return sector;
    }
    return -1;
}

/*! Adds a sector to an inode, returning true if successful, and false
    otherwise.  Note that length is externally synched. */
static bool inode_add_sector(struct inode * inode, off_t length) {
    off_t cache_idx1, cache_idx2, cache_idx3, cache_idx4;
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
    if (length == 0) {
        num_sectors = 0;
    } else {
        num_sectors = (length-1) / BLOCK_SECTOR_SIZE + 1;
    }
    /* Load the direct sector table. */
    cache_idx1 = inode_get_cache_block_idx(inode, DIRECT_BLOCK_OFFSET, inode->sector);
    fblock_add_user(cache_idx1);
    direct_data = (struct inode_disk *) fballoc_idx_to_addr(cache_idx1);
    sector_tbl = direct_data->sector_list;
    fblock_mark_read(cache_idx1);

    /* Check if it is a direct sector. */
    if (num_sectors < NUM_DIRECT_FILE_SECTOR) {
        sector_tbl[num_sectors] = sector;
        fblock_mark_write(cache_idx1);
        fblock_rm_user(cache_idx1);
    }
    /* Check if it is a single indirect sector. */
    else if (num_sectors < NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR) {
        /* Check if need to create the indirect sector */
        if (num_sectors == NUM_DIRECT_FILE_SECTOR) {
            block_sector_t meta_sector = 0;
            if (!free_map_allocate(1, &meta_sector)) {
                fblock_rm_user(cache_idx1);
                return false;
            }
            block_write(fs_device, meta_sector, zeros);
            sector_tbl[INDIRECT_ENTRY_IDX] = meta_sector;
            fblock_mark_write(cache_idx1);
        }

        /* Load the indirect sector table. */
        cache_idx2 = inode_get_cache_block_idx(inode, INDIRECT_BLOCK_OFFSET, sector_tbl[INDIRECT_ENTRY_IDX]);
        fblock_mark_read(cache_idx1);
        fblock_rm_user(cache_idx1);
        fblock_add_user(cache_idx2);
        indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx2);

        /* Set the sector. */
        indirect_data->sector_list[num_sectors - NUM_DIRECT_FILE_SECTOR] = sector;
        fblock_mark_write(cache_idx2);
        fblock_rm_user(cache_idx2);
    }
    /* Check if it is a double indirect sector. */
    else {
        /* Remap sector number to start from 0 */
        num_sectors -= NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR;

        /* Check if need to create the double indirect sector */
        if (num_sectors == 0) {
            block_sector_t meta_sector = 0;
            if (!free_map_allocate(1, &meta_sector)) {
                fblock_rm_user(cache_idx1);
                return false;
            }
            block_write(fs_device, meta_sector, zeros);
            sector_tbl[DBL_INDIRECT_ENTRY_IDX] = meta_sector;
            fblock_mark_write(cache_idx1);
        }

        /* Load the 1st double indirect table. */
        cache_idx3 = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET, sector_tbl[DBL_INDIRECT_ENTRY_IDX]);
        fblock_mark_read(cache_idx1);
        fblock_rm_user(cache_idx1);
        fblock_add_user(cache_idx3);
        indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx3);

        /* Determine which nested indirect table to use. */
        dbl_table_idx = num_sectors / NUM_INDIRECT_FILE_SECTOR;

        /* Check if need to create the double indirect sub-sector */
        if (num_sectors % NUM_INDIRECT_FILE_SECTOR == 0) {
            block_sector_t meta_sector = 0;
            if (!free_map_allocate(1, &meta_sector)) {
                fblock_rm_user(cache_idx3);
                return false;
            }
            block_write(fs_device, meta_sector, zeros);
            indirect_data->sector_list[dbl_table_idx] = meta_sector;
            fblock_mark_write(cache_idx3);
        }

        /* Load the nested indirect table. */
        cache_idx4 = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET + (dbl_table_idx + 1)*BLOCK_SECTOR_SIZE, indirect_data->sector_list[dbl_table_idx]);
        fblock_mark_read(cache_idx3);
        fblock_rm_user(cache_idx3);
        fblock_add_user(cache_idx4);
        indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx4);

        /* Set the sector. */
        indirect_data->sector_list[num_sectors % NUM_INDIRECT_FILE_SECTOR] = sector;
        fblock_mark_write(cache_idx4);
        fblock_rm_user(cache_idx4);
    }
    return true;
}

/*! Removes a sector from an inode.  Note that length is externally synched. */
static void inode_remove_sector(struct inode * inode) {
    off_t cache_idx1, cache_idx2, cache_idx3, cache_idx4;
    off_t dbl_table_idx;
    struct inode_disk *direct_data;
    struct inode_disk_fs *indirect_data;
    size_t num_sectors;
    block_sector_t *sector_tbl;
    block_sector_t sector = 0;

    ASSERT(inode != NULL);

    /* Add into meta data */
    /* Check if file has length of 0 and do nothing */
    if (inode->length == 0) {
        return;
    } else {
        num_sectors = (inode->length-1) / BLOCK_SECTOR_SIZE;
    }
    /* Load the direct sector table. */
    cache_idx1 = inode_get_cache_block_idx(inode, DIRECT_BLOCK_OFFSET, inode->sector);
    fblock_add_user(cache_idx1);
    direct_data = (struct inode_disk *) fballoc_idx_to_addr(cache_idx1);
    sector_tbl = direct_data->sector_list;
    fblock_mark_read(cache_idx1);

    /* Check if it is a direct sector. */
    if (num_sectors < NUM_DIRECT_FILE_SECTOR) {
        sector = sector_tbl[num_sectors];
        fblock_mark_read(cache_idx1);
        fblock_rm_user(cache_idx1);
    }
    /* Check if it is a single indirect sector. */
    else if (num_sectors < NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR) {
        /* Load the indirect sector table. */
        cache_idx2 = inode_get_cache_block_idx(inode, INDIRECT_BLOCK_OFFSET, sector_tbl[INDIRECT_ENTRY_IDX]);
        fblock_mark_read(cache_idx1);
        fblock_add_user(cache_idx2);
        indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx2);

        /* Get the sector. */
        sector = indirect_data->sector_list[num_sectors - NUM_DIRECT_FILE_SECTOR];
        fblock_mark_read(cache_idx2);

        /* Check if need to remove the indirect sector */
        if (num_sectors == NUM_DIRECT_FILE_SECTOR) {
            free_map_release(sector_tbl[INDIRECT_ENTRY_IDX], 1);
            fblock_mark_read(cache_idx1);
        }
        
        fblock_rm_user(cache_idx1);
        fblock_rm_user(cache_idx2);
    }
    /* Check if it is a double indirect sector. */
    else {
        /* Remap sector number to start from 0 */
        num_sectors -= NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR;

        /* Load the 1st double indirect table. */
        cache_idx3 = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET, sector_tbl[DBL_INDIRECT_ENTRY_IDX]);
        fblock_mark_read(cache_idx1);
        fblock_add_user(cache_idx3);
        indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx3);
        fblock_mark_read(cache_idx3);

        /* Check if need to remove the double indirect sector */
        if (num_sectors == 0) {
            free_map_release(sector_tbl[DBL_INDIRECT_ENTRY_IDX], 1);
            fblock_mark_read(cache_idx1);
        }
        fblock_rm_user(cache_idx1);

        /* Determine which nested indirect table to use. */
        sector_tbl = indirect_data->sector_list;
        fblock_mark_read(cache_idx3);
        dbl_table_idx = num_sectors / NUM_INDIRECT_FILE_SECTOR;

        /* Load the nested indirect table. */
        cache_idx4 = inode_get_cache_block_idx(inode, DBL_INDIRECT_BLOCK_OFFSET + (dbl_table_idx + 1)*BLOCK_SECTOR_SIZE, sector_tbl[dbl_table_idx]);
        fblock_mark_read(cache_idx3);
        fblock_rm_user(cache_idx3);
        fblock_add_user(cache_idx4);
        indirect_data = (struct inode_disk_fs *) fballoc_idx_to_addr(cache_idx4);
        
        /* Check if need to remove the double indirect sub-sector */
        if (num_sectors % NUM_INDIRECT_FILE_SECTOR == 0) {
            free_map_release(sector_tbl[dbl_table_idx], 1);
            fblock_mark_read(cache_idx3);
        }

        /* Get the sector. */
        sector = indirect_data->sector_list[num_sectors % NUM_INDIRECT_FILE_SECTOR];
        fblock_mark_read(cache_idx4);
        fblock_rm_user(cache_idx4);
    }
    free_map_release(sector, 1);
}

/*! Returns the length of INODE from the disk. */
static off_t length_from_disk(struct inode * inode) {
    uint32_t idx = inode_get_cache_block_idx(inode, DIRECT_BLOCK_OFFSET, inode->sector);
    fblock_add_user(idx);
    struct inode_disk * disk = (struct inode_disk *) fballoc_idx_to_addr(idx);
    off_t length = disk->length;
    fblock_mark_read(idx);
    fblock_rm_user(idx);
    return length;
}

/*! update the length of INODE. */
static void length_set_on_disk(struct inode * inode, off_t length) {
    uint32_t idx = inode_get_cache_block_idx(inode, DIRECT_BLOCK_OFFSET, inode->sector);
    fblock_add_user(idx);
    struct inode_disk * disk = (struct inode_disk *) fballoc_idx_to_addr(idx);
    fblock_mark_write(idx);
    disk->length = length;
    fblock_rm_user(idx);
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
bool inode_create(block_sector_t direct_sector, off_t length) {
    off_t dbl_table_idx;
    struct inode_disk *direct_data = calloc(1, sizeof(struct inode_disk));
    struct inode_disk_fs *indirect_data = NULL;
    struct inode_disk_fs *dbl_indirect_data = NULL;
    struct inode_disk_fs *dbl_indirect_sub_data = NULL;
    block_sector_t sector = 0;

    bool success = false;
    size_t i, j;

    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
       one sector in size, and you should fix that. */
    ASSERT(sizeof(struct inode_disk) == BLOCK_SECTOR_SIZE);
    ASSERT(sizeof(struct inode_disk_fs) == BLOCK_SECTOR_SIZE);

    if (direct_data != NULL) {
        size_t sectors = bytes_to_sectors(length);
        direct_data->length = length;
        direct_data->magic = INODE_MAGIC;
        for (i = 0; i < sectors; i++) {
            /* Allocation failed, clean up. */
            if (!free_map_allocate(1, &sector)) {
                goto clean_up;
            }
            block_write(fs_device, sector, zeros);

            /* Add into meta data */
            /* Check if it is a direct sector. */
            if (i < NUM_DIRECT_FILE_SECTOR) {
                direct_data->sector_list[i] = sector;
            }
            /* Check if it is a single indirect sector. */
            else if (i < NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR) {
                /* Check if need to create the indirect sector */
                if (i == NUM_DIRECT_FILE_SECTOR) {
                    ASSERT(indirect_data == NULL);
                    indirect_data = calloc(1, sizeof(struct inode_disk_fs));
                    /* Allocation failed, clean up. */
                    if (indirect_data == NULL) {
                        goto clean_up;
                    }
                    block_sector_t meta_sector = 0;
                    /* Allocation failed, clean up. */
                    if (!free_map_allocate(1, &meta_sector)) {
                        goto clean_up;
                    }
                    direct_data->sector_list[INDIRECT_ENTRY_IDX] = meta_sector;
                }
                /* Set the sector. */
                indirect_data->sector_list[i - NUM_DIRECT_FILE_SECTOR] = sector;
            }
            /* Check if it is a double indirect sector. */
            else {
                /* Remap sector number to start from 0 */
                j = i - (NUM_DIRECT_FILE_SECTOR + NUM_INDIRECT_FILE_SECTOR);

                /* Check if need to create the double indirect sector */
                if (j == 0) {
                    ASSERT(dbl_indirect_data == NULL);
                    dbl_indirect_data = calloc(1, sizeof(struct inode_disk_fs));
                    /* Allocation failed, clean up. */
                    if (dbl_indirect_data == NULL) {
                        goto clean_up;
                    }
                    block_sector_t meta_sector = 0;
                    /* Allocation failed, clean up. */
                    if (!free_map_allocate(1, &meta_sector)) {
                        goto clean_up;
                    }
                    direct_data->sector_list[DBL_INDIRECT_ENTRY_IDX] = meta_sector;
                }

                /* Determine which nested indirect table to use. */
                dbl_table_idx = j / NUM_INDIRECT_FILE_SECTOR;

                /* Check if need to create the double indirect sub-sector */
                if (j % NUM_INDIRECT_FILE_SECTOR == 0) {
                    ASSERT(dbl_indirect_sub_data == NULL);
                    dbl_indirect_sub_data = calloc(1, sizeof(struct inode_disk_fs));
                    /* Allocation failed, clean up. */
                    if (dbl_indirect_sub_data == NULL) {
                        goto clean_up;
                    }
                    block_sector_t meta_sector = 0;
                    /* Allocation failed, clean up. */
                    if (!free_map_allocate(1, &meta_sector)) {
                        goto clean_up;
                    }
                    dbl_indirect_data->sector_list[dbl_table_idx] = meta_sector;
                }

                /* Set the sector. */
                dbl_indirect_sub_data->sector_list[j % NUM_INDIRECT_FILE_SECTOR] = sector;

                /* Check if need to write back double indirect sub-sector */
                if (j % NUM_INDIRECT_FILE_SECTOR == NUM_INDIRECT_FILE_SECTOR-1) {
                    block_write(fs_device, dbl_indirect_data->sector_list[dbl_table_idx], dbl_indirect_sub_data);
                    free(dbl_indirect_sub_data);
                }
            }
        }
        success = true;
    }

    /* Clean everything up. */
    clean_up:
    if (indirect_data != NULL) {
        /* Must write to disk if successful. */
        if (success)
            block_write(fs_device, direct_data->sector_list[INDIRECT_ENTRY_IDX], indirect_data);
        free(indirect_data);
    }
    if (dbl_indirect_data != NULL) {
        /* Must write to disk if successful. */
        if (success)
            block_write(fs_device, direct_data->sector_list[DBL_INDIRECT_ENTRY_IDX], dbl_indirect_data);
        free(dbl_indirect_data);
    }
    if (dbl_indirect_sub_data != NULL) {
        free(dbl_indirect_sub_data);
    }
    if (direct_data != NULL) {
        /* Must write to disk if successful. */
        if (success)
            block_write(fs_device, direct_sector, direct_data);
        free(direct_data);
    }
    return success;

}

/*! Reads an inode from SECTOR
    and returns a `struct inode' that contains it.
    Returns a null pointer if memory allocation fails. */
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
    inode->is_dir = false;
    lock_init(&(inode->in_use));
    lock_init(&(inode->extending));
    inode->length = length_from_disk(inode);

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
        /* Write back blocks to disk and free them */
        uint32_t i;
        for (i = 0; i < NUM_FBLOCKS; ++i) {
            if (fblock_cache_owned(inode_get_inumber(inode), i)) {
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

/*! Returns true if INODE has been removed, and false otherwise. */
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

    /* Ensure that user buffer is valid memory before getting cache blocks */
    if (size > 0) {
        void *trash = malloc(1);
        memcpy(buffer, (void *) trash, 1);
        memcpy(buffer+size-1, (void *) trash, 1);
        free(trash);
    }

    while (size > 0) {
        /* Starting byte offset within sector. */
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;
        
        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually read from this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;
            
        /* Sector to read */
        uint32_t block_idx = fblock_is_cached(inode_get_inumber(inode), offset);
        if (block_idx == (uint32_t) -1) {
            uint32_t sector = byte_to_sector(inode, offset);
            /* Get the pointer to the cache block containing file sector to read. */
            block_idx = inode_get_cache_block_idx(inode, offset, sector);
        }
        void *cache_block = fballoc_idx_to_addr(block_idx);

        /* Read the chunk from the cache block. */
        fblock_add_user(block_idx);
        memcpy(buffer + bytes_read, cache_block + sector_ofs, chunk_size);
        fblock_mark_read(block_idx);
        fblock_rm_user(block_idx);

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

    /* If writes are denied, don't write. */
    if (inode->deny_write_cnt) {
        return 0;
    }
    
    /* Ensure that user buffer is valid memory before getting cache blocks */
    if (size > 0) {
        void *trash = malloc(1);
        memcpy((void *) trash, buffer, 1);
        memcpy((void *) trash, buffer+size-1, 1);
        free(trash);
    }
    
    /* If new file is extended, need to update length */
    if (offset + size > inode->length) {
        /* Acquire lock to extend file. */
        lock_acquire(&inode->extending);

        /* Get how many file sectors long the file cucqently is. */
        curr_file_len = bytes_to_sectors(inode->length);

        /* Get how many file sectors long file must be to complete write. */
        ext_file_len = bytes_to_sectors(offset + size);

        /* Determine how many more file sectors must be allocated. */
        num_sec_to_alloc = ext_file_len - curr_file_len;

        /* If more sectors must be allocated, allocate. */
        for (i = 0; i < num_sec_to_alloc; i++) {
            inode_add_sector(inode, inode->length + BLOCK_SECTOR_SIZE * i);
        }

        length_set_on_disk(inode, offset + size);
        inode->length = offset + size;

        /* NOTE: do not need to put last block into cache, it will get loaded by
         * the write below. */
         
        /* Done extending, release the lock. */
        lock_release(&inode->extending);
    }

    while (size > 0) {
        /* starting byte offset within sector. */
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;
        
        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0)
            break;
            
        /* Sector to write */
        uint32_t block_idx = fblock_is_cached(inode_get_inumber(inode), offset);
        if (block_idx == (uint32_t) -1) {
            uint32_t sector = byte_to_sector(inode, offset);
            /* Get the pointer to the cache block containing file sector to write. */
            block_idx = inode_get_cache_block_idx(inode, offset, sector);
        }
        void *cache_block = fballoc_idx_to_addr(block_idx);

        /* Write chunk to the cache block */
        fblock_add_user(block_idx);
        memcpy(cache_block + sector_ofs, buffer + bytes_written, chunk_size);
        fblock_mark_write(block_idx);
        fblock_rm_user(block_idx);

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

/* Get the index of a block in cache, or load and then return the index */
uint32_t inode_get_cache_block_idx(struct inode *inode, off_t offset, block_sector_t sector) {
    /* If it is not present, need to pull it from disk into the cache. */
    uint32_t idx = fblock_is_cached(inode_get_inumber(inode), offset);
    if (idx == (uint32_t) -1) {
        idx = fballoc_load_fblock(inode_get_inumber(inode), offset, sector);
    }
    /* Get the block index into the cache */
    return idx;
}

/* Force all inodes to close */
void inode_force_close_all(void) {
    while (!list_empty(&open_inodes)) {
        struct inode *inode = list_entry(list_begin(&open_inodes), struct inode, elem);
        inode_close(inode);
    }
}
