## Idea 1：
维护一个free list和哈希表，每个bucket本身是一个lru双向链表，每个桶中如果没有找到相应的buffer，则从freelist中获取，释放的buffer也放入freelist。如果freelist为空，则从当前bucket开始，顺序搜索哈希表找到一个非空bucket中的lru buffer作为替代。  
tot = 5000+，**failed**  
存在的问题：有**死锁**。  
## Idea 2:
为了降低并发获取锁冲突，在idea 1的基础上，需要减少对freelist的访问。此外，为了避免死锁，必须注意，在从其他bucket中获取buffer的时候，获取锁的顺序。即可能两个CPU，一个拥有i号bucket的锁，另一个拥有j的bucket锁，可能试图获取对方的锁而查找对方的bucket中是否有空闲buffer，因此需要避免直接从其他bucket中获取buffer的行为。  
- 每个bucket维持一个buffer数变量，并设置一个阈值，如果buffer数量超过了阈值则释放的时候将buffer放入free list
- 当需要获取空闲buffer时，只有本bucket中buffer的数量较少才去free list获取，并且保证bucket中的buffer数量不低于最小阈值（不为0）
存在的问题：
- 不能充分利用其他bucket中的free buffer。当前bucket和global free list中没有空闲buffer时，不能使用其他bucket中的空闲buffer
- 两个heisenbug
    - freeing free buffer：据说是buffer未被独占，刚开始几次测试都有，后来几乎不再出现
    - ilock：no type，暂不清楚原因

## Idea 3:
在最开始的xv6的LRU设计中，需要充分利用free buffer，为了解决Idea 2中存在的该问题，因此摒弃Idea 2中关于阈值以及global free list的设计。  
- 每个bucket中有一个存放使用中的buffer的head，以及一个local free list
- 当bucket中没有空闲buffer时，优先从local free list中寻找；释放buffer也仅将buffer放入local free list
- 如果当前bucket中没有空闲buffer，按顺序从每个bucket中寻找空闲buffer。
如Idea 2中所描述，如果要从其他bucket中获取free buffer，可能遇到deadlock，因此每次都按索引顺序获取bucket锁。即当访问第i个bucket时，可能需要从第j个bucket中寻找空闲buffer。
- 如果j > i，则直接获取第j个bucket锁即可
- 如果j == i，不用获取锁，因为此时以及有i号bucket锁
- 如果j < i，需要先放弃第i号bucket锁，再重新获取。此时，可能另一个cpu上运行的线程，在第k号bucket中释放了一个buffer（k < j），然后另一个cpu抢在当前cpu之前，执行了一遍重复的工作，并把第k个bucket中的空闲buffer分配到了第i个bucket中。因此，当释放第i号锁后，再重新获取到第j号锁和第i号锁，此时可能想要找的buffer已经存在了，因此需要做一次检查。  
存在的问题：
- usertests最后一个测试用例bigdir无法通过，而现在运行make grade大多超时……（timeout是300s，但是基本运行完要300多s）