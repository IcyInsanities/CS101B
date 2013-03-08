#ifndef VM_FALLOC_H
#define VM_FALLOC_H

#include <stddef.h>
#include <list.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/*! A memory pool. */
struct pool
{
    struct lock lock;                   /*!< Mutual exclusion. */
    struct bitmap *used_map;            /*!< Bitmap of free pages. */
    uint8_t *base;                      /*!< Base of pool. */
};

// TODO: frame struct
// pointer to page
// need to know supplemental page
// 
/*! A frame entry struct. */
struct frame {
    void *faddr;                    /*!< Adress of corresponding frame. */
    uint32_t *pte;                  /*!< Related page table entry. */
    struct page_entry *sup_entry;   /*!< Supplemental page table entry. */
    struct thread *owner;           /*!< Thread which owns the frame. */
    struct list_elem process_elem;  /*!< List element for process. */
    struct list_elem open_elem;     /*!< List element for open list. */
};


void falloc_init(size_t user_page_limit);
struct frame *get_frame_addr(bool user);
void *falloc_get_frame(void *upage, bool user, struct page_entry *sup_entry);
void falloc_free_frame(void *frame);

// TODO: Don't need pool stuff
//static void init_pool(struct pool *, void *base, size_t page_cnt, const char *name);
//static bool page_from_pool(const struct pool *, void *page);

#endif /* vm/falloc.h */
