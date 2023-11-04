#ifndef MMAP_H
#define MMAP_H
#define NULL 0
#define true 1
#define false 0
/* Define mmap flags */
#define MAP_PRIVATE 0x0001
#define MAP_SHARED 0x0002
#define MAP_ANONYMOUS 0x0004
#define MAP_FIXED 0x0008
#define MAP_GROWSUP 0x0010
#define MAP_ANON MAP_ANONYMOUS


/* Protections on memory mapping */
#define PROT_READ 0x1
#define PROT_WRITE 0x2

/* Valid virtual address spaces*/
#define VIRT_ADDR_START 0x60000000
#define VIRT_ADDR_END 0x80000000

/* Page size constant */
#define PAGE_SIZE 4096
#define PAGE_LIMIT 32

extern int VA_PTR;

struct mmap_desc
{
    int length;         // the length in bits (bytes?)
    int virtualAddress; // the start of the virtual address
    int flags;          // flags associated with this mmap
    int prot;           // protection bits
    int dirty;          // if this has been written to
    int shared;         // if its shared or naw
    int valid;          // if this is a valid mmap_desc
    int guard_page;     // 1 if it is a guard page, 0 otherwise
    int hasFile;
    struct file *f;     //the file its associated with
    int already_alloced;//if the associated descriptor is already alloced
};
#endif // MMAP_H