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

#; Runs in real mode, which is a 16-bit segment.
    .code16

#; Switch to human readable assembly syntax and not silly AT&T syntax
.intel_syntax noprefix
    
#; Set up segment registers.
#; Set stack to grow downward from 60 kB (after boot, the kernel
#; continues to use this stack for its initial thread).

    XOR     AX, AX
    MOV     DS, AX
    MOV     SS, AX
    MOV     ESP, LOADER_STACK_OFFSET

#; Configure serial port so we can report progress without connected VGA.
#; See [IntrList] for details.
    XOR     DX, DX      #; Serial port 0.
    MOV     AL, 0xE3    #; 9600 bps, N-8-1.
                        #; AH is already 0 (Initialize Port).
    INT     0x14        #; Destroys AX.

    CALL    puts
    .string "PiLo"

#;  Look through hard disks looking for a bootable Pintos partition.  If
#;  found, load it.
setup_os_mem:       #; Set up ES:DI to 0x20000 to dump harddrive data
    MOV     AX, LOADER_HD_RD_BUF_SEG    #; Setup ES to for dump buffer
    MOV     ES, AX                      #; Set the segment for the dump buffer

hd_search_start:
    MOV     ESI, ESP                    #; Set SI to point to disk access packet
                                        #; on the stack.
                                        #; DS is already SS, so DS:SI points here
    SUB     ESP, DISK_ACCESS_PACKET_SIZE    #; Allocate disk access packet on 
                                            #; stack
  zero_dap:         #; Clear values in disk access packet to minize write
                    #; instructions
    MOV     CX, DISK_ACCESS_PACKET_SIZE #; Set to clear 16 bytes of 0s
  zero_dap_body:    #; Loop body clearing disk access packet
    MOV     BYTE PTR [SI-1], 0x00       #; Clear current byte
    DEC     SI                          #; Move SI back one
    LOOP    zero_dap_body               #; Go to next byte if not done clearing
    #;        zero_dap_done             #; Done clearing, continue
  zero_dap_done:
    MOV     BYTE PTR [SI   ], DISK_ACCESS_PACKET_SIZE   #; Set the packet size 
                                                        #; in bytes 
    MOV     BYTE PTR [SI+ 2], 0x01      #; Set read size (1 block of 512 bytes)
    MOV     BYTE PTR [SI+ 7], 0x20      #; Pass buffer to dump read data
    MOV     DL, 0x80                    #; Set to read from first harddrive
hd_search_body:
    MOV     AH, EXTENDED_READ_SECTOR    #; Set to use extended read-sector
    INT     0x13            #; Call system to read harddrive
    JC      hd_search_fail  #; Check carry flag for an error, assume no more HDs
                            #; so terminate system by indicating failure
    CMP     WORD PTR ES:[DRIVE_SIG_OFFSET], BOOT_SIG    #; Check drive signature 
                                                        #; to see
                                           #; if it is bootable
    JNE     hd_search_next                 #; If it isn't, check next drive
  part_search_start:        #; Iterate over 4 partitions for boot
    MOV     BX, FIRST_PART_OFFSET       #; Set to start at partition entry #1
  part_search_body:         #; Check if current partition is bootable
    CMP     BYTE PTR ES:[BX  ], BOOT_PART   #; Check if boot partition
    JNE     part_search_next                #; Not boot partition, continue 
                                            #; search
    CMP     BYTE PTR ES:[BX+4], PINTOS_PART #; Check if pintos partition
    JE      hd_boot_found       #; Found pintos partition, start boot process
    #;JMP   part_search_next    #; Fall through to check next partition
  part_search_next:
    ADD     BX, PART_ENTRY_SIZE #; Skip to next partition
    CMP     BX, LAST_PART_OFFSET + PART_ENTRY_SIZE  #; Passed last partition, 
                                                    #; invalid so end loop on HD
    JB      part_search_body    #; Not at end, go to next partition
    #;JMP     hd_search_next    #; Otherwise, go to next harddrive

hd_search_next:
    INC     DL                  #; Move to next harddrive
    JNZ     hd_search_body      #; Check next harddrive if DL did not overflow
    ;#JMP     hd_search_fail    #; Otherwise, error in finding boot partition

hd_search_fail:     #; Ran out of hds before finding boot partition, halt system
    CALL    puts
    .string "\rNo boot HD\r"
    INT     0x18    #; Inform system that no boot partition exists

hd_boot_found:      #; Notify user that HD found, start loading
    CALL    puts
    .string "\rLoading hd"
    MOV     AL, DL                      #; Convert drive number with Pintos
    SUB     AL, 0x80-'a'                #; on it to a letter
    CALL    putc                        #; Print drive letter to user
    LEA     AX, [BX - 0x01BE + '1'*16]  #; Get the number of the partition
    SHR     AX, 4                       #; containing Pintos
    CALL    putc

part_load_start:
    MOV     ECX, ES:[BX+ 8]     #; Get the LBA address of starting sector
    MOV     [SI+8], ECX         #; Update disk access packet with LBA address
    MOV     ECX, ES:[BX+12]     #; Get the number of sectors in partition
    CMP     ECX, 1024           #; Check if more sectors than 512kb
    JB      part_load_sectors   #; If less than 512kb, start loading
    #;JMP   bound_sector_size   #: Otherwise need to bound read size to 512kb
  bound_sector_size:            #; Cap at 512kb, or 1024 512B chunks
    MOV     ECX, 1024           #; 
    #;JMP   part_load_sectors   #; Now continue with setup, load sectors
part_load_sectors:
    MOV     AH, EXTENDED_READ_SECTOR    #; Set to use extended read-sector
    INT     0x13            #; Call system to read harddrive
    JC      sector_fail     #; Check carry flag for an error, indicates sector
                            #; failure in loading
part_load_next:             #; Move to load next block of sectors
    CALL    puts            #; Output a "." to indicate progress
    .string "."
    INC     DWORD PTR [SI+8]            #; Move LBA by 1 sectors
    ADD     WORD PTR  [SI+6], 0x0020    #; Increment segment by 512B
    LOOP    part_load_sectors   #; Check if done loading, otherwise load next
                                #; sector
    JMP     loading_done        #; Out of sectors to load, finished
    
sector_fail:        #; Error reading sectors, halt system
    CALL    puts
    .string "\rLoad failed\r"
    INT     0x18    #; Inform system that no boot partition exists
    
loading_done:       #; Finished loading, transfer control after telling user
    CALL    puts
    .string "\rDONE\r"
    NOP             #; Just because we have an extra byte

####; Transfer control to the kernel that we loaded.  We read the start
####; address out of the ELF header (see [ELF1]) and convert it from a
####; 32-bit linear address into a 16:16 segment:offset address for
####; real mode, then jump to the converted address.  The 80x86 doesn't
####; have an instruction to jump to an absolute segment:offset kept in
####; registers, so in fact we store the address in a temporary memory
####; location, then jump indirectly through that location.  To save 4
####; bytes in the loader, we reuse 4 bytes of the loader's code for
####; this temporary pointer.

    MOV     DX, ES:0x18
    MOV     [start], DX
    MOV     WORD PTR [start+2], 0x2000
    LJMP    start

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
    XCHG    SS:[ESP], SI
    PUSH    AX
next_char:
    MOV     AL, CS:[SI]
    INC     SI
    TEST    AL, AL
    JZ      1f
    CALL    putc
    JMP     next_char
1:  POP     AX
    XCHG    SS:[ESP], SI
    RET
    
####; Character output subroutine.  Prints the character in AL to the
####; VGA display and serial port 0, using BIOS services (see
####; [IntrList]).  Preserves all general-purpose registers.
####;
####; If called upon to output a carriage return, this subroutine
####; automatically supplies the following line feed.

putc:
    PUSHA

1:  XOR     BH, BH          #; Page 0.
    MOV     AH, 0x0E        #; Teletype output service.
    INT     0x10
    MOV     AH, 0x01        #; Serial port output service.
    XOR     DX, DX          #; Serial port 0.
    
2:  INT     0x14            #; Destroys AH.
    TEST    AH, 0x80        #; Output timed out?
    JZ      3f
    MOV     WORD PTR 2b, 0x9090 #; Turn "int $0x14" above into NOPs.

3:  CMP     AL, '\r'
    JNE     popa_ret
    MOV     AL, '\n'
    JMP     1b

popa_ret:
    POPA
    RET             #; Error code still in CF

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
    .word 0xAA55

