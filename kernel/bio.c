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

#define NBUCKET 7
#define HASH(id) ((id) % NBUCKET)
#define DEBUG 0

struct bucket
{
    struct spinlock lock;
    struct buf free_list;
    struct buf head;
};

struct
{
    struct buf buf[NBUF];
#if DEBUG
    struct spinlock lock;
#endif
    // Linked list of all free buffers, through prev/next.
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
}

// Remove from hash table
static void
remove(struct buf *b)
{
    b->next->prev = b->prev;
    b->prev->next = b->next;
}

static struct buf *
find(struct buf *head, uint dev, uint blockno)
{
    struct buf *b = 0;
    for (b = head->next; b != head; b = b->next)
        if (b->dev == dev && b->blockno == blockno)
            break;

    return b == head ? 0 : b;
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

#if DEBUG
    initlock(&bcache.lock, "bcache");
#endif

    // Init has_table and bucket lock
    for (i = 0; i < NBUCKET; ++i)
        bucket_init(&bcache.hash_table[i]);

    // Put MAXIMUM free buffers to hash table
    i = 0;
    for (b = bcache.buf; b < bcache.buf + NBUF; b++)
    {
        initsleeplock(&b->lock, "buffer");
        struct buf *head = &bcache.hash_table[i].free_list;
        insert_head(b, head);
        i = (i + 1) % NBUCKET;
    }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
    struct buf *b = 0;
    struct buf *head;

#if DEBUG
    acquire(&bcache.lock);
#endif

    int id = HASH(blockno);
    struct bucket *bkt = &bcache.hash_table[id];
    acquire(&bkt->lock);

    // Is the block already cached?
    head = &bcache.hash_table[id].head;
    b = find(head, dev, blockno);
    if (b)
    {
        b->refcnt++;
        // remove to bucket front
        remove(b);
        goto found;
    }
    // Not cached.
    // Are there any unused buffers in the current bucket?
    else
    {
        // 按顺序 从其他bucket中寻找free buffer
        for (int i = 0; i < NBUCKET; ++i)
        {
            if (i == id)
            {
                head = &bcache.hash_table[id].free_list;
                if (head->prev != head)
                {
                    b = head->prev;
                    // remove from free list
                    remove(b);
                    goto init;
                }
            }
            else if (i < id)
            {
                release(&bkt->lock); // 暂时释放
                acquire(&bcache.hash_table[i].lock);
                acquire(&bkt->lock);

                // 检查是否已经存在
                head = &bcache.hash_table[id].head;
                b = find(head, dev, blockno);
                if (b)
                {
                    b->refcnt++;
                    // remove to bucket front
                    remove(b);
                    release(&bcache.hash_table[i].lock);
                    goto found;
                }
                else
                {
                    head = &bcache.hash_table[i].free_list;
                    if (head->prev != head)
                    {
                        b = head->prev;
                        // remove from free list
                        remove(b);
                        release(&bcache.hash_table[i].lock);
                        goto init;
                    }
                    else
                        release(&bcache.hash_table[i].lock);
                }
            }
            else
            {
                acquire(&bcache.hash_table[i].lock);
                head = &bcache.hash_table[i].free_list;
                if (head->prev != head)
                {
                    b = head->prev;
                    // remove from free list
                    remove(b);
                    release(&bcache.hash_table[i].lock);
                    goto init;
                }
                else
                    release(&bcache.hash_table[i].lock);
            }
        }
    }
    printf("dev %d blockno %d id %d\n", dev, blockno, id);
    panic("bget: no buffers");

    // initialize buffer
init:
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;

found:
    insert_head(b, &bkt->head);
    release(&bkt->lock);
#if DEBUG
    release(&bcache.lock);
#endif
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

#if DEBUG
    acquire(&bcache.lock);
#endif
    int id = HASH(b->blockno);
    struct bucket *bkt = &bcache.hash_table[id];
    acquire(&bkt->lock);
    b->refcnt--;
    if (b->refcnt == 0)
    {
        // no one is waiting for it.
        // remove from hash table
        remove(b);
        insert_head(b, &bkt->free_list);
    }

    release(&bkt->lock);
#if DEBUG
    release(&bcache.lock);
#endif
}

void bpin(struct buf *b)
{
    int id = HASH(b->blockno);
    struct bucket *bkt = &bcache.hash_table[id];
#if DEBUG
    acquire(&bcache.lock);
#endif
    acquire(&bkt->lock);
    // acquire(&bcache.lock);
    b->refcnt++;
    // release(&bcache.lock);
    release(&bkt->lock);
#if DEBUG
    release(&bcache.lock);
#endif
}

void bunpin(struct buf *b)
{
    int id = HASH(b->blockno);
    struct bucket *bkt = &bcache.hash_table[id];
#if DEBUG
    acquire(&bcache.lock);
#endif
    acquire(&bkt->lock);
    // acquire(&bcache.lock);
    b->refcnt--;
    // release(&bcache.lock);
    release(&bkt->lock);
#if DEBUG
    release(&bcache.lock);
#endif
}
