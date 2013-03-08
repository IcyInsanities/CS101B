/*! \file palloc.c

   Page allocator.  Hands out memory in page-size (or page-multiple) chunks.
   See malloc.h for an allocator that hands out smaller chunks.

   System memory is divided into two "pools" called the kernel and user pools.
   The user pool is for user (virtual) memory pages, the kernel pool for
   everything else.  The idea here is that the kernel needs to have memory for
   its own operations even if user processes are swapping like mad.

   This allocator assigns a virtual page to the processes which can be put into
   a frame */

#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "threads/init.h"
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "vm/falloc.h"

static bool palloc_block_valid(void *start_addr, size_t block_size);
static bool palloc_page_less(const struct list_elem *A,
                             const struct list_elem *B,
                             void* aux UNUSED);

/*! Initializes the page allocator.  At most USER_PAGE_LIMIT
    pages are put into the user pool. */
void palloc_init(void)
{
    /* Does nothing for now? */
    // TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // Or maybe not.
}

void *palloc_make_multiple_addr(void * start_addr,
                                enum palloc_flags flags, 
                                size_t page_cnt,
                                enum page_load load_type, 
                                void *data,
                                void *f_ofs) {
                                
    struct list *alloc_page_list;
    int i;
    struct thread *t = thread_current();
    uint32_t *pagedir;
    uint32_t *pte;
    void *vaddr;
    void *curr_f_ofs = f_ofs;
    
    /* Page data should not be in a frame. */
    if (load_type == FRAME_PAGE) {
        // TODO: handle
    }
    
    /* Use to correct pool based on whether it is paging data or not. */
    if (flags & PAL_PAGING) {
        alloc_page_list = init_page_dir_sup;
        pagedir = init_page_dir;
    } else {
        alloc_page_list = &(t->page_entries);
        pagedir = t->pagedir;
    }
    
    /* If block at specified address is not open, return NULL. */
    if (!palloc_block_open(start_addr, page_cnt)) {
        if (flags & PAL_ASSERT) {
            PANIC("palloc: out of pages");
        }
        return NULL;
    }
    
    /* Allocate all pages for the block. */
    for (i = 0; i < page_cnt; i++) {
        
        /* Create a supplemental entry for the page. */
        struct page_entry *page_i = (struct page_entry *) fmalloc(sizeof(struct page_entry));
        
        ASSERT (page_i != NULL);
        
        /* Get the vrtual address for the page. */
        vaddr = (uint8_t *) (start_addr + (i * PGSIZE));
        
        /* Initialize the page. */
        page_i->vaddr = vaddr;
        page_i->source = load_type; // DEBUG: set to zero page by default for now
        if (load_type == ZERO_PAGE) {
            page_i->data = NULL;
        }
        else {
            page_i->data = data;
        }
        
        if (load_type == FILE_PAGE) {
            /* Get the file offset. */
            curr_f_ofs += PGSIZE;
            page_i->f_ofs = curr_f_ofs;
        }
        else {
            page_i->f_ofs = NULL;
        }
        
        pte = lookup_page(pagedir, vaddr, true);
        
        /* Pin the page if necessary. */
        if (flags & PAL_PIN) {
           *pte = *pte | PTE_PIN;
        }

        // TODO: need to handle flags properly (done? -shir)
        
        /* Add to list of allocated pages in order by address. */
        list_insert_ordered(alloc_page_list, &(page_i->elem), palloc_page_less, NULL);
        
    }
    
    return start_addr;
}

void *palloc_make_page_addr(void * start_addr, enum palloc_flags flags,
                            enum page_load load_type, void *data) {

    /* Allocate one page at the specified address, and return. */
    return palloc_make_multiple_addr(start_addr, flags, 1, load_type, data, NULL);

}

/*! Obtains and returns a group of PAGE_CNT contiguous free pages.
    If PAL_USER is set, the pages are obtained from the user pool,
    otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
    then the pages are filled with zeros.  If too few pages are
    available, returns a null pointer, unless PAL_ASSERT is set in
    FLAGS, in which case the kernel panics. */
void *palloc_get_multiple(enum palloc_flags flags, size_t page_cnt) {

    return _palloc_get_multiple(flags, page_cnt, ZERO_PAGE, NULL, NULL);
}
    
        // take enum page_load
    // take void* to data
void *_palloc_get_multiple(enum palloc_flags flags, size_t page_cnt, enum page_load load_type, void *data, void *f_ofs) {
    /*
    struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
    void *pages;
    size_t page_idx;
    
    if (page_cnt == 0)
        return NULL;
    
    lock_acquire(&pool->lock);
    page_idx = bitmap_scan_and_flip(pool->used_map, 0, page_cnt, false);
    lock_release(&pool->lock);
    
    if (page_idx != BITMAP_ERROR)
        pages = pool->base + PGSIZE * page_idx;
    else
        pages = NULL;
    
    if (pages != NULL) {
        if (flags & PAL_ZERO)
            memset(pages, 0, PGSIZE * page_cnt);
    }
    else {
        if (flags & PAL_ASSERT)
            PANIC("palloc_get: out of pages");
    }
    
    return pages;
    */
    struct list *alloc_page_list;
    void *start_addr;
    int i;
    struct thread *t = thread_current();
    
    /* Look for an open block. */
    for (i = 1; i < NUM_PAGES; i++) {
        /* Get the next page address. */
        start_addr = (void *) (i * PGSIZE);
        
        /* If the block is open, allocate it */
        if (palloc_block_open(start_addr, page_cnt)) {
            break;
        }
        /* If reached end of address space and nothing found, nothing to
           allocate. */
        else {
            return NULL;
        }
        /* Otherwise keep looking. */
        
    }
    
    /* Allocate the block. */
    start_addr = palloc_make_multiple_addr(start_addr, page_cnt, 1, load_type, data, f_ofs);
    
    return start_addr;
}

/*! Obtains a single free page and returns its kernel virtual
    address.
    If PAL_USER is set, the page is obtained from the user pool,
    otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
    then the page is filled with zeros.  If no pages are
    available, returns a null pointer, unless PAL_ASSERT is set in
    FLAGS, in which case the kernel panics. */
void *palloc_get_page(enum palloc_flags flags) {
    return _palloc_get_page(flags, ZERO_PAGE, NULL, NULL);
}

void *_palloc_get_page(enum palloc_flags flags, enum page_load load_type, void *data, void *f_ofs) {
    return _palloc_get_multiple(flags, 1, load_type, data, f_ofs);
}

/*! Frees the PAGE_CNT pages starting at PAGES. */
void palloc_free_multiple(void *pages, size_t page_cnt) {
/*
    struct pool *pool;
    size_t page_idx;

    ASSERT(pg_ofs(pages) == 0);
    if (pages == NULL || page_cnt == 0)
        return;

    if (page_from_pool(&kernel_pool, pages))
        pool = &kernel_pool;
    else if (page_from_pool(&user_pool, pages))
        pool = &user_pool;
    else
        NOT_REACHED();

    page_idx = pg_no(pages) - pg_no(pool->base);

#ifndef NDEBUG
    memset(pages, 0xcc, PGSIZE * page_cnt);
#endif

    ASSERT(bitmap_all(pool->used_map, page_idx, page_cnt));
    bitmap_set_multiple(pool->used_map, page_idx, page_cnt, false);
    
*/
    
    int i = 0;
    void *vaddr = pages;
    struct list *alloc_page_list;
    bool user_space;
    struct page_entry *start_page;
    struct list_elem *e;
    struct thread *t = thread_current();
    bool start_page_found = false;
    bool paging_list = false;
    
    /* Make sure the block to free is valid. */
    if(!palloc_block_valid(pages, page_cnt)) {
        // Kill process
    }
    
    /* Check if from user or kernel space. */
    if (is_user_vaddr(pages)) {
        user_space = true;
    }
    else {
        user_space = false;
    }
    
    /* Look in the process list for the page first. */
    alloc_page_list = &(t->page_entries);
    
    while (!start_page_found) {
    
        /* Find the starting address in the supplemental page table. */
        for (e = list_begin(alloc_page_list); e != list_end(alloc_page_list);
            e = list_next(e))
        {
            start_page = list_entry(e, struct page_entry, elem);
            
            /* If reached starting address, can start freeing. */
            if (start_page->vaddr == pages) {
                start_page_found = true;
                break;
            }
            /* If reached the end of paging list, page was not allocated, kill process. */
            else if (list_next(e) == list_end(alloc_page_list) && paging_list) {
                // TODO: Kill process
            }
        }
        
        /* Move on to paging list if not in process list. */
        if (!start_page_found) {
            paging_list = true;
            alloc_page_list = init_page_dir_sup;
        }
    }
    
    /* Go through all the pages in the block, freeing them. */
    for (e = list_begin(alloc_page_list), i = 0;
        e != list_end(alloc_page_list), i < page_cnt;
        e = list_next(e), i++)
    {
    
        struct page_entry *page_e = list_entry(e, struct page_entry, elem);
        
        /* If an unallocated page is in the block, can't free. */
        if (palloc_block_open(vaddr, 1)) { // TODO: modiify to compare vaddr with page_e->vaddr...more efficient
        
            // TODO: Kill the process if user
            if (user_space) {
                
            }
            // TODO: Kernel panic if kernel
            else {
                
            }
        }
        /* Otherwise, free the page. */
        else {
            
            /* Remove page from allocated list. */
            list_remove(&(page_e->elem));
            
            /* Free the supplemental page entry. */
            ffree((void *) page_e);
        }
        /* Go to the next page. */
        vaddr += PGSIZE;
    }
    
}

/*! Frees the page at PAGE. */
void palloc_free_page(void *page) {
    palloc_free_multiple(page, 1);
}

/*! A comparison of page entries based on virtual address for list elements */
static bool palloc_page_less(const struct list_elem *A,
                             const struct list_elem *B,
                             void* aux UNUSED) {
                             
    uint8_t *vaddrA = list_entry(A, struct page_entry, elem)->vaddr;
    uint8_t *vaddrB = list_entry(B, struct page_entry, elem)->vaddr;
 
    return vaddrA < vaddrB;
}

/*! Returns true if the block of size block_size starting at start_addr is
    unallocated (open). */ 
bool palloc_block_open(void *start_addr, size_t block_size) {
    struct list *alloc_page_list;
    struct thread *t = thread_current();
    void *end_addr;
    struct list_elem *e;
    struct list_elem *next_page;
    struct page_entry *start_page;
    
    /* Calculate ending address of the block. */
    end_addr = start_addr + (block_size*PGSIZE - 1);
    
    /* If the address overflowed, or entire block is not in the same space, the
       block is not open. */
    if (!palloc_block_valid(start_addr, block_size)) {
        return false;
    }

    /* If in user space, get list of supplemental page entries from process. */
    if (is_user_vaddr(start_addr)) {
        alloc_page_list = &(t->page_entries);
    }
    /* Otherwise, get list of supplemental page entries from kernel. */
    else {
        alloc_page_list = init_page_dir_sup;
    }
    
    /* Loop until start address is reached in allocated list. */
    for (e = list_begin(alloc_page_list); e != list_end(alloc_page_list);
        e = list_next(e))
    {
        start_page = list_entry(e, struct page_entry, elem);
        
        /* If reached the end of list, no more allocated pages, block open. */
        if (list_next(e) == list_end(alloc_page_list)) {
            return true;
        }
        /* If reached starting address, can check unallocated until end addr */
        else if (start_page->vaddr >= start_addr) {
            /* Get the next allocated page. */
            next_page = list_next(e);
            break;
        }
    }
    
    /* If the next alloc page is after ending address, block is open. */
    if (list_entry(next_page, struct page_entry, elem)->vaddr > end_addr) {
        return true;
    }
    /* Otherwise, the block is not open. */
    else {
        return false;
    }
}

static bool palloc_block_valid(void *start_addr, size_t block_size) {
    void *end_addr;
    
    /* Calculate ending address of the block. */
    end_addr = start_addr + (block_size*PGSIZE - 1);
    
    /* If the address overflowed, or entire block is not in the same space, the
       block is not open. */
    if (start_addr > end_addr || is_user_vaddr(start_addr) != is_user_vaddr(start_addr)) {
        return false;
    } else {
        return true;
    }
}

//[x]palloc_make_page_addr()
//[x]palloc_make_multiple_addr()
//address first argument

// modify allocator functions
    // [x]take enum page_load
    // [x]take void* to data
    
    //[x]page_load cannot == FRAME_PAGE (in palloc.h)
    // [x]data not used if zero page
    // [x]old functions wrapper for new functions that always call with 0 page
    // account for pin flag

// should we panic or kill the process on an invalid block? (requested block wraps, crosses kernel-user boundary, etc?)