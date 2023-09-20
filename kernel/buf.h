struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // freelist
  struct buf *next;
  struct buf *hash_prev; // hash table buckets list
  struct buf *hash_next;
  uchar data[BSIZE];
};

