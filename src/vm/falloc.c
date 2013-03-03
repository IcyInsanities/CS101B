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

/*! A memory pool. */
struct pool {
    struct lock lock;                   /*!< Mutual exclusion. */
    struct bitmap *used_map;            /*!< Bitmap of free frames. */
    uint8_t *base;                      /*!< Base of pool. */
};

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
}

/*! Obtains and returns a group of frame_CNT contiguous free frames.
    If PAL_USER is set, the frames are obtained from the user pool,
    otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
    then the frames are filled with zeros.  If too few frames are
    available, returns a null pointer, unless PAL_ASSERT is set in
    FLAGS, in which case the kernel panics. */
void * falloc_get_multiple(enum palloc_flags flags, size_t frame_cnt) {
    struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
    void *frames;
    size_t frame_idx;

    if (frame_cnt == 0)
        return NULL;

    lock_acquire(&pool->lock);
    frame_idx = bitmap_scan_and_flip(pool->used_map, 0, frame_cnt, false);
    lock_release(&pool->lock);

    if (frame_idx != BITMAP_ERROR)
        frames = pool->base + PGSIZE * frame_idx;
    else
        frames = NULL;

    if (frames != NULL) {
        if (flags & PAL_ZERO)
            memset(frames, 0, PGSIZE * frame_cnt);
    }
    else {
        if (flags & PAL_ASSERT)
            PANIC("palloc_get: out of frames");
    }

    return frames;
}

/*! Obtains a single free frame and returns its kernel virtual
    address.
    If PAL_USER is set, the frame is obtained from the user pool,
    otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
    then the frame is filled with zeros.  If no frames are
    available, returns a null pointer, unless PAL_ASSERT is set in
    FLAGS, in which case the kernel panics. */
void * falloc_get_frame(enum palloc_flags flags) {
    return palloc_get_multiple(flags, 1);
}

/*! Frees the FRAME_CNT frames starting at FRAMES. */
void falloc_free_multiple(void *frames, size_t frame_cnt) {
    struct pool *pool;
    size_t frame_idx;

    ASSERT(pg_ofs(frames) == 0);
    if (frames == NULL || frame_cnt == 0)
        return;

    if (frame_from_pool(&kernel_pool, frames))
        pool = &kernel_pool;
    else if (frame_from_pool(&user_pool, frames))
        pool = &user_pool;
    else
        NOT_REACHED();

    frame_idx = pg_no(frames) - pg_no(pool->base);

#ifndef NDEBUG
    memset(frames, 0xcc, PGSIZE * frame_cnt);
#endif

    ASSERT(bitmap_all(pool->used_map, frame_idx, frame_cnt));
    bitmap_set_multiple(pool->used_map, frame_idx, frame_cnt, false);
}

/*! Frees the frame at frame. */
void falloc_free_frame(void *frame) {
    falloc_free_multiple(frame, 1);
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

