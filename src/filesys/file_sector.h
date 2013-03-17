#ifndef FILESYS_FILE_SECTOR_H
#define FILESYS_FILE_SECTOR_H

typedef uint32_t file_sector;

#define FILE_SEC_ADDR_BITS  25
#define FILE_SEC_BN_BITS    6

#define FILE_SEC_BN_SHIFT   FILE_SEC_ADDR_BITS
#define FILE_SEC_P_SHIFT    FILE_SEC_ADDR_BITS + FILE_SEC_BN_BITS

#define FILE_SEC_ADDR       0x01ffffff  // Address bits.
#define FILE_SEC_BLOCK_NUM  0x7e000000  // Block cache index.
#define FILE_SEC_PRESENT    0x80000000  // 1=present, 0=not present.

static inline void *file_sec_get_addr(file_sector sector) {
    return (void *) sector & FILE_SEC_ADDR;
}

static inline uint32_t file_sec_get_block_num(file_sector sector) {
    return (uint32_t) sector >> FILE_SEC_BN_SHIFT;
}

static inline bool file_sec_is_present(file_sector sector) {
    return (bool) sector >> FILE_SEC_P_SHIFT;
}

static inline file_sector file_sec_set_present(file_sector sector, bool present) {
    return (file_sector) ((sector & ~FILE_SEC_PRESENT) | (present ? FILE_SEC_PRESENT : false));
}

// Mark mask bits from block num to prevent largerer than valid values?
static inline file_sector file_sec_set_block_num(file_sector sector, uint32_t block_num) {
    return (file_sector) (sector & ~FILE_SEC_BLOCK_NUM) | (block_num << BN_SHIFT);
}

static inline file_sector file_sec_set_addr(file_sector sector, void *addr) {
    return (file_sector) (sector & ~FILE_SEC_ADDR) | ((uint32_t) addr);
}

#endif /* filesys/file_sector.h */