struct vm_area
{
    uint64 addr;
    uint length;
    uint prot;
    uint flags;
    
    // if mapped-file
    struct file *fp;
    uint offset;

    // linked vmas in process or in cache.
    struct vm_area *prev;
    struct vm_area *next;
};