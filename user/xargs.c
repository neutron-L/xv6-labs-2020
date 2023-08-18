#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[])
{
    char buf[512];
    int pid;
    char *my_args[MAXARG];
    int i;
    int j = 0, n;

    for (i = 1; i < argc; ++i)
        my_args[i - 1] = argv[i];
    my_args[i - 1] = my_args[i] = 0;

    while (1)
    {
        j = 0;
        while ((n = read(0, &buf[j], 1)) == 1)
        {
            ++j;
            if (buf[j - 1] == '\n')
                break;
        }
        if (n == 0)
            break;
        buf[j - 1] = '\0';
        my_args[i - 1] = buf;
        pid = fork();

        if (pid < 0)
        {
            fprintf(2, "fork error\n");
            continue;
        }
        else if (pid == 0)
            exec(my_args[0], my_args);

        if (wait(0) != pid)
            fprintf(2, "wait error\n");
    }

    exit(0);
}
