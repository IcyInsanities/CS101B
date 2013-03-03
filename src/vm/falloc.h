#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stddef.h>

/* How to allocate frames. */
enum falloc_flags
  {
    FAL_ASSERT = 001,           /* Panic on failure. */
    FAL_ZERO = 002,             /* Zero frame contents. */
    FAL_USER = 004              /* User frame. */
  };

void falloc_init (size_t user_page_limit);
void *falloc_get_frame (enum falloc_flags);
void *falloc_get_multiple (enum falloc_flags, size_t page_cnt);
void falloc_free_frame (void *);
void falloc_free_multiple (void *, size_t page_cnt);

#endif /* vm/frame.h */
