#ifndef MMAP_H
#define MMAP_H
/* Define mmap flags */
#define MAP_PRIVATE 0x0001
#define MAP_SHARED 0x0002
#define MAP_ANONYMOUS 0x0004
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FIXED 0x0008
#define MAP_GROWSUP 0x0010

/* Protections on memory mapping */
#define PROT_READ 0x1
#define PROT_WRITE 0x2

/* Valid virtual address spaces*/
#define VIRT_ADDR_START 0x60000000
#define VIRT_ADDR_END 0x80000000

/* Page size constant */
#define PAGE_SIZE 4096
#define PAGE_LIMIT 32

struct mmap_desc
{
    int numberOfPages;  // how many pages are associated with the mmap
    int virtualAddress; // the start of the virtual address
    int flags;          // flags associated with this mmap
    int prot;           // protection bits
    int dirty;          // if this has been written to
    int shared;         // if its shared or naw
    int valid;          // if this is a valid mmap_desc
};
#endif // MMAP_H