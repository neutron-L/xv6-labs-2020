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

struct
{
    struct spinlock lock;
    struct buf buf[NBUF];

    // Linked list of all free buffers, through prev/next.
    struct buf head;
    struct buf hash_table[NBUCKET];
    struct spinlock bucket_lock[NBUCKET];
} bcache;

// Remove from hash table
static void
remove_from_bucket(struct buf *b)
{
    b->hash_next->hash_prev = b->hash_prev;
    b->hash_prev->hash_next = b->hash_next;
}


void binit(void)
{
    struct buf *b;

    initlock(&bcache.lock, "bcache");

    // Create linked list of free buffers
    bcache.head.prev = &bcache.head;
    bcache.head.next = &bcache.head;
    for (b = bcache.buf; b < bcache.buf + NBUF; b++)
    {
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        initsleeplock(&b->lock, "buffer");
        bcache.head.next->prev = b;
        bcache.head.next = b;
    }

    // Init has_table and bucket lock
    char lockname[16];
    for (int i = 0; i < NBUCKET; ++i)
    {
        snprintf(lockname, 16, "bucket_lock%d");
        initlock(&bcache.bucket_lock[i], "lockname");
        bcache.hash_table[i].hash_prev = bcache.hash_table[i].hash_next = &bcache.hash_table[i];
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
    acquire(&bcache.bucket_lock[id]);

    // Is the block already cached?
    struct buf *head = &bcache.hash_table[id];
    for (b = head->hash_next; b != head; b = b->hash_next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            // remove from hash table
            remove_from_bucket(b);
            goto found;
        }
    }

    // Not cached.
    // 1. Recycle the free buffer list.
    acquire(&bcache.lock);
    if (bcache.head.next != &bcache.head)
    {
        b = bcache.head.next;
        // remove from free list
        b->prev->next = b->next;
        b->next->prev = b->prev;

        // initialize buffer
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache.lock);
        goto found;
        //   release(&bcache.bucket_lock[id]);

        //   acquiresleep(&b->lock);
        //   return b;
    }
    release(&bcache.lock);

    // 2. Starting from the current bucket, find the least used buffer in the index order of the bucket
    int next_id;
    for (int i = 0; i < NBUCKET; ++i)
    {
        next_id = (id + i) % NBUCKET;
        if (i)
            acquire(&bcache.bucket_lock[next_id]);
        head = &bcache.hash_table[next_id];
        b = head->hash_prev;
        if (b != head)
        {
            // remove from origin hash bucket
            remove_from_bucket(b);
            if (i)
                release(&bcache.bucket_lock[next_id]);
            goto found;
        }
        else
        {
            if (i)
                release(&bcache.bucket_lock[next_id]);
        }
    }

    release(&bcache.bucket_lock[id]);
    panic("bget: no buffers");
found:
    // insert into current bucket head
    b->hash_next = bcache.hash_table[id].hash_next;
    bcache.hash_table[id].hash_next->hash_prev = b;
    b->hash_prev = &bcache.hash_table[id];
    bcache.hash_table[id].hash_next = b;

    release(&bcache.bucket_lock[id]);
    acquiresleep(&b->lock);
    return b;
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
    acquire(&bcache.bucket_lock[id]);
    b->refcnt--;
    if (b->refcnt == 0)
    {
        // no one is waiting for it.
        // remove from hash table
        remove_from_bucket(b);

        acquire(&bcache.lock);
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        bcache.head.next->prev = b;
        bcache.head.next = b;
        release(&bcache.lock);
    //     b->hash_prev = bcache.hash_table[id].hash_prev;
    //     bcache.hash_table[id].hash_prev->hash_next = b;
    //     b->hash_next = &bcache.hash_table[id];
    //     bcache.hash_table[id].hash_prev = b;
    }
    release(&bcache.bucket_lock[id]);
}

void bpin(struct buf *b)
{
    int id = HASH(b->blockno);
    acquire(&bcache.bucket_lock[id]);
    // acquire(&bcache.lock);
    b->refcnt++;
    // release(&bcache.lock);
    release(&bcache.bucket_lock[id]);
}

void bunpin(struct buf *b)
{
    int id = HASH(b->blockno);
    acquire(&bcache.bucket_lock[id]);
    // acquire(&bcache.lock);
    b->refcnt--;
    // release(&bcache.lock);
    release(&bcache.bucket_lock[id]);
}
