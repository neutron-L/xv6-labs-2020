#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
    int n;
    if (argint(0, &n) < 0)
        return -1;
    exit(n);
    return 0; // not reached
}

uint64
sys_getpid(void)
{
    return myproc()->pid;
}

uint64
sys_fork(void)
{
    return fork();
}

uint64
sys_wait(void)
{
    uint64 p;
    if (argaddr(0, &p) < 0)
        return -1;
    return wait(p);
}

uint64
sys_sbrk(void)
{
    int addr;
    int n;

    if (argint(0, &n) < 0)
        return -1;
    addr = myproc()->sz;
    if (growproc(n) < 0)
        return -1;
    return addr;
}

uint64
sys_sleep(void)
{
    int n;
    uint ticks0;

    if (argint(0, &n) < 0)
        return -1;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n)
    {
        if (myproc()->killed)
        {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

uint64
sys_kill(void)
{
    int pid;

    if (argint(0, &pid) < 0)
        return -1;
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}

static char str[64];
static uint64 len;
// give kernel the str
uint64
sys_down(void)
{
    int n;
    uint64 src;

    if (argaddr(0, &src) < 0 || argint(1, &n) < 0)
        return -1;

    if (copyinstr(myproc()->pagetable, str, src, 4096) < 0)
        return -1;
    len = n;
    printf("kernel %s %d\n", str, len);
    return 0;
}

// get the kernel str
uint64
sys_up(void)
{
    int n;
    uint64 dst;

    if (argaddr(0, &dst) < 0 || argint(1, &n) < 0)
        return -1;
    printf("up %s %d\n", str, len);

    if (copyout(myproc()->pagetable, dst, str, n) < 0)
        return -1;
    printf("up %s %d\n", str, len);

    len = 0;
    return 0;
}
