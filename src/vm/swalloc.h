#ifndef VM_SWALLOC_H
#define VM_SWALLOC_H

#include <stddef.h>
#include <list.h>
#include "threads/palloc.h"
#include "threads/thread.h"
 
/*! A swap entry struct. */
struct swap {
    block_sector_t start_sector;    /*!< Starting sector of swap. */
    bool in_use;                    /*!< Marks a swap as used or open. */
    struct list_elem process_elem;  /*!< List element for process. */
    struct list_elem open_elem;     /*!< List element for open list. */
};

void swalloc_init(void);
struct swap *swalloc_get_swap(void);
void swalloc_free_swap(struct swap *);

#endif /* vm/swalloc.h */
