#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 4){
    printf(2, "Usage 1: ln [-h] [old] [new]\n");
    printf(2, "Usage 2: ln [-s] [old] [new]\n");

    exit();
  }

  if(!strcmp(argv[1], "-h")) {
    if(link(argv[2], argv[3]) < 0)
      printf(2, "link %s %s: failed\n", argv[2], argv[3]);
  }
  else if (!strcmp(argv[1], "-s")) {
    if(symlink(argv[2], argv[3]) < 0)
      printf(2, "symlink %s %s: failed\n", argv[2], argv[3]);
  }
  sync();
  exit();
}
