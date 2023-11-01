#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include <stddef.h>


int
main(int argc, char *argv[])
{
  printf(1,"This is the munmap system call, printed in munmap.c\n");

  int testingAddrVar = 1;
  int *var = &testingAddrVar;
  size_t length = 4096;


  munmap(var, length);
  exit();
}
