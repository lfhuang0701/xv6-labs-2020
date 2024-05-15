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

//roster new add
#define NBUCKET 13
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%NBUCKET)

struct {
  //struct spinlock lock; //删除原有的区块缓存全局锁
  struct buf buf[NBUF];

  // roster new add，删除原有双向链表，添加哈希表
  // // Linked list of all buffers, through prev/next.
  // // Sorted by how recently the buffer was used.
  // // head.next is most recent, head.prev is least.
  // struct buf head;
  struct buf bucket_buf[NBUCKET];       //散列桶
  struct spinlock bucket_lock[NBUCKET]; //散列桶的锁
  struct spinlock search_lock;          //寻找并再分配的锁
} bcache;

//new add
char lockname[16];

//设置一个初始状态的缓冲区链表
void
binit(void)
{
  struct buf *b;
  //roster new add，初始化所有桶锁
  for(int i = 0; i < NBUCKET; i++){
    snprintf(lockname, sizeof(lockname), "bcache_bucket%d", i);
    initlock(&bcache.bucket_lock[i], lockname);
  }
  initlock(&bcache.search_lock, "search_lock");
  //initlock(&bcache.lock, "bcache.lock");
  //将所有的缓冲区buf添加到第一个桶内
  for(int i = 0; i < NBUF; i++){
    b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer"); //初始化缓冲区的睡眠锁
    b->refcnt = 0;
    b->timestamp = 0;
    b->next = bcache.bucket_buf[0].next; //头插法插入链表头部
    bcache.bucket_buf[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  //printf("into bio\n");
  //acquire(&bcache.lock);
  struct buf *b;
  uint key = BUFMAP_HASH(dev, blockno); //获取对应的桶号

  acquire(&bcache.bucket_lock[key]);    //获取桶锁

  // Is the block already cached?
  for(b = bcache.bucket_buf[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket_lock[key]);
      //release(&bcache.lock);
      acquiresleep(&b->lock);
      //release(&bcache.lock);
      //printf("find and out\n");
      return b;
    }
  }

  //没有找到缓存
  //寻找所有桶，找到一个最近最少使用的buf，将其移出其所属的桶，加入到该桶内

  release(&bcache.bucket_lock[key]); //先释放桶锁，再获取search_lock,防止另一进程拿着search_lock获取此通锁，造成环路死锁
  acquire(&bcache.search_lock); //此所将寻找buf并再分配这个过程约束为单线程，防止重复为一个块分配buf
  //printf("acquire search_lock\n");
  // 获取search_lock后再次检查此桶有没有块对应的buf,以防在释放桶锁后有进程抢先一步为此桶分配新buf，若不检查，有可能重复分配
  for(b = bcache.bucket_buf[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bucket_lock[key]);
      b->refcnt++;
      release(&bcache.bucket_lock[key]);
      release(&bcache.search_lock);
      //release(&bcache.lock);
      acquiresleep(&b->lock);
      //release(&bcache.lock);
      //printf("again find and out\n");
      return b;
    }
  }

  //再次检查发现没有后，寻找所有其他的桶，并找到最近最少使用的buf，从原桶删除，加入到此桶内
  struct buf *LRU_buf = 0;       //目标buf的上一个结点
  struct buf *buf;
  int holding_bucket = -1;        //用来标记最新LRU_buf所属的桶

  for(int i = 0; i < NBUCKET; i++){
    int found = 0;                  //标志位，即如果在此桶更新了LRU_buf，暂时不释放此桶的锁

    acquire(&bcache.bucket_lock[i]); //先获取桶锁
    //printf("acquire bucket_lock%d\n", i);
    for(buf = &bcache.bucket_buf[i]; buf->next; buf = buf->next){ //遍历当前桶,这里遍历时使用目标buf的前一个结点，方便后续删除
      if(buf->next->refcnt == 0 && (LRU_buf == 0 || buf->next->timestamp < LRU_buf->next->timestamp)){
        LRU_buf = buf;    //这里LRU_buf是真正LRU_buf的上一个结点，为了方便后续从原桶删除结点，这里保存上一个结点
        found = 1;
      }
    }

    //判断当前桶是否更新了LRU_buf, 若没有更新，则释放此桶所，若更新了，则释放之前获取的桶锁，并将此桶的key传给holding_bucket
    if(!found)
      release(&bcache.bucket_lock[i]);
    else{
      if(holding_bucket != -1)    //防止第一个桶更新了LRU，释放桶出现错误
        release(&bcache.bucket_lock[holding_bucket]);
      holding_bucket = i;
    }
  }
  if(!LRU_buf) {
    panic("bget: no buffers");
  }
  buf = LRU_buf->next; //这里的buf是真正寻找的buf

  //遍历结束后，LRU_buf就是目标buf，且当前应该还持有目标buf所属的桶锁

  //判断holding_bucket是否为key，即是否在当前的桶找到LRU_buf,若不是，需要将LRU_buf从原桶删除，再加入本桶
  if(holding_bucket != key){
    //从原桶删除
    LRU_buf->next = buf->next;
    release(&bcache.bucket_lock[holding_bucket]);
    //加入本桶
    acquire(&bcache.bucket_lock[key]);
    //printf("acquire my bucket_lock%d\n", key);
    buf->next = bcache.bucket_buf[key].next;
    bcache.bucket_buf[key].next = buf;
  }

  //若holding_bucket = key,此时还获取着LRU_buf的锁，也就是本桶的锁，若不等于，上面语句也获取了本桶的锁，下面对缓存buf进行修改
  buf->dev = dev;
  buf->blockno = blockno;
  buf->refcnt = 1;
  buf->valid = 0;
  //释放本桶的锁,和search_lock
  release(&bcache.bucket_lock[key]);
  release(&bcache.search_lock);

  //获取buf的睡眠锁并返回
  //release(&bcache.lock);
  //printf("release bcache.lock\n");
  acquiresleep(&buf->lock);
  //printf("sleep lock\n");
  //release(&bcache.lock);
  //printf("out bio\n");
  return buf;

}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bucket_lock[key]);

  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it
    b->timestamp = ticks;
  }
  release(&bcache.bucket_lock[key]);
  
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bucket_lock[key]);
  b->refcnt++;
  release(&bcache.bucket_lock[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bucket_lock[key]);
  b->refcnt--;
  release(&bcache.bucket_lock[key]);
}


