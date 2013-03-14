#ifndef VM_SWALLOC_H
#define VM_SWALLOC_H

#include <stddef.h>
#include <list.h>
#include "threads/palloc.h"
#include "threads/thread.h"
 
/*! A swap entry struct. */
struct swap {
    void *swaddr;                   /*!< Address of corresponding swap. */
    struct page_entry *sup_entry;   /*!< Supplemental page table entry. */
    struct thread *owner;           /*!< Thread which owns the swap. */
    struct list_elem process_elem;  /*!< List element for process. */
    struct list_elem open_elem;     /*!< List element for open list. */
};

void swalloc_init(void);
struct swap *get_swap_addr(void);
void *swalloc_get_swap(void *upage, struct page_entry *sup_entry);
void swalloc_free_swap(void *swap);

#endif /* vm/swalloc.h */
