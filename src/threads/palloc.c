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
#include "threads/fmalloc.h"
#include "threads/init.h"
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "vm/falloc.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"

static bool palloc_block_valid(void *start_addr, size_t block_size);

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
    uint32_t i;
    struct thread *t = thread_current();
    uint32_t *pagedir;
    uint32_t *pte;
    void *vaddr;
    void *curr_f_ofs = f_ofs;
    
    /* Page data should not be in a frame. */
    if (load_type == FRAME_PAGE) {
        // TODO: handle
        ASSERT(false);
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
        
        /* Check if read only. */
        if (flags & PAL_READO) {
            *pte = *pte & (~PTE_W);
        }

        // TODO: need to handle flags properly (done? -shir)
        
        /* Add to list of allocated pages in order by address. */
        list_insert_ordered(alloc_page_list, &(page_i->elem), palloc_page_less, NULL);
        
    }
    
    return start_addr;
}

void *palloc_make_page_addr(void * start_addr, enum palloc_flags flags,
                            enum page_load load_type, void *data, void *f_ofs) {

    /* Allocate one page at the specified address, and return. */
    return palloc_make_multiple_addr(start_addr, flags, 1, load_type, data, f_ofs);

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
    
    void *start_addr;

    /* Look for an open block. */
    start_addr = palloc_get_open_addr(flags & PAL_USER, page_cnt);
    
    /* Allocate the block. */
    start_addr = palloc_make_multiple_addr(start_addr, flags, page_cnt, load_type, data, f_ofs);
    
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
// TODO: DELTETION FROM KERNEL PAGEDIR AFFECTS ALL PROCESSES
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
    
    uint32_t i = 0;
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
        kill_current_thread(1);
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
                kill_current_thread(1);
            }
        }
        
        /* Move on to paging list if not in process list. */
        if (!start_page_found) {
            paging_list = true;
            alloc_page_list = init_page_dir_sup;
        }
    }
    
    /* Go through all the pages in the block, freeing them. */
    for (e = &(start_page->elem), i = 0;
        e != list_end(alloc_page_list), i < page_cnt;
        e = list_next(e), i++)
    {
    
        struct page_entry *page_e = list_entry(e, struct page_entry, elem);
        
        /* If an unallocated page is in the block, can't free. */
        if (page_e->vaddr != vaddr) {
        
            // TODO: Kill the process if user
            if (user_space) {
                kill_current_thread(1);
            }
            // TODO: Kernel panic if kernel
            else {
                PANIC("palloc_free: unallocated page in block to free");
                
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
bool palloc_page_less(const struct list_elem *A,
                             const struct list_elem *B,
                             void* aux UNUSED) {
                             
    uint8_t *vaddrA = list_entry(A, struct page_entry, elem)->vaddr;
    uint8_t *vaddrB = list_entry(B, struct page_entry, elem)->vaddr;
 
    return vaddrA < vaddrB;
}

bool palloc_block_open_list(void* vaddr, struct list *alloc_list, struct list_elem *e, size_t block_size) {
    struct page_entry *curr_alloc_page;
    struct page_entry *next_alloc_page;
    struct list_elem *prev_elem;
    struct list_elem *next_e;
    void *end_addr;
    
    /* If at the list end, block open if it is valid. */
    if (e == list_end(alloc_list)) {
        return palloc_block_valid(vaddr, block_size);
    }
    
    /* Get the address of page after end of proposed block. */
    end_addr = vaddr + (block_size*PGSIZE);
    
    next_alloc_page = list_entry(e, struct page_entry, elem);
    
    /* If next allocated block is after end address, and block is valid, it is open. */ 
    return (bool) (end_addr <= next_alloc_page->vaddr && palloc_block_valid(vaddr, block_size));

}

// TODO: might want to change it to get the one after
struct list_elem *palloc_alloc_elem_after_addr(void *vaddr, struct list *alloc_list, struct list_elem *curr_elem) {

    struct list_elem *e;
    struct page_entry *curr_page;
    ASSERT(!list_empty(alloc_list));
    ASSERT(curr_elem != list_end(alloc_list));
    /* Search through list for the specified address. */
    for (e = curr_elem; e != list_end(alloc_list); e = list_next(e)) {
        
        curr_page = list_entry(e, struct page_entry, elem);
        
        /* Once allocated page past the given address found, go back one to get
         * to the page one before the address. */
        if (((void *) curr_page->vaddr) >= vaddr) {
            return e;
        }
    }
    
    return list_end(alloc_list);
}

void* palloc_get_open_addr(bool user_space, size_t block_size) {

    bool block_found = false;
    struct list *proc_list;
    struct list *paging_list = init_page_dir_sup;
    struct list_elem *proc_elem;
    struct list_elem *paging_elem;
    struct page_entry *proc_page;
    struct page_entry *paging_page;
    struct page_entry *next_proc_page;
    struct page_entry *next_paging_page;
    struct thread *t = thread_current();
    uint32_t alloc_size = block_size * PGSIZE;
    void *curr_addr;
    void *start_addr;
    bool proc_page_open = false;
    bool paging_page_open = false;
    int last_page_index;
    uint32_t i;
    
    /* Look in the process list for the starting page first. */
    proc_list = &(t->page_entries);

    /* If requesting kernel space, get to kernel space first. */
    if (!user_space) {
    
        if (!list_empty(proc_list)) {
            proc_elem = palloc_alloc_elem_after_addr(PHYS_BASE, proc_list, list_begin(proc_list));
            /* Check if there is an open block at start of kernel space. */
            proc_page_open = palloc_block_open_list(PHYS_BASE, proc_list, proc_elem, block_size);
        }
        else {
            proc_page_open = palloc_block_valid(PHYS_BASE, block_size);
        }
        
        if (!list_empty(paging_list)) {
            paging_elem = palloc_alloc_elem_after_addr(PHYS_BASE, paging_list, list_begin(paging_list));
            /* Check if there is an open block at start of kernel space. */
            paging_page_open = palloc_block_open_list(PHYS_BASE, paging_list, paging_elem, block_size);
        }
        else {
            paging_page_open = palloc_block_valid(PHYS_BASE, block_size);
        }
        
        /* If open in both lists, valid address. */
        if (proc_page_open && paging_page_open) {
            return (void *) PHYS_BASE;
        }
        else {
            proc_page_open = false;
            paging_page_open = false;
            start_addr = PHYS_BASE;
            last_page_index = NUM_PAGES;
        }
        
    }
    else {
    
        if (!list_empty(proc_list)) {
            proc_elem = palloc_alloc_elem_after_addr((void *) PGSIZE, proc_list, list_begin(proc_list));
            /* Check if there is an open block at start of user space. */
            proc_page_open = palloc_block_open_list((void *) PGSIZE, proc_list, proc_elem, block_size);
        }
        else {
            proc_page_open = palloc_block_valid(PHYS_BASE, block_size);
        }
        
        /* If open in both lists, valid address. */
        if (proc_page_open) {
            return (void *) PGSIZE;
        }
        else {
            proc_page_open = false;
            paging_page_open = false;
            start_addr = (void *) PGSIZE;
            last_page_index = ((uint32_t) PHYS_BASE)/PGSIZE;
        }
    }
    
    /* Search address space for an open block. */
    for (i = (((uint32_t) start_addr)/PGSIZE) + 1; i < last_page_index; i++) {
        
        curr_addr = (void *) (i * PGSIZE);
        if (!list_empty(proc_list)) {
            proc_elem = palloc_alloc_elem_after_addr(curr_addr, proc_list, proc_elem);
            proc_page_open = palloc_block_open_list(curr_addr, proc_list, proc_elem, block_size);
            
        }
        else {
            /* TODO: Might want to make a call to check if valid. */
            proc_page_open = true;
        }
        /* If in kernel space need, to check that the block is open in both
           lists. */
        if (!user_space) {
        
            if (!list_empty(paging_list)) {
                paging_elem = palloc_alloc_elem_after_addr(curr_addr, paging_list, paging_elem);
                paging_page_open = palloc_block_open_list(curr_addr, paging_list, paging_elem, block_size);
            }
            else {
                /* TODO: Might want to make a call to check if valid. */
                paging_page_open = true;
            }   
            
            /* If free in both lists, then must be free. */
            if (paging_page_open && proc_page_open) {
                ASSERT(curr_addr >= (void *) PHYS_BASE);
                return curr_addr;
            }
        }
        else {
            /* If in user space, and free in process list, must be free. */
            if (proc_page_open) {
                ASSERT(curr_addr >= (void *) PGSIZE);
                return curr_addr;
            }
        }
    }
    
    /* If no open block could be found, return NULL. */
    return NULL;
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
    bool page_found = false;
    bool paging_list = false;
    
    /* Calculate ending address of the block. */
    end_addr = start_addr + (block_size*PGSIZE - 1);
    
    /* If the address overflowed, or entire block is not in the same space, the
       block is not open. */
    if (!palloc_block_valid(start_addr, block_size)) {
        return false;
    }

    // TODO: NEED TO FUCKING CHANGE THIS TO SEARCH BOTH LIST
    ///* If in user space, get list of supplemental page entries from process. */
    //if (is_user_vaddr(start_addr)) {
    //    alloc_page_list = &(t->page_entries);
    //}
    ///* Otherwise, get list of supplemental page entries from kernel. */
    //else {
    //    alloc_page_list = init_page_dir_sup;
    //}
    //
    ///* Loop until start address is reached in allocated list. */
    //for (e = list_begin(alloc_page_list); e != list_end(alloc_page_list);
    //    e = list_next(e))
    //{
    //    start_page = list_entry(e, struct page_entry, elem);
    //    
    //    /* If reached the end of list, no more allocated pages, block open. */
    //    if (list_next(e) == list_end(alloc_page_list)) {
    //        return true;
    //    }
    //    /* If reached starting address, can check unallocated until end addr */
    //    else if (((void *) start_page->vaddr) >= start_addr) {
    //        /* Get the next allocated page. */
    //        next_page = list_next(e); // DEBUG: should this be next or current???
    //        break;
    //    }
    //}
    
    /* Look in the process list for the starting page first. */
    alloc_page_list = &(t->page_entries);
    
    while (!page_found) {
    
        /* Find the starting address in the supplemental page table. */
        for (e = list_begin(alloc_page_list); e != list_end(alloc_page_list);
            e = list_next(e))
        {
            start_page = list_entry(e, struct page_entry, elem);
            
            /* If a match is found, it is allocated and not open. */
            if (((void *) start_page->vaddr) == start_addr) {
                return false;
            }
            /* If end of paging list, no more allocated pages, block open. */
            else if (list_next(e) == list_end(alloc_page_list) && paging_list) {
                return true;
            }
            /* If reached starting address, can check unallocated until end addr */
            else if (((void *) start_page->vaddr) > start_addr) {
                /* Get the next allocated page. */
                next_page = start_page; // DEBUG: should this be next or current???
                page_found = true;
                break;
            }
        }
        
        /* Move on to paging list if not in process list. */
        if (!page_found) {
            paging_list = true;
            alloc_page_list = init_page_dir_sup;
        }
    }
    
    /* If the next alloc page is after ending address, block is open. */
    if (((void *) list_entry(next_page, struct page_entry, elem)->vaddr) > end_addr) {
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

// TODO: finish implementing this properly
struct page_entry *palloc_addr_to_page_entry(void *page_addr) {
    struct list *alloc_page_list;
    struct thread *t = thread_current();
    struct list_elem *e;
    struct page_entry *page;
    bool page_found = false;
    bool paging_list = false;
    
    /* Look in the process list for the page first. */
    alloc_page_list = &(t->page_entries);
    
    while (!page_found) {
    
        /* Find the address in the supplemental page table. */
        for (e = list_begin(alloc_page_list); e != list_end(alloc_page_list);
            e = list_next(e))
        {
            page = list_entry(e, struct page_entry, elem);
            
            /* If found address, return its supplemental entry */
            if (((void *) page->vaddr) == page_addr) {
                return page;
            }
            /* If end of paging list, not allocated, return NULL. */
            else if (list_next(e) == list_end(alloc_page_list) && paging_list) {
                return NULL;
            }
            
        }
        
        /* Move on to paging list if not in process list. */
        if (!page_found) {
            paging_list = true;
            alloc_page_list = init_page_dir_sup;
        }
    }
    
    return NULL;
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

// implement read only (PAL_READO)
