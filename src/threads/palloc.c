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
#include "vm/falloc.h"

/*! Initializes the page allocator.  At most USER_PAGE_LIMIT
    pages are put into the user pool. */
void palloc_init(void)
{
    /* Does nothing for now? */
    // TODO!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // Or maybe not.
}

/*! Obtains and returns a group of PAGE_CNT contiguous free pages.
    If PAL_USER is set, the pages are obtained from the user pool,
    otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
    then the pages are filled with zeros.  If too few pages are
    available, returns a null pointer, unless PAL_ASSERT is set in
    FLAGS, in which case the kernel panics. */
void * palloc_get_multiple(enum alloc_flags flags, size_t page_cnt) {
    //struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
    //void *pages;
    //size_t page_idx;
    //
    //if (page_cnt == 0)
    //    return NULL;
    //
    //lock_acquire(&pool->lock);
    //page_idx = bitmap_scan_and_flip(pool->used_map, 0, page_cnt, false);
    //lock_release(&pool->lock);
    //
    //if (page_idx != BITMAP_ERROR)
    //    pages = pool->base + PGSIZE * page_idx;
    //else
    //    pages = NULL;
    //
    //if (pages != NULL) {
    //    if (flags & PAL_ZERO)
    //        memset(pages, 0, PGSIZE * page_cnt);
    //}
    //else {
    //    if (flags & PAL_ASSERT)
    //        PANIC("palloc_get: out of pages");
    //}
    //
    //return pages;
}

/*! Obtains a single free page and returns its kernel virtual
    address.
    If PAL_USER is set, the page is obtained from the user pool,
    otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
    then the page is filled with zeros.  If no pages are
    available, returns a null pointer, unless PAL_ASSERT is set in
    FLAGS, in which case the kernel panics. */
void * palloc_get_page(enum alloc_flags flags) {
    return palloc_get_multiple(flags, 1);
}

/*! Frees the PAGE_CNT pages starting at PAGES. */
void palloc_free_multiple(void *pages, size_t page_cnt) {
//    struct pool *pool;
//    size_t page_idx;
//
//    ASSERT(pg_ofs(pages) == 0);
//    if (pages == NULL || page_cnt == 0)
//        return;
//
//    if (page_from_pool(&kernel_pool, pages))
//        pool = &kernel_pool;
//    else if (page_from_pool(&user_pool, pages))
//        pool = &user_pool;
//    else
//        NOT_REACHED();
//
//    page_idx = pg_no(pages) - pg_no(pool->base);
//
//#ifndef NDEBUG
//    memset(pages, 0xcc, PGSIZE * page_cnt);
//#endif
//
//    ASSERT(bitmap_all(pool->used_map, page_idx, page_cnt));
//    bitmap_set_multiple(pool->used_map, page_idx, page_cnt, false);
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
    if (start_addr > end_addr || is_user_vaddr(start_addr) != is_user_vaddr(start_addr)) {
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
