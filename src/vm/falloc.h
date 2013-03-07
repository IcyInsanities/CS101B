#ifndef VM_FALLOC_H
#define VM_FALLOC_H

#include <stddef.h>
#include <list.h>

/* How to allocate frames. */
enum alloc_flags
{
    PAL_ASSERT = 001,           /* Panic on failure. */
    PAL_ZERO = 002,             /* Zero frame contents. */
    PAL_USER = 004              /* User frame. */
};

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
    uint32_t *pte                   /*< Related page table entry. */
    struct page_entry *sup_entry    /*!< Supplemental page table entry. */
    struct list_elem process_elem;  /*!< List element for process. */
    struct list_elem open_elem;     /*!< List element for open list. */
}

struct 
void falloc_init (size_t user_page_limit);
void *falloc_get_frame (enum falloc_flags);
void *falloc_get_multiple (enum falloc_flags, size_t page_cnt);
void falloc_free_frame (void *);
void falloc_free_multiple (void *, size_t page_cnt);

#endif /* vm/falloc.h */
