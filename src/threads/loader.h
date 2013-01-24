#ifndef THREADS_LOADER_H
#define THREADS_LOADER_H

/* Constant for offset of the bottom of the stack. */
#define LOADER_STACK_OFFSET     0xf000  /* Base offset of the stack */

/* Constant for hard drive read buffer. */
#define LOADER_HD_RD_BUF_SEG    0x2000  /* Address of HD read buffer */

/* Constants for disk access packet. */
#define DISK_ACCESS_PACKET_SIZE 16  /* Size of disk access packet in bytes */
#define EXTENDED_READ_SECTOR  0x42  /* Value to pass int 13 to use extended
                                     * read sector
                                     */

/* Constant for boot signature */
#define DRIVE_SIG_OFFSET    0x01fe  /* Offset of drive signature */
#define BOOT_SIG            0xaa55  /* Signature for bootable drive */

/* Constants for partitions */
#define FIRST_PART_OFFSET   0x01be  /* Offset of the first partition */
#define LAST_PART_OFFSET    0x01fe  /* Offset of the last partition */
#define PART_ENTRY_SIZE     0x10    /* Partition entry size */
#define BOOT_PART           0x80    /* Value for a boot partition */
#define PINTOS_PART         0x20    /* Value for a Pintos partition */

/* Constants fixed by the PC BIOS. */
#define LOADER_BASE 0x7c00      /* Physical address of loader's base. */
#define LOADER_END  0x7e00      /* Physical address of end of loader. */

/* Physical address of kernel base. */
#define LOADER_KERN_BASE 0x20000       /* 128 kB. */

/* Kernel virtual address at which all physical memory is mapped.
   Must be aligned on a 4 MB boundary. */
#define LOADER_PHYS_BASE 0xc0000000     /* 3 GB. */

/* Important loader physical addresses. */
#define LOADER_SIG (LOADER_END - LOADER_SIG_LEN)   /* 0xaa55 BIOS signature. */
#define LOADER_PARTS (LOADER_SIG - LOADER_PARTS_LEN)     /* Partition table. */
#define LOADER_ARGS (LOADER_PARTS - LOADER_ARGS_LEN)   /* Command-line args. */
#define LOADER_ARG_CNT (LOADER_ARGS - LOADER_ARG_CNT_LEN) /* Number of args. */

/* Sizes of loader data structures. */
#define LOADER_SIG_LEN 2
#define LOADER_PARTS_LEN 64
#define LOADER_ARGS_LEN 128
#define LOADER_ARG_CNT_LEN 4

/* GDT selectors defined by loader.
   More selectors are defined by userprog/gdt.h. */
#define SEL_NULL        0x00    /* Null selector. */
#define SEL_KCSEG       0x08    /* Kernel code selector. */
#define SEL_KDSEG       0x10    /* Kernel data selector. */

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Amount of physical memory, in 4 kB pages. */
extern uint32_t init_ram_pages;
#endif

#endif /* threads/loader.h */
