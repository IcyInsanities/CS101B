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
        swap_list[i].start_sector = i * PAGE_SECTORS;
        swap_list[i].in_use = false;
        list_push_back(open_swap_list, &(swap_list[i].open_elem));
    }
}

/*! Obtains a single free swap and returns its entry. The swap is marked in use,
    and associated into the current process list.
    If no swaps are available, the kernel panics. */
struct swap *swalloc_get_swap()
{
    struct list_elem *elem;
    struct swap *swap_entry;

    /* If no empty swap slots, panic system */
    if (list_empty(open_swap_list))
    {
        PANIC("swalloc_get: out of swap slots");
    }
    /* Otherwise, get an open swap. */
    elem = list_pop_front(open_swap_list);

    /* Remove swap from list of open swaps. */
    swap_entry = list_entry(elem, struct swap, open_elem);
    /* Mark as in use */
    swap_entry->in_use = true;
    /* Add to process list. */
    list_push_back(&(thread_current()->swaps), &(swap_entry->process_elem));

    return swap_entry;
}

/*! Frees the swap at swap. */
// TODO: should this put data back into a frame optionally?
void swalloc_free_swap(struct swap *swap_entry)
{
    /* If it wasn't allocated, just return. */
    // TODO: should this be an error? -Shir
    if (!swap_entry->in_use)
    {
        return;
    }

    /* Add swap struct back to open list. */
    list_push_back(open_swap_list, &(swap_entry->open_elem));
    /* Remove from user's list */
    list_remove(&(swap_entry->process_elem));
    /* Mark as unused */
    swap_entry->in_use = false;
}

/*! Takes a page and writes it into the given swap entry file. */
void swap_write_page(struct swap* swap_entry, void *upage)
{
    ASSERT(swap_entry->in_use);
    uint32_t i;
    for (i = 0; i < PAGE_SECTORS; i++)
    {
        block_write(swap_disk, swap_entry->start_sector + i, upage + i * BLOCK_SECTOR_SIZE);
    }
}

/*! Writes a swap file back to the given page */
void swap_read_page(struct swap* swap_entry, void *upage)
{
    ASSERT(swap_entry->in_use);
    uint32_t i;
    for (i = 0; i < PAGE_SECTORS; i++)
    {
        block_read(swap_disk, swap_entry->start_sector + i, upage + i * BLOCK_SECTOR_SIZE);
    }
}
