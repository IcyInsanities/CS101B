/*! \file falloc.c

   Frame allocator.  Hands out memory in frame-size (or frame-multiple) chunks.
   See malloc.h for an allocator that hands out smaller chunks.

   System memory is divided into two "pools" called the kernel and user pools.
   The user pool is for user (virtual) memory frames, the kernel pool for
   everything else.  The idea here is that the kernel needs to have memory for
   its own operations even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and half to the
   user pool.  That should be huge overkill for the kernel pool, but that's
   just fine for demonstration purposes. */

#include "falloc.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/init.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/swalloc.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

#define NUM_PAGE_ENTRY  6000

static struct frame *addr_to_frame(void *frame_addr);

static struct list *open_frame_list_user;
static struct list *open_frame_list_kernel;

static struct frame *frame_list_user;
static struct frame *frame_list_kernel;

static uint32_t user_frames;
static uint32_t kernel_frames;

static struct list *open_page_entry;

void frame_evict(bool user);

/*! Returns a supplementary page entry for an open page.  Note that this
    function will panic if there are no open pages. */
struct page_entry *get_page_entry(void)
{
    if (list_empty(open_page_entry))
    {
        PANIC("get_page_entry: out of page entries to allocate");
    }
    /* Otherwise, get an open page entry. */
    struct list_elem *elem = list_pop_front(open_page_entry);
    return list_entry(elem, struct page_entry, elem);
}

/*! Frees a page entry by putting it back into the open list. */
void free_page_entry(struct page_entry *entry)
{
    list_push_back(open_page_entry, &(entry->elem));
}

/*! Initializes the frame allocator.  At most USER_FRAME_LIMIT frames are put
    into the user pool. */
void falloc_init(size_t user_frame_limit)
{
    uint32_t *pd, *pt;
    size_t page;
    uint32_t i;
    extern char _start, _end_kernel_text;

    /* Free memory starts at 1 MB and runs to the end of RAM. */
    uint8_t *free_start = ptov(1024 * 1024);
    uint8_t *free_end = ptov(init_ram_pages * PGSIZE);
    size_t free_frames = (free_end - free_start) / PGSIZE;
    /* Give half of memory to kernel, half to user. */
    user_frames = free_frames / 2;
    if (user_frames > user_frame_limit)
    {
        user_frames = user_frame_limit;
    }
    kernel_frames = free_frames - user_frames;
    kernel_frames += 1024 * 1024 / PGSIZE;

    /* Initialize frame table, take space out of kernel frames */
    frame_list_kernel = (struct frame *) (1024 * 1024);
    frame_list_user   = (struct frame *) (1024 * 1024 + sizeof(struct frame) * (kernel_frames));
    uint32_t num_frame_used = 1024 * 1024 + sizeof(struct frame) * (user_frames + kernel_frames);
    num_frame_used = (uint32_t) pg_round_up((void *) num_frame_used) / PGSIZE;
    /* Compute space for page_entry structs */
    uint32_t num_frame_for_page_ent = (sizeof(struct page_entry) * NUM_PAGE_ENTRY / PGSIZE) + 1;
    struct page_entry *page_entry_list = (struct page_entry *) (num_frame_used * PGSIZE);
    /* Update total pages used */
    num_frame_used += num_frame_for_page_ent;

    /* Put global variables into frame */
    pd = (uint32_t *) (num_frame_used * PGSIZE);
    memset(pd, 0, PGSIZE);
    num_frame_used++;
    init_page_dir_sup = (struct list *) (num_frame_used * PGSIZE);
    open_frame_list_user = (struct list *) (num_frame_used * PGSIZE + sizeof(struct list));
    open_frame_list_kernel = (struct list *) (num_frame_used * PGSIZE + 2*sizeof(struct list));
    open_page_entry = (struct list *) (num_frame_used * PGSIZE + 3*sizeof(struct list));
    num_frame_used++;
    /* Map and pin the first num_frame_used frames into init_page_dir */
    pt = NULL;
    for (page = 0; page < num_frame_used; page++)
    {
        uintptr_t paddr = page * PGSIZE;
        char *vaddr = ptov(paddr);
        size_t pde_idx = pd_no(vaddr);
        size_t pte_idx = pt_no(vaddr);
        bool in_kernel_text = &_start <= vaddr && vaddr < &_end_kernel_text;

        if (pd[pde_idx] == 0)
        {
            pt = (uint32_t *) (num_frame_used * PGSIZE);
            memset(pt, 0, PGSIZE);
            num_frame_used++;
            pd[pde_idx] = pde_create(pt);
            pd[pde_idx] = pde_create(pt) | PTE_P | PTE_PIN;
        }
        
        pt[pte_idx] = pte_create_kernel(paddr, !in_kernel_text) | PTE_P | PTE_PIN;

        /* Initialize frame entries */
        frame_list_kernel[page].faddr = (void *) paddr;
        frame_list_kernel[page].pte = &(pt[pte_idx]);
        frame_list_kernel[page].sup_entry = NULL;
        frame_list_kernel[page].owner = NULL;

        /* Initialize page_entry in page_entry_list */
        page_entry_list[page].vaddr = (uint8_t *) vaddr;
        page_entry_list[page].source = FRAME_PAGE;
        page_entry_list[page].data = &paddr;
    }
    
    /* Convert address back into virtual address now that done writing to them */
    frame_list_kernel = ptov((uintptr_t) frame_list_kernel);
    frame_list_user = ptov((uintptr_t) frame_list_user);
    init_page_dir = ptov((uintptr_t) pd);
    init_page_dir_sup = ptov((uintptr_t) init_page_dir_sup);
    open_frame_list_user = ptov((uintptr_t) open_frame_list_user);
    open_frame_list_kernel = ptov((uintptr_t) open_frame_list_kernel);
    page_entry_list = ptov((uintptr_t) page_entry_list);
    open_page_entry = ptov((uintptr_t) open_page_entry);
    
    /* Switch into the page directory that we created before we can initialize
       any lists, otherwise addresses will be physical and not virtal
       Store the physical address of the page directory into CR3 aka PDBR (page
       directory base register).  This activates our new page tables
       immediately.  See [IA32-v2a] "MOV--Move to/from Control Registers" and
       [IA32-v3a] 3.7.5 "Base Address of the Page Directory". */
    asm volatile ("movl %0, %%cr3" : : "r" (vtop (init_page_dir)));

    /* Initialize lists */
    list_init(open_frame_list_user);
    list_init(open_frame_list_kernel);
    list_init(init_page_dir_sup);
    list_init(open_page_entry);
    
    for (page = 0; page < num_frame_used; page++)
    {
        /* Will already be sorted by physical address */
        list_push_back(init_page_dir_sup, &(page_entry_list[page].elem));
    }
    /* Add leftover page entries to open list */
    for (page = num_frame_used; page < NUM_PAGE_ENTRY; page++)
    {
        list_push_back(open_page_entry, &(page_entry_list[page].elem));
    }
    /* Build open frame table entries, don't care about entry value */
    if (num_frame_used > kernel_frames)
    {
        PANIC("Falloc_init used more frames than kernel has");
    }
    /* Add unused kernel frames to the kernel open list. */
    for (i = num_frame_used; i < kernel_frames; i++)
    {
        frame_list_kernel[i].faddr = (void *) (i * PGSIZE);
        list_push_back(open_frame_list_kernel, &(frame_list_kernel[i].open_elem));
    }
    /* Add unused user frames to the user open list. */
    for (i = 0; i < user_frames; i++)
    {
        frame_list_user[i].faddr = (void *) (i * PGSIZE);
        list_push_back(open_frame_list_user, &(frame_list_user[i].open_elem));
    }
}

/*! Returns a frame from the space specified by USER (true = user space, false =
    kernel space). */
struct frame *get_frame_addr(bool user)
{
    struct list_elem *elem;
    struct frame *frame_entry;
    struct list *open_frame_list;
    struct thread *t = thread_current();
    
    /* Choose the correct frame list. */
    if (user)
    {
        open_frame_list = open_frame_list_user;
    }
    else
    {
        open_frame_list = open_frame_list_kernel;
    }

    /* If attempting to allocate frame, and out of frames, try evicting. */
    if (list_empty(open_frame_list))
    {
        frame_evict(user);
    }
    
    /* If still no empty frame, panic system */
    if (list_empty(open_frame_list))
    {
        PANIC("falloc_get: out of frames");
    }
    /* Otherwise, get an open frame. */
    elem = list_pop_front(open_frame_list);

    /* Remove frame from list of open frames. */
    frame_entry = list_entry(elem, struct frame, open_elem);
    
    /* Add to process list of frames if in user space. */
    if (user) {
        list_push_back(&(t->frames), &(frame_entry->process_elem));
    }

    return frame_entry;
}

/*! Obtains a single free frame and returns its kernel virtual
    address.
    If no frames are available, the kernel panics. */
void *falloc_get_frame(void *upage, bool user, struct page_entry *sup_entry)
{
    void *frame;
    struct thread *t = thread_current();
    uint32_t *pagedir = t->pagedir;                     /* Get page directory */
    uint32_t *pte;
    struct frame *frame_entry;
    uint32_t bytes_read;

    /* Get the frame entry. */
    frame_entry = get_frame_addr(user);
    frame = frame_entry->faddr;

    /* NOTE: need to force read/write bit to always be valid. */
    pte = lookup_page(init_page_dir, upage, false);
    if (pte != NULL) {
        ASSERT(pte_is_pinned(*pte));
        ASSERT(!user);
        pagedir = init_page_dir;
    }

    ASSERT(pagedir != NULL);
    ASSERT(!(*pte & PTE_P));
    if (user) {
        pagedir_set_page(pagedir, upage, frame, pte_is_read_write(*pte));
    }
    else {
        pagedir_set_page_kernel(pagedir, upage, frame, pte_is_read_write(*pte));
    }
    pte = lookup_page(pagedir, upage, false);
    *pte |= PTE_P;

    /* Associate frame with page. */
    frame_entry->pte = pte;
    frame_entry->sup_entry = sup_entry;
    frame_entry->owner = t;
    
    /* Load requested data into page. */
    switch (sup_entry->source)
    {
    case ZERO_PAGE:     /* Zero the page. */
        memset(upage, 0, PGSIZE);
        break;
    case FILE_PAGE:     /* Read file into page. */
        bytes_read = (uint32_t) file_read(sup_entry->data, upage, (off_t) PGSIZE);
        memset(upage + bytes_read, 0,  PGSIZE - bytes_read);
    case SWAP_PAGE:     /* Read data in from swap. */
        swap_read_page(sup_entry->data, upage);
        swalloc_free_swap(sup_entry->data);
        break;
    case FRAME_PAGE:    /* Cannot have page already in frame */
        ASSERT(false);
    }
    
    sup_entry->source = FRAME_PAGE;
    sup_entry->data = frame;
    
    return frame;
}

/*! Frees the frame at FRAME. */
void falloc_free_frame(void *frame)
{
    struct frame *frame_entry = addr_to_frame(frame);
    uint32_t *pd = thread_current()->pagedir;       /* Get page directory */
    uint32_t pte = *(frame_entry->pte);
    void *upage = frame_entry->sup_entry->vaddr;    /* Get virtual addr */
    struct list *open_frame_list;
    bool user_space;
    
#ifndef NDEBUG
    memset(frame, 0xcc, PGSIZE);
#endif

    /* If it wasn't allocated, just return. */
    if (!pte_is_present(pte))
    {
        return;
    }

    /* Need to figure out if in kernel or user list. */
    if (is_user_vaddr(upage))
    {
        open_frame_list = open_frame_list_user;
        user_space = true;
    }
    else
    {
        open_frame_list = open_frame_list_kernel;
        user_space = false;
    }

    /* Remove page from page directory. */
    pagedir_clear_page(pd, upage);
    
    /* Add frame struct back to open list. */
    list_push_back(open_frame_list, &(frame_entry->open_elem));
    
    /* Remove from process list if in user space. */
    if (user_space) {
        list_remove(&(frame_entry->process_elem));
    }
    
}

/*! Returns a pointer to the frame struct for the passed address. */
static struct frame *addr_to_frame(void *frame_addr) {
    return &(frame_list_kernel[pg_no(frame_addr)]);
}

/* TODO: implement frame_clean and frame_evict */
/*! Selects a frame for eviction */
void frame_evict(bool user)
{
    return;
}