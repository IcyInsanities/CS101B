#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>
#include <list.h>
#include "vm/falloc.h"

/* How to allocate pages. */
enum palloc_flags
{
    PAL_ASSERT = 0x01,           /* Panic on failure. */
    PAL_ZERO = 0x02,             /* Zero frame contents. */
    PAL_USER = 0x04,             /* User frame. */
    PAL_PIN  = 0x08              /* User frame. */
};

/* Indicate where to find page data */
enum page_load
{
    ZERO_PAGE,              /* indicate that page should be zeroed on load */
    FILE_PAGE,              /* indicate that page belongs to file */
    SWAP_PAGE,              /* indicate that page is in swap */
    FRAME_PAGE              /* indicate that page is in a frame */
};

/*! A struct to store data for the supplemental page table. */
struct page_entry
{
    uint8_t *vaddr;                 /*!< Virtual address of page. */
    
    enum page_load source;          /*!< Location type of page data */
    void *data;                     /*!< Pointer to data location, unused if zero page */
    void *f_ofs;                    /*!< Offset inside file */
    
    struct list_elem elem;          /*!< Enable putting page entries into list */
};

void palloc_init (void);
void *palloc_get_page (enum palloc_flags);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);
bool palloc_block_open(void *, size_t block_size);
void *palloc_make_multiple_addr(void *, enum palloc_flags, size_t page_cnt, enum page_load, void *);
void *_palloc_get_multiple(enum palloc_flags, size_t page_cnt, enum page_load, void *);
void *_palloc_get_page(enum palloc_flags flags, enum page_load load_type, void *data);

#endif /* threads/palloc.h */
