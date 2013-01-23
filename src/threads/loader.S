#include "threads/loader.h"

####; Kernel loader.

####; This code should be stored in the first sector of a hard disk.
####; When the BIOS runs, it loads this code at physical address
####; 0x7c00-0x7e00 (512 bytes) and jumps to the beginning of it,
####; in real mode.  The loader loads the kernel into memory and jumps
####; to its entry point, which is the start function in start.S.
####;
####; The BIOS passes in the drive that the loader was read from as
####; DL, with floppy drives numbered 0x00, 0x01, ... and hard drives
####; numbered 0x80, 0x81, ...  We want to support booting a kernel on
####; a different drive from the loader, so we don't take advantage of
####; this.

# Runs in real mode, which is a 16-bit segment.
    .code16

# Set up segment registers.
# Set stack to grow downward from 60 kB (after boot, the kernel
# continues to use this stack for its initial thread).

    sub %ax, %ax
    mov %ax, %ds
    mov %ax, %ss
    mov $0xf000, %esp

# Configure serial port so we can report progress without connected VGA.
# See [IntrList] for details.
    sub %dx, %dx            # Serial port 0.
    mov $0xe3, %al          # 9600 bps, N-8-1.
                    # AH is already 0 (Initialize Port).
    int $0x14           # Destroys AX.

    call puts
    .string "PiLo"

####; TODO:  ITERATE THROUGH ALL HARD DISKS IN THE SYSTEM, LOOKING FOR A
####;        BOOTABLE PINTOS PARTITION TO LOAD AND START.
####;
####;        Note that there are only ~300 bytes available for the entire
####;        Pintos bootloader, as Pintos commands are stored in the
####;        region immediately before the partition table in the sector.
####;        Sooo... be concise!
####;
####;        If you want to create helper routines, there is a TODO toward
####;        the end of this file that shows where they should go.
.intel_syntax noprefix
setup_os_mem:       #; Set up ES:DI to 0x20000 to dump harddrive data
    MOV     AX, 0x2000
    MOV     ES, AX      #; Top 16 bits of memory
    XOR     DI, DI      #; Clear offset

hd_search_start:
    SUB     ESP, 16                     #; Allocate disk access packet on stack
    MOV     ESI, ESP                    #; Set SI to point to disk access packet
                                        #; DS is already SS, so DS:SI points here
    MOV     DWORD PTR [SI   ], 0x00010010   #; Write 0x10 as size and 0x00 for second byte
                                            #; Read 8 segments of 512 bytes (4KiB)
    MOV     DWORD PTR [SI+ 4], 0x00020000   #; segment:offset of buffer
    MOV     DWORD PTR [SI+ 8],        0x0   #; Read at 0 index sector
    MOV     DWORD PTR [SI+12],        0x0   #;
    MOV     DL, 0x80                    #; Set to read from first harddrive
hd_search_body:
    MOV     AH, 0x42        #; Set to use extended read-sector
    INT     0x13            #; Call system to read harddrive
##;;     JMP     hd_search_fail  #; Check carry flag for an error, assume no more hds
##;;                             #; so terminate system by indicating failure
##;;     CMP     WORD PTR DS:[SI+0x1FE], 0xAA55    #; Check drive signature for boot
##;;     JNE     hd_search_next
##;;   part_search_start:        #; Iterate over 4 partitions for boot
##;;     MOV     BX, 0x01BE      #; Set to start at partition entry #1
##;;   part_search_body:         #; Check current partition for boot
##;;     CMP     BYTE PTR ES:[BX  ], 0x80  #; Check if boot partition
##;;     JNE     part_search_next        #; Not boot partition, continue search
##;;     CMP     BYTE PTR ES:[BX+4], 0x20  #; Check if pintos partition
##;;     JE      hd_boot_found       #; Found pintos partition, start boot process
##;;     #;JMP   part_search_next    #; Fall through to check next partition
##;;   part_search_next:
##;;     ADD     BX, 0x10        #; Skip to next partition
##;;     CMP     BX, 0x01FE      #; 5th partition location, invalid so end loop on hd
##;;     JAE     hd_search_next  #; Out of partitions, go to next harddrive
##;;     JMP     part_search_body    #; Otherwise, go to next partition
##;;
##;; hd_search_next:
##;;     INC     DL              #; Move to next harddrive
##;;     JZ      hd_search_fail  #; Failure if DL overflows
##;;     JMP     hd_search_body  #; Check next harddrive
##;;
##;; hd_search_fail:     #; Ran out of hds before finding boot partition, halt system
##;;     CALL    puts
##;;     .string "\rNo boot hd found\r\r"
##;;     INT     0x18    #; Inform system that no boot partition exists
##;;
hd_boot_found:
    CALL    puts
    .string "\r\rBoot hd found!!!!\r\r"
    INT     0x18    #; Inform system that no boot partition exists
.att_syntax

####; TODO:  LOADING CODE ENDS HERE!

####; Transfer control to the kernel that we loaded.  We read the start
####; address out of the ELF header (see [ELF1]) and convert it from a
####; 32-bit linear address into a 16:16 segment:offset address for
####; real mode, then jump to the converted address.  The 80x86 doesn't
####; have an instruction to jump to an absolute segment:offset kept in
####; registers, so in fact we store the address in a temporary memory
####; location, then jump indirectly through that location.  To save 4
####; bytes in the loader, we reuse 4 bytes of the loader's code for
####; this temporary pointer.

    mov $0x2000, %ax
    mov %ax, %es
    mov %es:0x18, %dx
    mov %dx, start
    movw $0x2000, start + 2
    ljmp *start

####; The following label is a placeholder for the preceding code to use
####; to set up for the jump to the Pintos kernel.  Of course, the start
####; label also points to the puts function; when the previous code runs,
####; it will clobber a few of the bytes at the start of the puts function,
####; but hey we're done bootloading, so we don't need it anymore.

start:

####; Print string subroutine.  To save space in the loader, this
####; subroutine takes its null-terminated string argument from the
####; code stream just after the call, and then returns to the byte
####; just after the terminating null.  This subroutine preserves all
####; general-purpose registers.

puts:
    XCHG    %si, %ss:(%esp)
    PUSH    %ax
next_char:
    MOV     %cs:(%si), %al
    INC     %si
    TEST    %al, %al
    JZ      1f
    CALL    putc
    JMP     next_char
1:  POP     %ax
    XCHG    %si, %ss:(%esp)
    RET

####; Character output subroutine.  Prints the character in AL to the
####; VGA display and serial port 0, using BIOS services (see
####; [IntrList]).  Preserves all general-purpose registers.
####;
####; If called upon to output a carriage return, this subroutine
####; automatically supplies the following line feed.

putc:   pusha

1:  sub %bh, %bh            # Page 0.
    mov $0x0e, %ah          # Teletype output service.
    int $0x10

    mov $0x01, %ah          # Serial port output service.
    sub %dx, %dx            # Serial port 0.
2:  int $0x14           # Destroys AH.
    test $0x80, %ah         # Output timed out?
    jz 3f
    movw $0x9090, 2b        # Turn "int $0x14" above into NOPs.

3:
    cmp $'\r', %al
    jne popa_ret
    mov $'\n', %al
    jmp 1b

popa_ret:
    popa
    ret             # Error code still in CF


####; TODO:  YOU CAN PUT HELPER FUNCTIONS HERE IF YOU WISH.


####; Command-line arguments and their count.
####; This is written by the `pintos' utility and read by the kernel.
####; The loader itself does not do anything with the command line.
    .org LOADER_ARG_CNT - LOADER_BASE
    .fill LOADER_ARG_CNT_LEN, 1, 0

    .org LOADER_ARGS - LOADER_BASE
    .fill LOADER_ARGS_LEN, 1, 0

####; Partition table.
    .org LOADER_PARTS - LOADER_BASE
    .fill LOADER_PARTS_LEN, 1, 0

####; Boot-sector signature for BIOS inspection.
    .org LOADER_SIG - LOADER_BASE
    .word 0xaa55

