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

#include "vm/falloc.h"
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
#include "threads/synch.h"
#include "threads/vaddr.h"

static frame_entry *addr_to_frame(void *frame_addr);

// TODO: Need a list of frame structs
static struct list open_frame_list_user;
static struct list open_frame_list_kernel;

static uint32_t user_frames;
static uint32_t kernel_frames;


/*! Two pools: one for kernel data, one for user frames. */
static struct pool kernel_pool, user_pool;

/*! Initializes the frame allocator.  At most USER_FRAME_LIMIT
    frames are put into the user pool. */
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

    /* Initialize frame table, take space out of kernel frames */
    frame_list_kernel = ptov(1024 * 1024);
    frame_list_user   = ptov(1024 * 1024) + sizeof(struct frame) * (1024*1024/PGSIZE + kernel_frames);
    uint32_t num_frame_used = 1024 * 1024 + sizeof(struct frame) * (user_frames + kernel_frames)
    num_frame_used = pg_round_up(num_frame_used) / PGSIZE;
    
    /* Map and pin the first num_frame_used frames into init_page_dir */
    pd = init_page_dir = ptov(num_frame_used * PGSIZE);
    num_frame_used++;
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
            pt = ptov(num_frame_used * PGSIZE);;
            num_frame_used++;
            pd[pde_idx] = pde_create(pt);
        }

        pt[pte_idx] = pte_is_pinned(pte_create_kernel(vaddr, !in_kernel_text));
        
        /* Initialize frame entries */
        frame_list_kernel[i].pte = pt[pte_idx];
        frame_list_kernel[i].sup_entry = NULL;
        frame_list_kernel[i].owner = NULL;
    }
    /* Build open frame table entries, don't care about entry value */
    if (num_frame_used > kernel_frames)
    {
        PANIC("Falloc_init used more frames than kernel has");
    }
    for (i = num_frame_used < kernel_frames; ; i++)
    {
        list_push_back(&open_frame_list_kernel, frame_list_kernel[i].open_elem);
    }
    for (i = 0; i < user_frames; i++)
    {
        list_push_back(&open_frame_list_user, frame_list_user[i].open_elem);
    }
}

/*! Obtains a single free frame and returns its kernel virtual
    address.
    If PAL_USER is set, the frame is obtained from the user pool,
    otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
    then the frame is filled with zeros.  If no frames are
    available, returns a null pointer, unless PAL_ASSERT is set in
    FLAGS, in which case the kernel panics. */
void * falloc_get_frame(void *upage, enum alloc_flags flags, struct page_entry *sup_entry) {
 struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
    void *frame;
    size_t frame_idx;
    struct thread *t = thread_current();
    uint32_t *pagedir = t->pagedir;                     /* Get page directory */
    uint32_t *pte = lookup_page(pagedir, upage, true);  /* Get table entry */
    struct list_elem elem;
    struct frame *frame_entry;
    struct list *open_frame_list;

    /* Choose the correct frame list. */
    if (alloc_flags & PAL_USER) {
        open_frame_list = &open_frame_list_user;
    } else {
        open_frame_list = &open_frame_list_kernel;
    }
    
    /* DEBUG: If attempting to allocate frame, and out of frames, panic. */
    if (list_empty(open_frame_list) {
        PANIC("falloc_get: out of kernel frames");
        
    /* Otherwise, get an open frame. */
    } else {
        elem = list_pop_front(open_frame_list);
    }
    
    if (flags & PAL_ZERO) {
        // TODO: zero frame if necessary
        //memset(frame, 0, PGSIZE);
        
    }
    
    /* TODO: if failed to get frame (even after evicting) and assert flag set, panic */
    //if (flags & PAL_ASSERT) {
    //    PANIC("falloc_get: out of frames");
    //}
    
    /* Remove frame from list of open frames. */
    frame_entry = list_entry(elem, struct frame, open_elem);
    
    /* TODO: associate frame with page. */
    frame_entry->pte = pte;
    frame_entry->sup_entry = sup_entry;
    frame_entry->owner = t;
    // TODO: add to process list?
    
    /* NOTE: need to force read/write bit to always be valid. */
    pagedir_set_page(pagedir, upage, frame, pte_is_read_write(*pte));
    
    /* TODO: Associate frame address with frame entry struct. */
    //frame_list_kernel[pg_no(frame)] = frame_entry;  // TODO: May want to change this to be more robust
    
    return frames;
}

/*! Frees the frame at frame. */
void falloc_free_frame(void *frame) {
    struct pool *pool;
    size_t frame_idx;
    struct frame *frame_entry = addr_to_frame(frame);

    ASSERT(pg_ofs(frame) == 0);
    if (frame == NULL)
        return;

    if (frame_from_pool(&kernel_pool, frame))
        pool = &kernel_pool;
    else if (frame_from_pool(&user_pool, frame))
        pool = &user_pool;
    else
        NOT_REACHED();

    frame_idx = pg_no(frame) - pg_no(pool->base);

#ifndef NDEBUG
    memset(frame, 0xcc, PGSIZE);
#endif

    /* If it wasn't allocated, just return. */
    if (!pte_is_present(*(frame_entry->pte)) {
        return;
    }
    
    /* TODO: Need to figure out if in kernel or user list */
    /* Add frame struct back to open list. */
    list_push_back(&open_frame_list, &(frame_entry->open_elem));
    
}

/*! Initializes pool P as starting at START and ending at END,
    naming it NAME for debugging purposes. */
void init_pool(struct pool *f, void *base, size_t frame_cnt,
                      const char *name) {
    /* We'll put the pool's used_map at its base.
       Calculate the space needed for the bitmap
       and subtract it from the pool's size. */
    size_t bm_frames = DIV_ROUND_UP(bitmap_buf_size(frame_cnt), PGSIZE);
    if (bm_frames > frame_cnt)
        PANIC("Not enough memory in %s for bitmap.", name);
    frame_cnt -= bm_frames;

    printf("%zu frames available in %s.\n", frame_cnt, name);

    /* Initialize the pool. */
    lock_init(&f->lock);
    f->used_map = bitmap_create_in_buf(frame_cnt, base, bm_frames * PGSIZE);
    f->base = base + bm_frames * PGSIZE;
}

/*! Returns true if frame was allocated from POOL, false otherwise. */
bool frame_from_pool(const struct pool *pool, void *frame) {
    size_t frame_no = pg_no(frame);
    size_t start_frame = pg_no(pool->base);
    size_t end_frame = start_frame + bitmap_size(pool->used_map);

    return frame_no >= start_frame && frame_no < end_frame;
}

static bool frame_from_list(const struct frame_list, void *frame) {
    // use a bit in the pte to designate kernel vs user space? (better than
    // searching lists)
}
/*! Returns a pointer to the frame struct for the passed address. */
static frame *addr_to_frame(void *frame_addr) {
    return &(frame_list_kernel[pg_no(frame)]);
}

// TODO: implement frame_clean
// TODO: implement frame_evict
