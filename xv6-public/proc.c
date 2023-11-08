#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "mmap.h"

struct
{
    struct spinlock lock;
    struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// Get physical Address of page from virtual address of process
uint get_physical_page(struct proc *p, uint tempaddr, pte_t **pte)
{
    *pte = walkpgdir(p->pgdir, (char *)tempaddr, 0);

    if (!*pte)
    {
        return 0;
    }
    uint pa = PTE_ADDR(**pte);
    return pa;
}

void allocatePageNonGuard(struct proc *proc, struct mmap_desc *md, long addr_head)
{
    char *mem = kalloc();
    if (mem == NULL)
        panic("kalloc");
    memset(mem, 0, PGSIZE);
    addr_head = PGROUNDDOWN(addr_head);
    if (mappages(proc->pgdir, (void *)addr_head, PGSIZE, V2P(mem), md->prot | PTE_U) != 0)
    {
        kfree(mem);
        myproc()->killed = 1;
        exit();
    }
    if (md->flags & MAP_ANON)
    {
    }
    else
    {
        char *buff = kalloc();
        memset(buff, 0, PGSIZE);
        mmap_read(md->f, buff, PGSIZE);
        memmove(mem, buff, PGSIZE);
        md->page = buff;
    }
}

void allocatePageGuard(struct proc *proc, struct mmap_desc *md, long addr_head, int i)
{
    // see if we can grow up by one page

    if (proc->mmaps[i + 1].valid)
    {
        // sadly we cannot
        cprintf("Segmentation Fault\n");
        proc->killed = 1;
    }
    else
    {
        md->guard_page = false;
        // alloc this
        char *mem = kalloc();
        if (mem == NULL)
            panic("kalloc");
        memset(mem, 0, PGSIZE);
        addr_head = PGROUNDDOWN(addr_head);
        if (mappages(proc->pgdir, (void *)addr_head, PGSIZE, V2P(mem), md->prot | PTE_U) != 0)
        {
            panic("mappapges");
            kfree(mem);
            proc->killed = 1;
        }
        // set the mmaps[i+1] to be the new guard_page
        proc->mmaps[i + 1].valid = true;
        proc->mmaps[i + 1].guard_page = true;
        if (md->flags & MAP_ANON)
        {
        }
        else
        {
            char *buff = kalloc();
            memset(buff, 0, PGSIZE);
            mmap_read(md->f, buff, PGSIZE);
            memmove(mem, buff, PGSIZE);
            md->page = buff;
        }
    }
}

void pinit(void)
{
    initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
    return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
    int apicid, i;

    if (readeflags() & FL_IF)
        panic("mycpu called with interrupts enabled\n");

    apicid = lapicid();
    // APIC IDs are not guaranteed to be contiguous. Maybe we should have
    // a reverse map, or reserve a register to store &cpus[i].
    for (i = 0; i < ncpu; ++i)
    {
        if (cpus[i].apicid == apicid)
            return &cpus[i];
    }
    panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
    struct cpu *c;
    struct proc *p;
    pushcli();
    c = mycpu();
    p = c->proc;
    popcli();
    return p;
}

// PAGEBREAK: 32
//  Look in the process table for an UNUSED proc.
//  If found, change state to EMBRYO and initialize
//  state required to run in the kernel.
//  Otherwise return 0.
static struct proc *
allocproc(void)
{
    struct proc *p;
    char *sp;

    acquire(&ptable.lock);

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if (p->state == UNUSED)
            goto found;

    release(&ptable.lock);
    return 0;

found:
    p->state = EMBRYO;
    p->pid = nextpid++;
    p->open_frames = 0;
    for (int i = 0; i < PAGE_LIMIT; i++)
    {
        p->mmaps[i].valid = 0; // Mark the entry as invalid
                               // Set other initial values for the mmap_desc entries as needed
                               p->mmaps[i].isChildOfParent = false;
    }

    release(&ptable.lock);

    // Allocate kernel stack.
    if ((p->kstack = kalloc()) == 0)
    {
        p->state = UNUSED;
        return 0;
    }
    sp = p->kstack + KSTACKSIZE;

    // Leave room for trap frame.
    sp -= sizeof *p->tf;
    p->tf = (struct trapframe *)sp;

    // Set up new context to start executing at forkret,
    // which returns to trapret.
    sp -= 4;
    *(uint *)sp = (uint)trapret;

    sp -= sizeof *p->context;
    p->context = (struct context *)sp;
    memset(p->context, 0, sizeof *p->context);
    p->context->eip = (uint)forkret;

    return p;
}

// PAGEBREAK: 32
//  Set up first user process.
void userinit(void)
{
    struct proc *p;
    extern char _binary_initcode_start[], _binary_initcode_size[];

    p = allocproc();

    initproc = p;
    if ((p->pgdir = setupkvm()) == 0)
        panic("userinit: out of memory?");
    inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
    p->sz = PGSIZE;
    memset(p->tf, 0, sizeof(*p->tf));
    p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
    p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
    p->tf->es = p->tf->ds;
    p->tf->ss = p->tf->ds;
    p->tf->eflags = FL_IF;
    p->tf->esp = PGSIZE;
    p->tf->eip = 0; // beginning of initcode.S

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    // this assignment to p->state lets other cores
    // run this process. the acquire forces the above
    // writes to be visible, and the lock is also needed
    // because the assignment might not be atomic.
    acquire(&ptable.lock);

    p->state = RUNNABLE;

    release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
    uint sz;
    struct proc *curproc = myproc();

    sz = curproc->sz;
    if (n > 0)
    {
        if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
            return -1;
    }
    else if (n < 0)
    {
        if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
            return -1;
    }
    curproc->sz = sz;
    switchuvm(curproc);
    return 0;
}

void deep_copy(struct mmap_desc *old, struct mmap_desc *new)
{
    // copies old to new as deep copy
    new->length = old->length;
    new->virtualAddress = old->virtualAddress;
    new->flags = old->flags;
    new->prot = old->prot;
    new->dirty = old->dirty;
    new->shared = old->shared;
    new->valid = old->valid;
    new->guard_page = old->guard_page;
    new->f = filedup(old->f);
    new->already_alloced = old->already_alloced;
}


void munmap_free(struct mmap_desc *md)
{
    if(!md->valid) return;
    md->ref--;
    if (md->ref != 0)
        return;

    md->length = 0;
    md->virtualAddress = 0;
    md->flags = 0;
    md->prot = 0;
    md->dirty = 0;
    md->shared = 0;
    md->valid = 0;
    md->guard_page = 0;
    md->f = NULL;
    md->already_alloced = 0;
    if(md->page == 0) return;
    kfree(md->page);
    md->page = 0;
}

// Copy mmaps from parent to child process
int copy_maps(struct proc *parent, struct proc *child)
{
    pte_t *pte;
    for(int i = 0; i < PAGE_LIMIT; i++)
    {
        if(!parent->mmaps[i].valid) continue;
        struct mmap_desc* md_c = &child->mmaps[i];
        struct mmap_desc *md_p = &parent->mmaps[i];
        uint virt_addr = md_p->virtualAddress;
        int protection = md_p->prot;
        int isshared = md_p->flags & MAP_SHARED;
        uint parentPage = get_physical_page(parent, virt_addr, &pte);
        if (isshared)
        {

            if(parentPage == 0) {
                //since we are doing lazy alloc, it hasn't been faulted yet, but we have to 
                //share it, so we are going to allocate it, however, if its mapsgrowup, then 
                //allocating it will lead to infinit loop, thus we have to just instantiate it
                if(md_p->flags & MAP_GROWSUP) {
                    //do basic instantiation in child md and give a pointer to this proc as well
                    continue;
                } else {
                    //cprintf("non-guard page\n");
                    allocatePageNonGuard(parent, md_p, virt_addr);
                    parentPage = get_physical_page(parent, virt_addr, &pte);
                }
            }
            md_c->isChildOfParent = false;
            
            // If the page is shared and then all the data should be stored in page
            // and mapped to each process
            char *parentmem = (char *)P2V(parentPage);
            if (mappages(child->pgdir, (char *)virt_addr, PGSIZE, V2P(parentmem), protection | PTE_U) != 0)
            {
                // ERROR: Shared mappages failed
                panic("mappages");
            }
            md_c->ref = 2;
            md_p->ref = 2;
            md_c->page = md_p->page;
        }
        else
        {
            if(parentPage == 0) {
                //mapping is private, can do lazy alloc
                deep_copy(md_p, md_c);
                continue;
            }
            md_c->isChildOfParent = true;
            char *mem = kalloc();
            if (mem == NULL)
                exit();
            char *pmem = (char *)P2V(parentPage);
            if (mappages(child->pgdir, (char *)virt_addr, PGSIZE, V2P(mem),
                         protection | PTE_U) != 0)
            {
                // ERROR: Private mappages failed
                panic("ahahah");
            }
            memmove(mem, pmem, PGSIZE);
            md_c->ref = 1;
            md_c->page = mem;
        }

        deep_copy(md_p, md_c);
    }
    return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
    int i, pid;
    struct proc *np;
    struct proc *curproc = myproc();
    // Allocate process.
    if ((np = allocproc()) == 0)
    {
        return -1;
    }
    
    // Copy process state from proc.
    if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
    {
        kfree(np->kstack);
        np->kstack = 0;
        np->state = UNUSED;
        return -1;
    }
    
    np->sz = curproc->sz;
    np->parent = curproc;
    *np->tf = *curproc->tf;

    // Clear %eax so that fork returns 0 in the child.
    np->tf->eax = 0;

    for (i = 0; i < NOFILE; i++)
        if (curproc->ofile[i])
            np->ofile[i] = filedup(curproc->ofile[i]);
    np->cwd = idup(curproc->cwd);

    safestrcpy(np->name, curproc->name, sizeof(curproc->name));

    pid = np->pid;

    acquire(&ptable.lock);

    np->state = RUNNABLE;

    release(&ptable.lock);
    copy_maps(curproc, np);
    return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
    struct proc *curproc = myproc();
    struct proc *p;
    int fd;

    if (curproc == initproc)
        panic("init exiting");

    // Close all open files.
    for (fd = 0; fd < NOFILE; fd++)
    {
        if (curproc->ofile[fd])
        {
            fileclose(curproc->ofile[fd]);
            curproc->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(curproc->cwd);
    end_op();
    curproc->cwd = 0;

    acquire(&ptable.lock);

    // Parent might be sleeping in wait().
    wakeup1(curproc->parent);

    // Pass abandoned children to init.
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
        if (p->parent == curproc)
        {
            p->parent = initproc;
            if (p->state == ZOMBIE)
                wakeup1(initproc);
        }
    }
    for(int i = 0; i < PAGE_LIMIT; i++) {
        if(!curproc->mmaps[i].valid) { continue;}
        struct mmap_desc* md = &curproc->mmaps[i];
        if (md->f != NULL) {
        if (md->flags & MAP_PRIVATE)
        {
        }
        else
        {
            filewrite(md->f, (char *)md->virtualAddress, PGSIZE);
        }

        fileclose(md->f);
        }
        munmap_free(md);
    }
    // Jump into the scheduler, never to return.
    curproc->state = ZOMBIE;
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
    struct proc *p;
    int havekids, pid;
    struct proc *curproc = myproc();

    acquire(&ptable.lock);
    for (;;)
    {
        // Scan through table looking for exited children.
        havekids = 0;
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        {
            if (p->parent != curproc)
                continue;
            havekids = 1;
            if (p->state == ZOMBIE)
            {
                // Found one.
                pid = p->pid;
                kfree(p->kstack);
                p->kstack = 0;
                freevm(p->pgdir);
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                p->state = UNUSED;
                release(&ptable.lock);
                return pid;
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || curproc->killed)
        {
            release(&ptable.lock);
            return -1;
        }

        // Wait for children to exit.  (See wakeup1 call in proc_exit.)
        sleep(curproc, &ptable.lock); // DOC: wait-sleep
    }
}

// PAGEBREAK: 42
//  Per-CPU process scheduler.
//  Each CPU calls scheduler() after setting itself up.
//  Scheduler never returns.  It loops, doing:
//   - choose a process to run
//   - swtch to start running that process
//   - eventually that process transfers control
//       via swtch back to the scheduler.
void scheduler(void)
{
    struct proc *p;
    struct cpu *c = mycpu();
    c->proc = 0;

    for (;;)
    {
        // Enable interrupts on this processor.
        sti();

        // Loop over process table looking for process to run.
        acquire(&ptable.lock);
        for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        {
            if (p->state != RUNNABLE)
                continue;

            // Switch to chosen process.  It is the process's job
            // to release ptable.lock and then reacquire it
            // before jumping back to us.
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;

            swtch(&(c->scheduler), p->context);
            switchkvm();

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
        }
        release(&ptable.lock);
    }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
    int intena;
    struct proc *p = myproc();

    if (!holding(&ptable.lock))
        panic("sched ptable.lock");
    if (mycpu()->ncli != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (readeflags() & FL_IF)
        panic("sched interruptible");
    intena = mycpu()->intena;
    swtch(&p->context, mycpu()->scheduler);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
    acquire(&ptable.lock); // DOC: yieldlock
    myproc()->state = RUNNABLE;
    sched();
    release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
    static int first = 1;
    // Still holding ptable.lock from scheduler.
    release(&ptable.lock);

    if (first)
    {
        // Some initialization functions must be run in the context
        // of a regular process (e.g., they call sleep), and thus cannot
        // be run from main().
        first = 0;
        iinit(ROOTDEV);
        initlog(ROOTDEV);
    }

    // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    if (p == 0)
        panic("sleep");

    if (lk == 0)
        panic("sleep without lk");

    // Must acquire ptable.lock in order to
    // change p->state and then call sched.
    // Once we hold ptable.lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup runs with ptable.lock locked),
    // so it's okay to release lk.
    if (lk != &ptable.lock)
    {                          // DOC: sleeplock0
        acquire(&ptable.lock); // DOC: sleeplock1
        release(lk);
    }
    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    if (lk != &ptable.lock)
    { // DOC: sleeplock2
        release(&ptable.lock);
        acquire(lk);
    }
}

// PAGEBREAK!
//  Wake up all processes sleeping on chan.
//  The ptable lock must be held.
static void
wakeup1(void *chan)
{
    struct proc *p;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if (p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
    acquire(&ptable.lock);
    wakeup1(chan);
    release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
    struct proc *p;

    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
        if (p->pid == pid)
        {
            p->killed = 1;
            // Wake process from sleep if necessary.
            if (p->state == SLEEPING)
                p->state = RUNNABLE;
            release(&ptable.lock);
            return 0;
        }
    }
    release(&ptable.lock);
    return -1;
}

// PAGEBREAK: 36
//  Print a process listing to console.  For debugging.
//  Runs when user types ^P on console.
//  No lock to avoid wedging a stuck machine further.
void procdump(void)
{
    static char *states[] = {
        [UNUSED] "unused",
        [EMBRYO] "embryo",
        [SLEEPING] "sleep ",
        [RUNNABLE] "runble",
        [RUNNING] "run   ",
        [ZOMBIE] "zombie"};
    int i;
    struct proc *p;
    char *state;
    uint pc[10];

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        cprintf("%d %s %s", p->pid, state, p->name);
        if (p->state == SLEEPING)
        {
            getcallerpcs((uint *)p->context->ebp + 2, pc);
            for (i = 0; i < 10 && pc[i] != 0; i++)
                cprintf(" %p", pc[i]);
        }
        cprintf("\n");
    }
}
