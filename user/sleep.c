#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int is_numstr(char * str)
{
    while (*str && '0' <= *str && '9' > *str)
        ++str;
    return *str ? 0 : 1;
}

int
main(int argc, char *argv[])
{
  if (argc != 2 || !is_numstr(argv[1]))
    fprintf(2, "usage: sleep <seconds>\n");
  else
    sleep(atoi(argv[1]));

  exit(0);
}
