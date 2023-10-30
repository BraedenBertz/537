#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "mmap.h"

// mmap returns the starting virtual address of the allocated memory on success,
// and (void *)-1 on failure.
// That virtual address must be a multiple of page size.
// void* mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
/**
 * addr: A hint for what "virtual" address the mmap should place the mapping at, or
 * the "virtual" address that mmap MUST place the mapping.
 * length: The length of the mapping in bytes
 * prot: What operations are allowed in this region (read/write).
 *       For this project, we assume PROT_READ | PROT_WRITE.
 * fd: If it's a file-backed mapping, this is the file descriptor for the
 *       file to be mapped.
 * offset: Assume that this argument will always be 0.
 * flags: The kind of memory mapping you are requesting for.
 * */
int sys_mmap(void)
{
    char *addr;
    int length;
    int prot;
    int flags;
    int fd;
    int offset = 0;
    // handle the inputs
    if (argptr(0, &addr, sizeof(int)) < 0 || 
        argint(1, &length) < 0 || 
        argint(2, &prot) < 0 || 
        argint(3, &flags) <0 ||
        argint(4, &fd) < 0 || 
        argint(5, &offset) < 0) {
        return -1;
    }
    if(length <= 0) {
        return -1;
    }

    if (flags & MAP_ANONYMOUS)
    {
        // It's NOT a file-backed mapping. You can ignore the last two arguments
        //(fd and offset) if this flag is provided.
    }
    if (flags & MAP_SHARED)
    {
        // This flag tells mmap that the mapping is shared.
        // Memory mappings are copied from the parent to the child across the fork system call.
        // If the mapping is MAP_SHARED, then changes made by the child will be visible
        // to the parent, and vice versa. However, if the mapping is MAP_PRIVATE, each
        // process will have its own copy of the mapping.
    }
    else if (flags & MAP_PRIVATE)
    {
        // Mapping is not shared. You still need to copy the mappings from parent to child,
        // but these mappings should use different "physical" pages. In other words,
        // the same virtual addresses are mapped in child, but to a different set of
        // physical pages. That will cause modifications to memory be invisible to other
        // processes. Moreover, if it's a file-backed mapping, modification to memory are
        // NOT carried through to the underlying file.
    }
    else
    {
        //must have private or shared
        return -1;
    }
    
    if (flags & MAP_FIXED)
    {
        // our virtual address must be the addr argument
        if (((int)addr) >= VIRT_ADDR_END || ((int)addr) < VIRT_ADDR_START)
        {
            return -1;
        }
    }
    if (flags & MAP_GROWSUP)
    {
        // do the guard page stuff
    }

    // in case of a good mmap return the address of the VAS we start at
    return 0;
}

// munmap returns 0 to indicate success, and -1 for failure.
// int munmap(void *addr, size_t length)
int sys_munmap(void)
{
    char *addr;
    int length;
    if(argptr(0, &addr, sizeof(int)) < 0 || argint(1, &length) < 0) {
        return -1;
    }
    //if addr is not a multiple of PAGE_SIZE return -1;
    if(((int)addr) % PAGE_SIZE != 0)
        return -1;
    return 0;
}

int sys_fork(void)
{
    return fork();
}

int sys_exit(void)
{
    exit();
    return 0; // not reached
}

int sys_wait(void)
{
    return wait();
}

int sys_kill(void)
{
    int pid;

    if (argint(0, &pid) < 0)
        return -1;
    return kill(pid);
}

int sys_getpid(void)
{
    return myproc()->pid;
}

int sys_sbrk(void)
{
    int addr;
    int n;

    if (argint(0, &n) < 0)
        return -1;
    addr = myproc()->sz;
    if (growproc(n) < 0)
        return -1;
    return addr;
}

int sys_sleep(void)
{
    int n;
    uint ticks0;

    if (argint(0, &n) < 0)
        return -1;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n)
    {
        if (myproc()->killed)
        {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}
