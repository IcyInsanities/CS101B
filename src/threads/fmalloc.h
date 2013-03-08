#ifndef THREADS_FMALLOC_H
#define THREADS_FMALLOC_H

#include <debug.h>
#include <stddef.h>

void fmalloc_init(void);
void *fmalloc(size_t) __attribute__ ((malloc));
void *fcalloc(size_t, size_t) __attribute__ ((malloc));
void *frealloc(void *, size_t);
void ffree(void *);

#endif /* threads/fmalloc.h */

