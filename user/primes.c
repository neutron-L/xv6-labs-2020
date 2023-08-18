#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

__attribute__((noreturn))
void 
pipeline(int fd)
{
    int to_child[2];
    int num, factor = 0; // num保存读取到的数字，factor保存该进程持有的prime，初始时为0，读取到的第一个num即factor
    int readn, pid;
    int has_child = 0;
    int exit_status = 0;

    while (1)
    {
        readn = read(fd, &num, sizeof(int));
        if (!readn) // 写入端已关闭，则退出
            break;
        else if (readn == sizeof(int))
        {
            if (factor == 0)
            {
                factor = num;
                printf("prime %d\n", factor);
            }
            else if (num % factor)
            {
                // 判断是否创建了右侧邻居孩子进程
                if (!has_child)
                {
                    has_child = 1;

                    // init pipe
                    if (pipe(to_child))
                    {
                        fprintf(2, "pipe error\n");
                        exit_status = 1;
                        break;
                    }

                    pid = fork();
                    if (pid < 0)
                    {
                        fprintf(2, "fork error\n");
                        exit_status = 1;
                        break;
                    }
                    else if (!pid)
                    {
                        close(to_child[1]); // close write-end
                        pipeline(to_child[0]);
                    }
                    else
                        close(to_child[0]); // close read-end
                }

                // 写入
                if (write(to_child[1], &num, sizeof(int)) != sizeof(int))
                {
                    fprintf(2, "write error\n");
                    exit(1);
                }
            }
        }
        else
        {
            fprintf(2, "read error\n");
            exit_status = 1;
            break;
        }
    }

    close(fd);
    if (has_child)
    {
        close(to_child[1]);
        if (wait(0) != pid)
        {
            fprintf(2, "wait error\n");
            exit_status = 1;
        }
    }
    exit(exit_status);
}

// generate process
int main(int argc, char *argv[])
{
    int to_child[2]; // send generating numbers
    int pid;

    if (pipe(to_child))
    {
        fprintf(2, "pipe error\n");
        exit(1);
    }

    if ((pid = fork()) < 0)
    {
        fprintf(2, "fork error\n");
        exit(1);
    }
    else if (pid == 0) // child process
    {
        close(to_child[1]); // close write-end
        pipeline(to_child[0]);
    }
    else // parent process
    {
        close(to_child[0]); // close read-end

        for (int i = 2; i <= 35; ++i)
        {
            if (write(to_child[1], &i, sizeof(int)) != sizeof(int))
            {
                fprintf(2, "write error\n");
                exit(1);
            }
        }

        close(to_child[1]);

        if (wait(0) != pid)
        {
            fprintf(2, "wait error\n");
            exit(1);
        }
    }

    exit(0);
}
