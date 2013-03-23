/*! \file malloc.c

   A simple implementation of malloc(), this variant uses pinned pages to allow
   for use with paging structures.

   The size of each request, in bytes, is rounded up to a power
   of 2 and assigned to the "descriptor" that manages blocks of
   that size.  The descriptor keeps a list of free blocks.  If
   the free list is nonempty, one of its blocks is used to
   satisfy the request.

   Otherwise, a new page of memory, called an "arena", is
   obtained from the page allocator (if none is available,
   malloc() returns a null pointer).  The new arena is divided
   into blocks, all of which are added to the descriptor's free
   list.  Then we return one of the new blocks.

   When we free a block, we add it to its descriptor's free list.
   But if the arena that the block was in now has no in-use
   blocks, we remove all of the arena's blocks from the free list
   and give the arena back to the page allocator.

   We can't handle blocks bigger than 2 kB using this scheme,
   because they're too big to fit in a single page with a
   descriptor.  We handle those by allocating contiguous pages
   with the page allocator and sticking the allocation size at
   the beginning of the allocated block's arena header. */

#include "threads/fmalloc.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/init.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/falloc.h"

/*! Descriptor. */
struct desc {
    size_t block_size;          /*!< Size of each element in bytes. */
    size_t blocks_per_arena;    /*!< Number of blocks in an arena. */
    struct list free_list;      /*!< List of free blocks. */
    struct lock lock;           /*!< Lock. */
};

/*! Magic number for detecting arena corruption. */
#define ARENA_MAGIC 0x9a548eed

/*! Arena. */
struct arena {
    unsigned magic;             /*!< Always set to ARENA_MAGIC. */
    struct desc *desc;          /*!< Owning descriptor, null for big block. */
    size_t free_cnt;            /*!< Free blocks; pages in big block. */
    struct page_entry pg_ent;   /*!< Page entry struct for fmalloc pages. */
};

/*! Free block. */
struct block {
    struct list_elem free_elem; /*!< Free list element. */
};

/*! Our set of descriptors. */
static struct desc descs[10];   /*!< Descriptors. */
static size_t desc_cnt;         /*!< Number of descriptors. */

static struct arena *block_to_arena(struct block *);
static struct block *arena_to_block(struct arena *, size_t idx);

/*! Initializes the fmalloc() descriptors. */
void fmalloc_init(void) {
    size_t block_size;

    for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2) {
        struct desc *d = &descs[desc_cnt++];
        ASSERT(desc_cnt <= sizeof descs / sizeof *descs);
        d->block_size = block_size;
        d->blocks_per_arena = (PGSIZE - sizeof (struct arena)) / block_size;
        list_init(&d->free_list);
        lock_init(&d->lock);
    }
}

/*! Obtains and returns a new block of at least SIZE bytes.  Returns a null
    pointer if memory is not available. */
void * fmalloc(size_t size) {
    struct desc *d;
    struct block *b;
    struct arena *a;
    void *page;

    /* A null pointer satisfies a request for 0 bytes. */
    if (size == 0)
        return NULL;

    /* Find the smallest descriptor that satisfies a SIZE-byte
       request. */
    for (d = descs; d < descs + desc_cnt; d++) {
        if (d->block_size >= size)
            break;
    }

    /* fmalloc doesn't handle multi-page allocations */
    if (d == descs + desc_cnt) {
        return NULL;
    }

    /* Aqcuire lock to access descriptor. */
    lock_acquire(&d->lock);

    /* If the free list is empty, create a new arena. */
    if (list_empty (&d->free_list)) {
        size_t i;

        /* Allocate a page. */
        //a = palloc_get_page(PAL_PAGING | PAL_PIN);
        page = palloc_get_open_addr(false, 1);
        if (page == NULL) {
            lock_release(&d->lock);
            return NULL; 
        }
        
        /* Put page into frame and install */
        struct frame *f = get_frame_addr(false);
        printf("HI, from fmalloc");
        pagedir_set_page_kernel(init_page_dir, page, f->faddr, true);
        uint32_t *pte = lookup_page(init_page_dir, page, false);
        *pte |= PTE_P | PTE_PIN;
        /* Set frame entries */
        f->pte = pte;
        f->sup_entry = NULL;
        f->owner = NULL;
        /* Now set up page_entry for page */
        a = page;
        a->pg_ent.vaddr = page;
        a->pg_ent.source = FRAME_PAGE;
        a->pg_ent.data = f;
        list_insert_ordered(init_page_dir_sup, &(a->pg_ent.elem), palloc_page_less, NULL);
        
        /* Initialize arena and add its blocks to the free list. */
        a->magic = ARENA_MAGIC;
        a->desc = d;
        a->free_cnt = d->blocks_per_arena;
        for (i = 0; i < d->blocks_per_arena; i++) {
            struct block *b = arena_to_block(a, i);
            list_push_back(&d->free_list, &b->free_elem);
        }
    }

    /* Get a block from free list and return it. */
    b = list_entry(list_pop_front (&d->free_list), struct block, free_elem);
    a = block_to_arena(b);
    a->free_cnt--;
    lock_release(&d->lock);
    return b;
}

/*! Allocates and return A times B bytes initialized to zeroes.  Returns a null
    pointer if memory is not available. */
void * fcalloc(size_t a, size_t b) {
    void *p;
    size_t size;

    /* Calculate block size and make sure it fits in size_t. */
    size = a * b;
    if (size < a || size < b)
        return NULL;

    /* Allocate and zero memory. */
    p = fmalloc(size);
    if (p != NULL)
        memset(p, 0, size);

    return p;
}

/*! Returns the number of bytes allocated for BLOCK. */
static size_t block_size(void *block) {
    struct block *b = block;
    struct arena *a = block_to_arena(b);
    struct desc *d = a->desc;

    return d != NULL ? d->block_size : PGSIZE * a->free_cnt - pg_ofs(block);
}

/*! Attempts to resize OLD_BLOCK to NEW_SIZE bytes, possibly moving it in the
    process.  If successful, returns the new block; on failure, returns a null
    pointer.  A call with null OLD_BLOCK is equivalent to malloc(NEW_SIZE).  A
    call with zero NEW_SIZE is equivalent to free(OLD_BLOCK). */
void * frealloc(void *old_block, size_t new_size) {
    /* A request for 0 bytes is met with a NULL pointer. */
    if (new_size == 0) {
        ffree(old_block);       /* Old block is no longer needed, so free it. */
        return NULL;
    }
    else {
        /* Allocate a new block with size NEW_SIZE. */
        void *new_block = fmalloc(new_size);
        /* If successful, copy data from old block into new block, then free the
           old block. */
        if (old_block != NULL && new_block != NULL) {
            size_t old_size = block_size (old_block);
            size_t min_size = new_size < old_size ? new_size : old_size;
            memcpy(new_block, old_block, min_size);
            ffree(old_block);
        }
        return new_block;
    }
}

/*! Frees block P, which must have been previously allocated with
    fmalloc() or fcalloc(). */
void ffree(void *p) {
    if (p != NULL) {
        struct block *b = p;
        struct arena *a = block_to_arena(b);
        struct desc *d = a->desc;

        if (d != NULL) {
            /* It's a normal block.  We handle it here. */

#ifndef NDEBUG
            /* Clear the block to help detect use-after-free bugs. */
            memset(b, 0xcc, d->block_size);
#endif

            /* Acquire the lock to the descriptor. */
            lock_acquire(&d->lock);

            /* Add block to free list. */
            list_push_front(&d->free_list, &b->free_elem);

            /* If the arena is now entirely unused, free it. */
            if (++a->free_cnt >= d->blocks_per_arena) {
                size_t i;

                ASSERT(a->free_cnt == d->blocks_per_arena);
                for (i = 0; i < d->blocks_per_arena; i++) {
                    struct block *b = arena_to_block(a, i);
                    list_remove(&b->free_elem);
                }
                palloc_free_page(a);
            }

            lock_release(&d->lock);
        }
        else {
            /* It's a big block.  Free its pages. */
            palloc_free_multiple(a, a->free_cnt);
            return;
        }
    }
}

/*! Returns the arena that block B is inside. */
static struct arena * block_to_arena(struct block *b) {
    struct arena *a = pg_round_down(b);

    /* Check that the arena is valid. */
    ASSERT(a != NULL);
    ASSERT(a->magic == ARENA_MAGIC);

    /* Check that the block is properly aligned for the arena. */
    ASSERT(a->desc == NULL
           || (pg_ofs(b) - sizeof *a) % a->desc->block_size == 0);
    ASSERT(a->desc != NULL || pg_ofs(b) == sizeof *a);

    return a;
}

/*! Returns the (IDX - 1)'th block within arena A. */
static struct block * arena_to_block(struct arena *a, size_t idx) {
    ASSERT(a != NULL);
    ASSERT(a->magic == ARENA_MAGIC);
    ASSERT(idx < a->desc->blocks_per_arena);
    return (struct block *) ((uint8_t *) a
                             + sizeof *a
                             + idx * a->desc->block_size);
}
