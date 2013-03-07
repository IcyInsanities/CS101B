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
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

// TODO: Need a list of frame structs
static struct list open_frame_list;
static void **allocated_frames;

/*! Two pools: one for kernel data, one for user frames. */
static struct pool kernel_pool, user_pool;

static void init_pool(struct pool *, void *base, size_t frame_cnt,
                      const char *name);
static bool frame_from_pool(const struct pool *, void *frame);

/*! Initializes the frame allocator.  At most USER_FRAME_LIMIT
    frames are put into the user pool. */
void falloc_init(size_t user_frame_limit) {
    /* Free memory starts at 1 MB and runs to the end of RAM. */
    uint8_t *free_start = ptov(1024 * 1024);
    uint8_t *free_end = ptov(init_ram_frames * PGSIZE);
    size_t free_frames = (free_end - free_start) / PGSIZE;
    size_t user_frames = free_frames / 2;
    size_t kernel_frames;
    if (user_frames > user_frame_limit)
        user_frames = user_frame_limit;
    kernel_frames = free_frames - user_frames;

    /* Give half of memory to kernel, half to user. */
    init_pool(&kernel_pool, free_start, kernel_frames, "kernel pool");
    init_pool(&user_pool, free_start + kernel_frames * PGSIZE,
              user_frames, "user pool");
              
    /* Initialize the frame list. */
    list_init(&frame_list);
    
    /* Fill the list, pinning each frame in it? */
    
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

    lock_acquire(&pool->lock);
    frame_idx = bitmap_scan_and_flip(pool->used_map, 0, 1, false);
    lock_release(&pool->lock);

    if (frame_idx != BITMAP_ERROR)
        frames = pool->base + PGSIZE * frame_idx;
    else
        frame = NULL;

    if (frame != NULL) {
        if (flags & PAL_ZERO)
            memset(frame, 0, PGSIZE);
    }
    else {
        if (flags & PAL_ASSERT)
            PANIC("falloc_get: out of frames");
    }
    
    /* Remove frame from list of open frames. */
    elem = list_pop_front(&open_frame_list);
    frame_entry = list_entry(elem, struct frame, open_elem);
    
    /* TODO: associate frame with page. */
    frame_entry->pte = pte;
    frame_entry->sup_entry = sup_entry;
    frame_entry->owner = t;
    
    /* NOTE: need to force read/write bit to always be valid. */
    pagedir_set_page(pagedir, upage, frame, pte_is_read_write(*pte));
    
    /* TODO: Associate frame address with frame entry struct. */
    frame_list_kernel[pg_no(frame)] = frame_entry; 
    
    
    return frames;
}

/*! Frees the frame at frame. */
void falloc_free_frame(void *frame) {
    struct pool *pool;
    size_t frame_idx;
    struct frame *frame_entry = pg_no(frame);

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

    /* Add frame struct back to open list. */
    list_push_back(&open_frame_list, &(frame_entry->open_elem));
    
    /* Disassociate frame address from frame struct. */
    frame_list_kernel[pg_no(frame)] = NULL;
    
    ASSERT(bitmap_all(pool->used_map, frame_idx, 1));
    bitmap_set_multiple(pool->used_map, frame_idx, 1, false);
}

/*! Initializes pool P as starting at START and ending at END,
    naming it NAME for debugging purposes. */
static void init_pool(struct pool *f, void *base, size_t frame_cnt,
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
static bool frame_from_pool(const struct pool *pool, void *frame) {
    size_t frame_no = pg_no(frame);
    size_t start_frame = pg_no(pool->base);
    size_t end_frame = start_frame + bitmap_size(pool->used_map);

    return frame_no >= start_frame && frame_no < end_frame;
}

// TODO: implement frame_clean
// TODO: implement frame_evict
