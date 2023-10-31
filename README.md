# 537P5
https://git.doit.wisc.edu/cdis/cs/courses/cs537/public/p5
## Top Task
1. Add system calls mmap and munmap
2. Add functionality for mmap to do MAP_FIXED and MAP_ANONYMOUS
3. Add functionality for munmap to remove mappings
4. Add system call logic in sysfile.c (I think this will make a bit more sense than sysproc.c as the functionality is more similar to that in sysfile.c)
5. Checkout vm.c, this is where virtual memory is being handled so will most likely need to make a few changes here
6. On fails, signal should be sent to trap.c so may need to make some adjustments so that we handle our segfault conditions correctly, i.e accessing a page they are not supposed to
7. We have mmap.h but will want some sort of struct that we can use to track where things have been placed in memory... not sure yet how to approach this.
