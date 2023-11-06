#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"


// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[]; // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;


void tvinit(void)
{
    int i;

    for (i = 0; i < 256; i++)
        SETGATE(idt[i], 0, SEG_KCODE << 3, vectors[i], 0);
    SETGATE(idt[T_SYSCALL], 1, SEG_KCODE << 3, vectors[T_SYSCALL], DPL_USER);

    initlock(&tickslock, "time");
}

void idtinit(void)
{
    lidt(idt, sizeof(idt));
}

// PAGEBREAK: 41
void trap(struct trapframe *tf)
{
    if (tf->trapno == T_SYSCALL)
    {
        if (myproc()->killed)
            exit();
        myproc()->tf = tf;
        syscall();
        if (myproc()->killed)
            exit();
        return;
    }

    switch (tf->trapno)
    {
    case T_IRQ0 + IRQ_TIMER:
        if (cpuid() == 0)
        {
            acquire(&tickslock);
            ticks++;
            wakeup(&ticks);
            release(&tickslock);
        }
        lapiceoi();
        break;
    case T_IRQ0 + IRQ_IDE:
        ideintr();
        lapiceoi();
        break;
    case T_IRQ0 + IRQ_IDE + 1:
        // Bochs generates spurious IDE1 interrupts.
        break;
    case T_IRQ0 + IRQ_KBD:
        kbdintr();
        lapiceoi();
        break;
    case T_IRQ0 + IRQ_COM1:
        uartintr();
        lapiceoi();
        break;
    case T_PGFLT:
        // see if this is a page fault becuase of a grows up mmap
        int i;
        struct mmap_desc* md;
        long fault_addr = rcr2();
        for (i = 0; i < PAGE_LIMIT; i++)
        {
            md = &myproc()->mmaps[i];
            if (!md->valid)
                continue;
            else if (md->virtualAddress <= fault_addr 
            && md->virtualAddress + PAGE_SIZE > fault_addr)
            {
                //this is our mmap_descriptor for this address
                break;
            }
        }
        if(i == PAGE_LIMIT) {
            //wasn't in our mapping
            cprintf("Segmentation Fault\n");
            myproc()->killed = true;
        }
        // its a guard page
        else if (md->guard_page)
        {
            // cprintf("We hit a guard page at index %d\n", i);
            // //see if we can grow up by one page

            // if(myproc()->mmaps[i+1].valid)
            // {
            //     //sadly we cannot
            //     cprintf("Segmentation Fault with guard page\n");
            //     myproc()->killed = 1;
            // } else {
            //     md->guard_page = false;
            //     //alloc this
            //     char *mem = kalloc();
            //     if (mem == NULL)
            //         panic("kalloc");
            //     memset(mem, 0, PGSIZE);
            //     long fault_addr_head = PGROUNDDOWN(fault_addr);
            //     if (mappages(myproc()->pgdir, (void *)fault_addr_head, PGSIZE, V2P(mem), md->prot | PTE_U) != 0)
            //     {
            //         panic("mappapges");
            //         kfree(mem);
            //         myproc()->killed = 1;
            //     }
            //     //set the mmaps[i+1] to be the new guard_page
            //     myproc()->mmaps[i + 1].valid = true;
            //     myproc()->mmaps[i + 1].guard_page = true;
            //     if (md->flags & MAP_ANON){}
            //     else
            //     {
            //         char *buff = kalloc();
            //         memset(buff, 0, PGSIZE);
            //         mmap_read(md->f, buff, PGSIZE);
            //         memmove(mem, buff, PGSIZE);
            //     }
            // }
            allocatePageGuard(myproc(), md, fault_addr, i);
        }
        else
        {
            // //not a guard page, but is valid, go ahead and alloc this page
            // char *mem = kalloc();
            // if (mem == NULL)
            //     panic("kalloc");
            // memset(mem, 0, PGSIZE);
            // long fault_addr_head = PGROUNDDOWN(fault_addr);
            // if (mappages(myproc()->pgdir, (void*)fault_addr_head, PGSIZE, V2P(mem), md->prot | PTE_U) != 0)
            // {
            //     kfree(mem);
            //     myproc()->killed = 1;
            //     exit();
            // }
            // if(md->flags & MAP_ANON) {}
            // else {
            //     char* buff = kalloc();
            //     memset(buff, 0, PGSIZE);
            //     mmap_read(md->f, buff, PGSIZE);
            //     memmove(mem, buff, PGSIZE);
            // }
            allocatePageNonGuard(myproc(), md, fault_addr);
        }
        break;
    case T_IRQ0 + 7:
    case T_IRQ0 + IRQ_SPURIOUS:
        cprintf("cpu%d: spurious interrupt at %x:%x\n",
                cpuid(), tf->cs, tf->eip);
        lapiceoi();
        break;

    // PAGEBREAK: 13
    default:
        if (myproc() == 0 || (tf->cs & 3) == 0)
        {
            // In kernel, it must be our mistake.
            cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
                    tf->trapno, cpuid(), tf->eip, rcr2());
            panic("trap");
        }
        // In user space, assume process misbehaved.
        cprintf("pid %d %s: trap %d err %d on cpu %d "
                "eip 0x%x addr 0x%x--kill proc\n",
                myproc()->pid, myproc()->name, tf->trapno,
                tf->err, cpuid(), tf->eip, rcr2());
        myproc()->killed = 1;
    }

    // Force process exit if it has been killed and is in user space.
    // (If it is still executing in the kernel, let it keep running
    // until it gets to the regular system call return.)
    if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
        exit();

    // Force process to give up CPU on clock tick.
    // If interrupts were on while locks held, would need to check nlock.
    if (myproc() && myproc()->state == RUNNING &&
        tf->trapno == T_IRQ0 + IRQ_TIMER)
        yield();

    // Check if the process has been killed since we yielded
    if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
        exit();
}
