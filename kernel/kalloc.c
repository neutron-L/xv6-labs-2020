// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
    struct run *next;
};

struct
{
    struct spinlock lock;
    struct run *freelist;
} kmems[NCPU];

void kinit()
{
    char kmem_name[8];
    for (int i = 0; i < NCPU; ++i)
    {
        snprintf(kmem_name, 5, "kmem%d");
        initlock(&kmems[i].lock, kmem_name);
    }
    freerange(end, (void *)PHYSTOP);
}

// Put the page of physical memory to specified
// free list
void put_page(struct run *r, int id)
{
    push_off();
    if (id == -1)
        id = cpuid();
    acquire(&kmems[id].lock);
    r->next = kmems[id].freelist;
    kmems[id].freelist = r;
    release(&kmems[id].lock);
    pop_off();
}

void freerange(void *pa_start, void *pa_end)
{
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    int id = 0;
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    {
        put_page((struct run *)p, id);
        id = (id + 1) % NCPU;
    }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    put_page((struct run *)pa, -1);

    //   acquire(&kmem.lock);
    //   r->next = kmem.freelist;
    //   kmem.freelist = r;
    //   release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
    struct run *r;

    push_off();
    int id = cpuid();
    int cycles = NCPU;
    do
    {
        acquire(&kmems[id].lock);
        r = kmems[id].freelist;
        if (r)
            kmems[id].freelist = r->next;
        release(&kmems[id].lock);
        id = (id + 1) % NCPU;
    } while (!r && --cycles != 0);

    if (r)
        memset((char *)r, 5, PGSIZE); // fill with junk

    pop_off();
    return (void *)r;
}
