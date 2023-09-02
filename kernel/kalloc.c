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

uint8 refcount[(PHYSTOP - KERNBASE) >> PGSHIFT];
struct spinlock reflock;

struct run
{
    struct run *next;
};

struct
{
    struct spinlock lock;
    struct run *freelist;
} kmem;

void kinit()
{
    initlock(&kmem.lock, "kmem");
    initlock(&reflock, "reflock");
    freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    {
        increment_ref((uint64)p);
        kfree(p);
    }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    if (decrement_ref((uint64)pa) != 0)
        return;
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
    struct run *r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r)
    {
        kmem.freelist = r->next;

        if (increment_ref((uint64)r) != 1)
        {
            printf("%p\n", r);
            panic("kalloc: initial ref should be 1");
        }
    }
    release(&kmem.lock);

    if (r)
        memset((char *)r, 5, PGSIZE); // fill with junk
    return (void *)r;
}

uint8 add_ref(uint64 pa, int delta)
{
    uint8 ref;
    int idx = (PGROUNDDOWN(pa) - KERNBASE) >> PGSHIFT;
    // printf("add ref: %p %d\n", pa, idx);
    acquire(&reflock);
    refcount[idx] += delta;
    ref = refcount[idx];
    release(&reflock);

    return ref;
}

uint8 increment_ref(uint64 pa)
{
    return add_ref(pa, 1);
}

uint8 decrement_ref(uint64 pa)
{
    return add_ref(pa, -1);
}

uint8 get_ref(uint64 pa)
{
    return add_ref(pa, 0);
}

void *
cow_copy_page(uint64 pa)
{
    if (get_ref(pa) == 1)
        return (void *)pa;
    else
        return kalloc();
}