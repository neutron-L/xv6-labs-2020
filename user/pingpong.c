#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int parent2child[2]; // parent write and child read
    int child2parent[2]; // child write and parent read
    int pid;
    char ch;

    if (pipe(parent2child))
    {
        fprintf(2, "pipe error\n");
        exit(1);
    }
    if (pipe(child2parent))
    {
        fprintf(2, "pipe error\n");
        exit(1);
    }

    if ((pid = fork()) < 0)
    {
        fprintf(2, "fork error\n");
        exit(1);
    }
    else if (pid == 0)  // child process
    {
        // Close unnecessary pipeline descriptors
        close(parent2child[1]);
        close(child2parent[0]);

        if (read(parent2child[0], &ch, 1) != 1)
        {
            fprintf(2, "read error\n");
            exit(1);
        }
        printf("%d: received ping\n", getpid());
        if (write(child2parent[1], &ch, 1) != 1)
        {
            fprintf(2, "write error\n");
            exit(1);
        }

        close(parent2child[0]);
        close(child2parent[1]);
    }
    else // parent process
    {
        // Close unnecessary pipeline descriptors
        close(parent2child[0]);
        close(child2parent[1]);

        ch = ' ';
        if (write(parent2child[1], &ch, 1) != 1)
        {
            fprintf(2, "write error\n");
            exit(1);
        }
        if (read(child2parent[0], &ch, 1) != 1)
        {
            fprintf(2, "read error\n");
            exit(1);
        }
        printf("%d: received pong\n", getpid());

        close(child2parent[0]);
        close(parent2child[1]);
    }
    

    exit(0);
}
