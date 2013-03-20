#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

bool dir_add_obj(struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir);

/*! A directory. */
struct dir {
    struct inode *inode;                /*!< Backing store. */
    off_t pos;                          /*!< Current position. */
};

/*! A single directory entry. */
struct dir_entry {
    block_sector_t inode_sector;        /*!< Sector number of header. */
    char name[NAME_MAX + 1];            /*!< Null terminated file name. */
    bool in_use;                        /*!< In use or free? */
    bool is_dir;                        /*!< Is this a subdirectory. */
};

/*! Creates a directory with space for ENTRY_CNT entries in the
    given SECTOR.  Returns true if successful, false on failure. */
bool dir_create(block_sector_t sector, size_t entry_cnt, struct dir *parent) {
    if (inode_create(sector, entry_cnt * sizeof(struct dir_entry))) {
        struct dir_entry e;
        /* Open directory to write initial entries */
        struct inode *inode = inode_open(sector);
        if (inode == NULL) {
            return false;
        }
        inode_set_dir(inode);
        /* Write self directory to file */
        e.name[0] = '.'; e.name[1] = '\0';
        e.in_use = true;
        e.is_dir = true;
        e.inode_sector = sector;
        if (inode_write_at(inode, &e, sizeof(e), 0) != sizeof(e)) {
            inode_remove(inode);
            inode_close(inode);
            return false;
        }
        /* Write parent directory to file */
        e.name[0] = '.'; e.name[1] = '.'; e.name[2] = '\0';
        e.in_use = true;
        e.is_dir = true;
        if (parent == NULL) { /* Indicates that root is parent */
            e.inode_sector = sector;
        } else {
            e.inode_sector = inode_get_inumber(parent->inode);
        }
        if (inode_write_at(inode, &e, sizeof(e), sizeof(e)) != sizeof(e)) {
            inode_remove(inode);
            inode_close(inode);
            return false;
        }
        inode_close(inode);
        return true;
    }
    return false;
}

/*! Opens and returns the directory for the given INODE, of which
    it takes ownership.  Returns a null pointer on failure. */
struct dir * dir_open(struct inode *inode) {
    struct dir *dir = calloc(1, sizeof(*dir));
    if (inode != NULL && dir != NULL) {
        dir->inode = inode;
        inode_set_dir(dir->inode);
        dir->pos = sizeof(struct dir_entry) * 2;
        return dir;
    }
    else {
        inode_close(inode);
        free(dir);
        return NULL;
    }
}

/*! Opens the root directory and returns a directory for it.
    Return true if successful, false on failure. */
struct dir * dir_open_root(void) {
    return dir_open(inode_open(ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir * dir_reopen(struct dir *dir) {
    return dir_open(inode_reopen(dir->inode));
}

/*! Destroys DIR and frees associated resources. */
void dir_close(struct dir *dir) {
    if (dir != NULL) {
        inode_close(dir->inode);
        free(dir);
    }
}

/*! Returns the inode encapsulated by DIR. */
struct inode * dir_get_inode(struct dir *dir) {
    return dir->inode;
}

/*! Searches DIR for a file with the given NAME.
    If successful, returns true, sets *EP to the directory entry
    if EP is non-null, and sets *OFSP to the byte offset of the
    directory entry if OFSP is non-null.
    otherwise, returns false and ignores EP and OFSP. */
static bool lookup(const struct dir *dir, const char *name,
                   struct dir_entry *ep, off_t *ofsp, bool *is_dir) {
    struct dir_entry e;
    size_t ofs;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    for (ofs = 0; inode_read_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);
         ofs += sizeof(e)) {
        if (e.in_use && !strcmp(name, e.name)) {
            if (ep != NULL)
                *ep = e;
            if (ofsp != NULL)
                *ofsp = ofs;
            *is_dir = e.is_dir;
            return true;
        }
    }
    return false;
}
/*! Variant of lookup that specifies if file or dir in search */
static bool lookup_typed(const struct dir *dir, const char *name,
                   struct dir_entry *ep, off_t *ofsp, bool is_dir) {
    struct dir_entry e;
    size_t ofs;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    for (ofs = 0; inode_read_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);
         ofs += sizeof(e)) {
        if (e.in_use && !strcmp(name, e.name) && e.is_dir == is_dir) {
            if (ep != NULL)
                *ep = e;
            if (ofsp != NULL)
                *ofsp = ofs;
            return true;
        }
    }
    return false;
}

/*! Searches DIR for a file with the given NAME and returns true if one exists,
    false otherwise.  On success, sets *INODE to an inode for the file,
    otherwise to a null pointer.  The caller must close *INODE. */
bool dir_lookup(const struct dir *dir, const char *name, struct inode **inode) {
    struct dir_entry e;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    if (lookup_typed(dir, name, &e, NULL, false))
        *inode = inode_open(e.inode_sector);
    else
        *inode = NULL;

    return *inode != NULL;
}
/*! Lookup version that searches for a directory of the given name */
bool dir_lookup_dir(const struct dir *dir, const char *name, struct inode **inode) {
    struct dir_entry e;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    if (lookup_typed(dir, name, &e, NULL, true)) {
        *inode = inode_open(e.inode_sector);
        inode_set_dir(*inode);
    } else {
        *inode = NULL;
    }

    return *inode != NULL;
}
/*! Lookup version that finds either a file or a directory */
bool dir_lookup_any(const struct dir *dir, const char *name, struct inode **inode) {
    struct dir_entry e;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    bool is_dir;
    if (lookup(dir, name, &e, NULL, &is_dir)) {
        *inode = inode_open(e.inode_sector);
        if (is_dir) {
            inode_set_dir(*inode);
        }
    } else {
        *inode = NULL;
    }

    return *inode != NULL;
}

/*! Adds a file named NAME to DIR, which must not already contain a file by
    that name.  The file's inode is in sector INODE_SECTOR.
    Returns true if successful, false on failure.
    Fails if NAME is invalid (i.e. too long) or a disk or memory
    error occurs. */
bool dir_add(struct dir *dir, const char *name, block_sector_t inode_sector) {
    return dir_add_obj(dir, name, inode_sector, false);
}

/*! Adds a directory named NAME to DIR, which must not already contain a 
    directory by that name.  The directory's inode is in sector INODE_SECTOR.
    Returns true if successful, false on failure.
    Fails if NAME is invalid (i.e. too long) or a disk or memory
    error occurs. */
bool dir_add_dir(struct dir *dir, const char *name, block_sector_t inode_sector) {
    return dir_add_obj(dir, name, inode_sector, true);
}

bool dir_add_obj(struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir) {

    struct dir_entry e;
    off_t ofs;
    bool success = false;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    /* Check NAME for validity. */
    if (*name == '\0' || strlen(name) > NAME_MAX)
        return false;

    /* Check that NAME is not in use. */
    bool trash;
    if (lookup(dir, name, NULL, NULL, &trash))
        goto done;

    /* Set OFS to offset of free slot.
       If there are no free slots, then it will be set to the
       current end-of-file.

       inode_read_at() will only return a short read at end of file.
       Otherwise, we'd need to verify that we didn't get a short
       read due to something intermittent such as low memory. */
    for (ofs = 0; inode_read_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);
         ofs += sizeof(e)) {
        if (!e.in_use)
            break;
    }
    
    /* Write slot. */
    e.in_use = true;
    strlcpy(e.name, name, sizeof e.name);
    e.inode_sector = inode_sector;
    e.is_dir = is_dir;
    success = inode_write_at(dir->inode, &e, sizeof(e), ofs) == sizeof(e);

done:
    return success;

}

/*! Removes any entry for NAME in DIR.  Returns true if successful, false on
    failure, which occurs only if there is no file with the given NAME. */
bool dir_remove(struct dir *dir, const char *name) {
    struct dir_entry e;
    struct inode *inode = NULL;
    bool success = false;
    bool is_dir;
    off_t ofs;
    struct dir *dir_rm = NULL;

    ASSERT(dir != NULL);
    ASSERT(name != NULL);

    /* Find directory entry. */
    if (!lookup(dir, name, &e, &ofs, &is_dir))
        goto done;

    /* Open inode. */
    inode = inode_open(e.inode_sector);
    if (inode == NULL)
        goto done;
    if (is_dir) {
        dir_rm = dir_open(inode);
    }

    /* Erase directory entry. */
    e.in_use = false;
    if (inode_write_at(dir->inode, &e, sizeof(e), ofs) != sizeof(e))
        goto done;

    /* Remove inode. For directories, check if empty first */
    if (dir_rm == NULL && !dir_empty(dir_rm)) {
        goto done;
    }
    inode_remove(inode);
    success = true;

done:
    inode_close(inode);
    return success;
}

/*! Returns true if a directory is empty and contains no files or subdirectories */
bool dir_empty(struct dir *dir) {
    char trash[NAME_MAX+1];
    off_t pos_orig = dir->pos;
    dir->pos = sizeof(struct dir_entry) * 2;
    bool not_empty = dir_readdir(dir, trash);
    dir->pos = pos_orig;
    return !not_empty;
}

/*! Reads the next directory entry in DIR and stores the name in NAME.  Returns
    true if successful, false if the directory contains no more entries. */
bool dir_readdir(struct dir *dir, char name[NAME_MAX + 1]) {
    struct dir_entry e;

    while (inode_read_at(dir->inode, &e, sizeof(e), dir->pos) == sizeof(e)) {
        dir->pos += sizeof(e);
        if (e.in_use) {
            strlcpy(name, e.name, NAME_MAX + 1);
            return true;
        }
    }
    return false;
}

/*! Returns TRUE if the passed file ID is a directory, and FALSE if it is a
    file. */
bool dir_is_dir(struct file *f) {
    ASSERT(f != NULL);
    return inode_is_dir(f->inode);
    
}