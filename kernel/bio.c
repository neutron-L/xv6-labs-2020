// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define HASH(id) ((id) % NBUCKET)
#define MINIMUM_BLOCKS 2
#define MAXIMUM_BLOCKS 4
#define MAXIMUM_FREE_BLOCKS 2

struct bucket
{
    struct spinlock lock;
    struct buf free_list;
    struct buf head;
    int frees;
    int tot;
};

struct
{
    struct spinlock lock;
    struct buf buf[NBUF];

    // Linked list of all free buffers, through prev/next.
    struct buf head;
    struct bucket hash_table[NBUCKET];
} bcache;

static void
bucket_init(struct bucket *bk)
{
    static char lockname[16];

    snprintf(lockname, 16, "bucket%d");
    initlock(&bk->lock, lockname);
    bk->free_list.prev = bk->free_list.next = &bk->free_list;
    bk->head.prev = bk->head.next = &bk->head;
    bk->frees = bk->tot = 0;
}

// Remove from hash table
static void
remove(struct buf *b)
{
    b->next->prev = b->prev;
    b->prev->next = b->next;
}

static void
insert_head(struct buf *b, struct buf *head)
{
    b->next = head->next;
    head->next->prev = b;
    b->prev = head;
    head->next = b;
}

void binit(void)
{
    struct buf *b;
    int i;
    initlock(&bcache.lock, "bcache");

    // Init has_table and bucket lock
    for (i = 0; i < NBUCKET; ++i)
        bucket_init(&bcache.hash_table[i]);

    // Create linked list of free buffers
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    // Put MAXIMUM free buffers to hash table
    i = 0;
    int n = NBUF;
    if (n > MAXIMUM_FREE_BLOCKS * NBUCKET)
        n = MAXIMUM_FREE_BLOCKS * NBUCKET;
    for (b = bcache.buf; b < bcache.buf + n; b++)
    {
        initsleeplock(&b->lock, "buffer");
        struct buf *head = &bcache.hash_table[i].free_list;
        insert_head(b, head);
        ++bcache.hash_table[i].frees;
        ++bcache.hash_table[i].tot;
        i = (i + 1) % NBUCKET;
    }

    // 剩余的放入global free list
    if (n < NBUF)
    {
        for (b = bcache.buf + n; b < bcache.buf + NBUF; b++)
        {
            initsleeplock(&b->lock, "buffer");
            insert_head(b, &bcache.head);
        }
    }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
    struct buf *b;

    int id = HASH(blockno);
    struct bucket *bkt = &bcache.hash_table[id];
    acquire(&bkt->lock);

    // Is the block already cached?
    struct buf *head = &bcache.hash_table[id].head;
    for (b = head->next; b != head; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            // remove from hash table
            remove(b);
            insert_head(b, head);
            release(&bkt->lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // Not cached.
    // Is there a unused buffer?
    b = 0;
    if (bkt->frees)
    {
        b = bkt->free_list.prev;
        remove(b);
        --bkt->frees;
    }
    else if (bkt->tot < MAXIMUM_BLOCKS)
    {
        // Recycle the free buffer list.
        acquire(&bcache.lock);
        if (bcache.head.prev != &bcache.head)
        {
            b = bcache.head.prev;
            // remove from free list
            remove(b);
            release(&bcache.lock);
            ++bkt->tot;
        }
        else
            release(&bcache.lock);
    }

    if (!b) // 从本地替换
    {
        b = bkt->head.prev;
        remove(b);
    }

    // initialize buffer
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;

    insert_head(b, &bkt->head);
    release(&bkt->lock);
    acquiresleep(&b->lock);
    
    return b;

    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid)
    {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    int id = HASH(b->blockno);
    struct bucket *bkt = &bcache.hash_table[id];
    acquire(&bkt->lock);
    b->refcnt--;
    if (b->refcnt == 0)
    {
        // no one is waiting for it.
        // remove from hash table
        remove(b);

        if (bkt->frees == MAXIMUM_FREE_BLOCKS && bkt->tot > MINIMUM_BLOCKS) // put to global free list
        {
            acquire(&bcache.lock);
            insert_head(b, &bcache.head);
            release(&bcache.lock);

            --bkt->tot;
        }
        else
        {
            insert_head(b, &bkt->free_list);
            ++bkt->frees;
        }
    }

    release(&bkt->lock);
}

void bpin(struct buf *b)
{
    int id = HASH(b->blockno);
    struct bucket *bkt = &bcache.hash_table[id];
    acquire(&bkt->lock);
    // acquire(&bcache.lock);
    b->refcnt++;
    // release(&bcache.lock);
    release(&bkt->lock);
}

void bunpin(struct buf *b)
{
    int id = HASH(b->blockno);
    struct bucket *bkt = &bcache.hash_table[id];
    acquire(&bkt->lock);
    // acquire(&bcache.lock);
    b->refcnt--;
    // release(&bcache.lock);
    release(&bkt->lock);
}
