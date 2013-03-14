/*! \file swalloc.c

   Swap allocator. */

#include "swalloc.h"
#include "falloc.h"
#include "threads/thread.h"
#include <stddef.h>
#include <stdint.h>
#include "devices/block.h"
#include "threads/init.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define PAGE_SECTORS    PGSIZE / BLOCK_SECTOR_SIZE

static struct swap *addr_to_swap(void *swap_addr);

// Need a list of swap structs
static struct list *open_swap_list;

static struct swap *swap_list;

struct block *swap_disk;
static uint32_t swap_slots;


/*! Initializes the swap allocator. */
void swalloc_init(void)
{
    uint32_t i;

    swap_disk = block_get_role(BLOCK_SWAP);
    swap_slots = block_size(swap_disk) / PAGE_SECTORS;

    /* Initialize swap table */
    uint32_t num_pages_used = sizeof(struct swap) * swap_slots;
    num_pages_used = (uint32_t) pg_round_up((void *) num_pages_used) / PGSIZE;

    /* Get pages for swap table */
    swap_list = palloc_get_multiple(PAL_ASSERT | PAL_PAGING | PAL_ZERO, num_pages_used);
    
    /* Initialize list */
    list_init(open_swap_list);
    /* Initialize swap entries */
    for (i = 0; i < swap_slots; ++i)
    {
        uintptr_t paddr = i * PGSIZE;
        swap_list[i].swaddr = (void *) paddr;
        swap_list[i].sup_entry = NULL;
        swap_list[i].owner = NULL;
        list_push_back(open_swap_list, &(swap_list[i].open_elem));
    }
}

struct swap *get_swap_addr(void)
{
    struct list_elem *elem;
    struct swap *swap_entry;
    struct thread *t = thread_current();
    
    /* If no empty swap slots, panic system */
    if (list_empty(open_swap_list))
    {
        PANIC("swalloc_get: out of swap slots");
    }
    /* Otherwise, get an open swap. */
    elem = list_pop_front(open_swap_list);

    /* Remove swap from list of open swaps. */
    swap_entry = list_entry(elem, struct swap, open_elem);
    
    /* Add to process list. */
    list_push_back(&(t->swaps), &(swap_entry->process_elem));

    return swap_entry;
}

/*! Obtains a single free swap and returns its kernel virtual address.
    If no swaps are available, the kernel panics. */
// TODO: need to load data in swap slot, should that be here?
void *swalloc_get_swap(void *upage, struct page_entry *sup_entry)
{
    void *swap;
    struct swap *swap_entry;
    
    /* Get the swap entry. */
    swap_entry = get_swap_addr();
    swap = swap_entry->swaddr;

    /* Associate swap with page. */
    swap_entry->sup_entry = sup_entry;
    swap_entry->owner = thread_current();
    
    return swap;
}

/*! Frees the swap at swap. */
// TODO: should this put data back into a frame optionally?
void swalloc_free_swap(void *swap)
{
    struct swap *swap_entry = addr_to_swap(swap);
    
    /* If it wasn't allocated, just return. */
    // TODO: should this be an error? -Shir
    if (swap_entry->sup_entry == NULL)
    {
        return;
    }

    /* Add swap struct back to open list. */
    list_push_back(open_swap_list, &(swap_entry->open_elem));
    /* Remove from user's list */
    list_remove(&(swap_entry->process_elem));
    /* Reset swap entry values */
    swap_entry->sup_entry = NULL;
    swap_entry->owner = NULL;
}

/*! Returns a pointer to the swap struct for the passed address. */
static struct swap *addr_to_swap(void *swap_addr)
{
    return &(swap_list[pg_no(swap_addr)]);
}
