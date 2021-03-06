#ifndef VM_FALLOC_H
#define VM_FALLOC_H

#include <stddef.h>
#include <list.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/*! A frame entry struct. */
struct frame {
    void *faddr;                    /*!< Address of corresponding frame. */
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

struct page_entry *get_page_entry(void);
void free_page_entry(struct page_entry *);

#endif /* vm/falloc.h */
