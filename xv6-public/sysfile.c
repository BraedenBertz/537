//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and
#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "mmap.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"


const int RETURN_ERR = (int)((void *)-1);
int VA_PTR = VIRT_ADDR_START;


// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
    int fd;
    struct file *f;

    if (argint(n, &fd) < 0)
        return -1;
    if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
        return -1;
    if (pfd)
        *pfd = fd;
    if (pf)
        *pf = f;
    return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
    int fd;
    struct proc *curproc = myproc();

    for (fd = 0; fd < NOFILE; fd++)
    {
        if (curproc->ofile[fd] == 0)
        {
            curproc->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}
// make a function that makes a deep copy of mmap_desc

void munmap_free(struct mmap_desc* md, int addr, int length){
    //cprintf("In sysfile.c gonna run logic for munmap and free the mmap_desc struct\n");
    int length_check = 0;
    for(int i = 0; i < PAGE_LIMIT; i++)
    {
        if(md->virtualAddress == addr)
        {
            if(length_check >= length)
                break;
            length_check++;
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
        }   
    }
}

void munmap()
{
    // one function that allow me to pput in a mmap_desc descriptor
    // this function should clear all of the data in there, releasing file and freeing memory/setting to 0

    // may need another function to see if the page is dirty: ie it has been written
    // to and then will need to write to that file. Possibly won't need thisk, but keep in mind if needed. 
    // implement the freeing logic of munmap here
    //cprintf("In vm.c in the munmap method\n");
}

// munmap returns 0 to indicate success, and -1 for failure.
// int munmap(void *addr, size_t length)
int sys_munmap(void)
{
    //cprintf("In the sys_munmap function in sysfile.c\n");
    int addr;
    int length;
    if (argint(0, &addr) < 0 || argint(1, &length) < 0)
    {
        cprintf("Error getting the arguments\n");
        return RETURN_ERR;
    }
    // if addr is not a multiple of PAGE_SIZE return -1;
    if (((uint)addr) % PGSIZE != 0){
        cprintf("error, addr mod PAGE_SIZE is not 0: addr: %d, PAGE_SIZE: %d\n", (uint)addr, PGSIZE);
        return RETURN_ERR;
    }

    //determine if we write
    int j = 0;
    struct mmap_desc* to_free[PAGE_LIMIT];
    for(int _addr = addr; _addr < length+addr; _addr+=PGSIZE) {
        for(int i = 0; i < PAGE_LIMIT; i++) {
            struct mmap_desc* md = &myproc()->mmaps[i];
            if(!md->valid) continue;
            if(md->virtualAddress <= _addr && md->virtualAddress+PGSIZE > _addr) {
                //addr is within this memory region, so we are going to unmap this mmap_desc
                to_free[j++] = md;
            }
        }
    }


    // char buffer[PGSIZE];
    // for(int i = 0; i < PGSIZE; i++) {
    //     buffer[i] = '+';
    // }
    // cprintf("testing filewrite\n");
    //for all of the mumap regions, we write to file if its file-backed
    for(int i =0; i < j; i++) {
        struct mmap_desc* md = to_free[i];
        if(md->f == NULL) continue;
        mmap_write(md->f, (char *)md->virtualAddress, 0, PGSIZE);
        // filewrite(md->f, buffer, PGSIZE);
        //  char read_buffer[PGSIZE];
        //  fileread(md->f, read_buffer, PGSIZE);
        //  cprintf("read\n");
        //  cprintf("%s\n", read_buffer);
        fileclose(md->f);
    }
    
    munmap_free(myproc()->mmaps, addr, length);

    return 0;
}

// void* mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
//Validates the user's inputs and calls the implementation of mmap
int sys_mmap(void)
{
    //cprintf("In sys_map\n");
    int va_start = VA_PTR;
    int addr, length, prot, fd, flags, offset = 0;
    struct file *f;
    // handle the inputs
    if (argint(0, &addr) < 0  ||
        argint(1, &length) < 0  ||
        argint(2, &prot) < 0 ||
        argint(3, &flags) < 0 ||
        argint(4, &fd) < 0 ||
        argint(5, &offset) < 0)
    {
        return RETURN_ERR;
    }
    
    if (length < 0 || offset < 0)  return RETURN_ERR;

    int j = 0;
    int numberOfPagesToAllocate = 0;
    for(j = 0; j < length; j+=PGSIZE){
        numberOfPagesToAllocate++;
    }
    if(flags & MAP_GROWSUP) 
        numberOfPagesToAllocate++;

    if (!(flags & MAP_PRIVATE) && !(flags & MAP_SHARED)) return RETURN_ERR; 

    if (flags & MAP_FIXED) {
        //see if the addr is even valid within the mmap bounds
        if (((int)addr + length) >= VIRT_ADDR_END || ((int)addr) < VIRT_ADDR_START)
            return RETURN_ERR;

        for(j = 0; j < numberOfPagesToAllocate; j++){

            int currAddr = addr+PAGE_SIZE*j;

            //see if the address is already being mapped
            for(int i = 0; i < PAGE_LIMIT; i++) {
                struct mmap_desc md = myproc()->mmaps[i];
                if(!md.valid) continue;
                if(md.virtualAddress <= currAddr && 
                    currAddr < md.virtualAddress+PAGE_SIZE) {
                    //this is already mapped region,
                    cprintf("Return statement from fixed region intercepts other mapping: %d\n", RETURN_ERR); 
                    return RETURN_ERR;
                }
            }
        }
        va_start = addr;
    }

    cprintf("Number of pages to allocate: %d\n", numberOfPagesToAllocate);

    // if flag is MAP_ANONYMOUS can ignore offset and fd
    int run = 0;
    int i = -1;
    
    for(j = 0; j< PAGE_LIMIT; j++){
        struct mmap_desc md = myproc()->mmaps[j];
        if(md.valid) {
            run = 0;
            continue;
        }
        run++;
        if(run == numberOfPagesToAllocate){
             i = j - numberOfPagesToAllocate + 1;
             break;
        }
    }
    if(i == -1) {
        return RETURN_ERR;
    }

    

    for(j = i; j < i + run; j++) {
        struct mmap_desc *md = &myproc()->mmaps[j];
        if(md->valid) continue;
        md->valid = true;
        md->dirty = false;
        md->flags = flags;
        md->prot = prot;
        if(j == i+run-1 && flags & MAP_GROWSUP) md->guard_page = true;
        else md->guard_page = false;
        if (!(flags & MAP_ANON))
        {
            if (argfd(4, &fd, &f) < 0)
                return RETURN_ERR;
            md->f = filedup(f);
        }
        else
        {
            md->f = NULL;
        }
        md->length = length;
        
        cprintf("The virt addr being assigned is: %d, %s\n", va_start, md->guard_page ? "true" : "false");
        md->virtualAddress = va_start;
        va_start+=PGSIZE;
    }
    // if the mmap request is fixed, then we don't change the va_ptr
    if(!(flags & MAP_FIXED)){
        VA_PTR = va_start;
    }
    //cprintf("Return value: %d\n", myproc()->mmaps[i].virtualAddress);
    
    
    return myproc()->mmaps[i].virtualAddress;
    
}

int sys_dup(void)
{
    struct file *f;
    int fd;

    if (argfd(0, 0, &f) < 0)
        return -1;
    if ((fd = fdalloc(f)) < 0)
        return -1;
    filedup(f);
    return fd;
}

int sys_read(void)
{
    struct file *f;
    int n;
    char *p;

    if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
        return -1;
    return fileread(f, p, n);
}

int sys_write(void)
{
    struct file *f;
    int n;
    char *p;

    if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
        return -1;
    return filewrite(f, p, n);
}

int sys_close(void)
{
    int fd;
    struct file *f;

    if (argfd(0, &fd, &f) < 0)
        return -1;
    myproc()->ofile[fd] = 0;
    fileclose(f);
    return 0;
}

int sys_fstat(void)
{
    struct file *f;
    struct stat *st;

    if (argfd(0, 0, &f) < 0 || argptr(1, (void *)&st, sizeof(*st)) < 0)
        return -1;
    return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int sys_link(void)
{
    char name[DIRSIZ], *new, *old;
    struct inode *dp, *ip;

    if (argstr(0, &old) < 0 || argstr(1, &new) < 0)
        return -1;

    begin_op();
    if ((ip = namei(old)) == 0)
    {
        end_op();
        return -1;
    }

    ilock(ip);
    if (ip->type == T_DIR)
    {
        iunlockput(ip);
        end_op();
        return -1;
    }

    ip->nlink++;
    iupdate(ip);
    iunlock(ip);

    if ((dp = nameiparent(new, name)) == 0)
        goto bad;
    ilock(dp);
    if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0)
    {
        iunlockput(dp);
        goto bad;
    }
    iunlockput(dp);
    iput(ip);

    end_op();

    return 0;

bad:
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    end_op();
    return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
    int off;
    struct dirent de;

    for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de))
    {
        if (readi(dp, (char *)&de, off, sizeof(de)) != sizeof(de))
            panic("isdirempty: readi");
        if (de.inum != 0)
            return 0;
    }
    return 1;
}

// PAGEBREAK!
int sys_unlink(void)
{
    struct inode *ip, *dp;
    struct dirent de;
    char name[DIRSIZ], *path;
    uint off;

    if (argstr(0, &path) < 0)
        return -1;

    begin_op();
    if ((dp = nameiparent(path, name)) == 0)
    {
        end_op();
        return -1;
    }

    ilock(dp);

    // Cannot unlink "." or "..".
    if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
        goto bad;

    if ((ip = dirlookup(dp, name, &off)) == 0)
        goto bad;
    ilock(ip);

    if (ip->nlink < 1)
        panic("unlink: nlink < 1");
    if (ip->type == T_DIR && !isdirempty(ip))
    {
        iunlockput(ip);
        goto bad;
    }

    memset(&de, 0, sizeof(de));
    if (writei(dp, (char *)&de, off, sizeof(de)) != sizeof(de))
        panic("unlink: writei");
    if (ip->type == T_DIR)
    {
        dp->nlink--;
        iupdate(dp);
    }
    iunlockput(dp);

    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);

    end_op();

    return 0;

bad:
    iunlockput(dp);
    end_op();
    return -1;
}

static struct inode *
create(char *path, short type, short major, short minor)
{
    struct inode *ip, *dp;
    char name[DIRSIZ];

    if ((dp = nameiparent(path, name)) == 0)
        return 0;
    ilock(dp);

    if ((ip = dirlookup(dp, name, 0)) != 0)
    {
        iunlockput(dp);
        ilock(ip);
        if (type == T_FILE && ip->type == T_FILE)
            return ip;
        iunlockput(ip);
        return 0;
    }

    if ((ip = ialloc(dp->dev, type)) == 0)
        panic("create: ialloc");

    ilock(ip);
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;
    iupdate(ip);

    if (type == T_DIR)
    {                // Create . and .. entries.
        dp->nlink++; // for ".."
        iupdate(dp);
        // No ip->nlink++ for ".": avoid cyclic ref count.
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
            panic("create dots");
    }

    if (dirlink(dp, name, ip->inum) < 0)
        panic("create: dirlink");

    iunlockput(dp);

    return ip;
}

int sys_open(void)
{
    char *path;
    int fd, omode;
    struct file *f;
    struct inode *ip;

    if (argstr(0, &path) < 0 || argint(1, &omode) < 0)
        return -1;

    begin_op();

    if (omode & O_CREATE)
    {
        ip = create(path, T_FILE, 0, 0);
        if (ip == 0)
        {
            end_op();
            return -1;
        }
    }
    else
    {
        if ((ip = namei(path)) == 0)
        {
            end_op();
            return -1;
        }
        ilock(ip);
        if (ip->type == T_DIR && omode != O_RDONLY)
        {
            iunlockput(ip);
            end_op();
            return -1;
        }
    }

    if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0)
    {
        if (f)
            fileclose(f);
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    end_op();

    f->type = FD_INODE;
    f->ip = ip;
    f->off = 0;
    f->readable = !(omode & O_WRONLY);
    f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
    return fd;
}

int sys_mkdir(void)
{
    char *path;
    struct inode *ip;

    begin_op();
    if (argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0)
    {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

int sys_mknod(void)
{
    struct inode *ip;
    char *path;
    int major, minor;

    begin_op();
    if ((argstr(0, &path)) < 0 ||
        argint(1, &major) < 0 ||
        argint(2, &minor) < 0 ||
        (ip = create(path, T_DEV, major, minor)) == 0)
    {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

int sys_chdir(void)
{
    char *path;
    struct inode *ip;
    struct proc *curproc = myproc();

    begin_op();
    if (argstr(0, &path) < 0 || (ip = namei(path)) == 0)
    {
        end_op();
        return -1;
    }
    ilock(ip);
    if (ip->type != T_DIR)
    {
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    iput(curproc->cwd);
    end_op();
    curproc->cwd = ip;
    return 0;
}

int sys_exec(void)
{
    char *path, *argv[MAXARG];
    int i;
    uint uargv, uarg;

    if (argstr(0, &path) < 0 || argint(1, (int *)&uargv) < 0)
    {
        return -1;
    }
    memset(argv, 0, sizeof(argv));
    for (i = 0;; i++)
    {
        if (i >= NELEM(argv))
            return -1;
        if (fetchint(uargv + 4 * i, (int *)&uarg) < 0)
            return -1;
        if (uarg == 0)
        {
            argv[i] = 0;
            break;
        }
        if (fetchstr(uarg, &argv[i]) < 0)
            return -1;
    }
    return exec(path, argv);
}

int sys_pipe(void)
{
    int *fd;
    struct file *rf, *wf;
    int fd0, fd1;

    if (argptr(0, (void *)&fd, 2 * sizeof(fd[0])) < 0)
        return -1;
    if (pipealloc(&rf, &wf) < 0)
        return -1;
    fd0 = -1;
    if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0)
    {
        if (fd0 >= 0)
            myproc()->ofile[fd0] = 0;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }
    fd[0] = fd0;
    fd[1] = fd1;
    return 0;
}
