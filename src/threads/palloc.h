#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>

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
};

void palloc_init (size_t user_page_limit);
void *palloc_get_page (enum alloc_flags);
void *palloc_get_multiple (enum alloc_flags, size_t page_cnt);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);

#endif /* threads/palloc.h */
