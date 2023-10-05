// VMA cache.
//
// The vmas cache is a linked list of free vm_area structures.
//
// Interface:
// * To get a vma, call vma_get.
// * After using, call vma_put to free it to cache.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "vma.h"

struct
{
    struct spinlock lock;
    struct vm_area vmas[NVMA];

    // Linked list of all vmas, through prev/next.
    struct vm_area head;
} vma_cache;

void 
vma_cache_init(void)
{
    struct vm_area *v;

    initlock(&vma_cache.lock, "vmacache");

    // Create linked list of buffers
    vma_cache.head.prev = &vma_cache.head;
    vma_cache.head.next = &vma_cache.head;
    for (v = vma_cache.vmas; v < vma_cache.vmas + NVMA; v++)
    {
        v->next = vma_cache.head.next;
        v->prev = &vma_cache.head;
        vma_cache.head.next->prev = v;
        vma_cache.head.next = v;
    }
}

// Look through vma cache for free vma.
// If no free vma, panic.
// In either case, return vma.
struct vm_area *
vma_get()
{
    struct vm_area *v;

    acquire(&vma_cache.lock);

    // Are there free vmas?
    v = vma_cache.head.next;
    if (v != &vma_cache.head)
    {
        vma_cache.head.next = v->next;
        v->next->prev = &vma_cache.head;
        release(&vma_cache.lock);
        return v;
    }

    else
        panic("vma_get: no free vmas");
}

// Move v from origin list to head`s next
void 
vma_insert(struct vm_area *head, struct vm_area *v)
{
    v->next->prev = v->prev;
    v->prev->next = v->next;
    v->next = head->next;
    v->prev = head;
    head->next->prev = v;
    head->next = v;
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void vma_put(struct vm_area *v)
{
    acquire(&vma_cache.lock);

    // v->next->prev = v->prev;
    // v->prev->next = v->next;
    // v->next = vma_cache.head.next;
    // v->prev = &vma_cache.head;
    // vma_cache.head.next->prev = v;
    // vma_cache.head.next = v;
    vma_insert(&vma_cache.head, v);

    release(&vma_cache.lock);
}
