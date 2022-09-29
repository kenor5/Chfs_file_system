#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{

  if (id > BLOCK_NUM || buf == nullptr) return;

  memcpy(buf, blocks[id], BLOCK_SIZE);
  // for (int i = 0; i < BLOCK_NUM; ++i)
  // {buf[i] = blocks[id][i];std::cerr<<i<<std::endl;}

}

void
disk::write_block(blockid_t id, const char *buf)
{
  if (id > BLOCK_NUM || buf == nullptr) return;
  memcpy(blocks[id], buf,  BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */
  for (uint i = FILESTART; i < BLOCK_NUM; ++i) {
    if (!using_blocks[i]) {
      using_blocks[i] = 1;
      return i;
    }
  }
  assert(false);

  return 0;
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */
  using_blocks[id] = 0;
  
  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */
  for (uint i = 1; i < INODE_NUM; ++i) {
    struct inode* tmp = get_inode(i);
    
    if (tmp == nullptr) {
      tmp = new inode();
      tmp->type = type;
      tmp->atime = tmp->ctime = tmp->mtime = time(NULL);
  
      put_inode(i, tmp);

      free(tmp);

      return i;
    }
    
    free(tmp);
  }
  return 1;
}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */

  struct inode* cur = get_inode(inum);
  if (!cur) return;
  assert(cur->type !=0);
  cur->type = 0;
  this->put_inode(inum, cur);
  
  free(cur);

  return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino;
  /* 
   * your code goes here.
   */
  char buf[BLOCK_NUM];
  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  struct inode* rt = (struct inode*)buf + inum%IPB;
  if (rt->type == 0) {
    return nullptr;
  }
  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *rt;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_out
   */
  struct inode* cur = get_inode(inum);
  if (!cur) {
    return;
  }

  *size = cur->size;

  blockid_t block_num = *size / BLOCK_SIZE;

  if (*size % BLOCK_SIZE) block_num ++;
  assert(block_num <= MAXFILE);
  *buf_out = (char *)malloc(block_num * BLOCK_SIZE);
  // why malloc?
 
   for (uint i = 0; i < block_num; ++i) {
    bm->read_block(get_nth_block(cur, i), *buf_out + i * BLOCK_SIZE);

  }

  // update access time
  cur->atime = time(NULL);
  put_inode(inum, cur);
  free(cur);
  return;
}

void inode_manager::alloc_inode_block(struct inode* cur, blockid_t start, blockid_t num) {
  assert(start + num <= MAXFILE);
  char buf[BLOCK_SIZE];

  for (uint32_t i = start; i < start + num; i++) {
    if (i >= NDIRECT)  {
      if (!cur->blocks[NDIRECT]) {
        cur->blocks[NDIRECT] = bm->alloc_block();
      }
      bm->read_block(cur->blocks[NDIRECT], buf);
      for (uint j = i; j < start+num; ++j)
        ((blockid_t *)buf)[j-NDIRECT] = bm->alloc_block();
      bm->write_block(cur->blocks[NDIRECT], buf);
      break;
    }else {
      cur->blocks[i] = bm->alloc_block();
    }
  }

}

void inode_manager::free_inode_block(struct inode* cur, blockid_t start, blockid_t num) {
  assert(start + num <= MAXFILE);
  char buf[BLOCK_SIZE];

  if (start + num >= NDIRECT) {
    bm->read_block(cur->blocks[NDIRECT], buf);
    if (start >= NDIRECT) {
      for (blockid_t i = start - NDIRECT; i < start + num - NDIRECT; ++i) {
        bm->free_block(((blockid_t *)buf)[i]);
      }
      if (start == NDIRECT)
        bm->free_block(cur->blocks[NDIRECT]);

    }else {
      for (blockid_t i = 0; i < start + num - NDIRECT; ++i) {
        bm->free_block(((blockid_t *)buf)[i]);
      }

      for (blockid_t i = start; i <= NDIRECT; ++i) {
        bm->free_block(cur->blocks[i]);
      }

    }
  }else {
    for (blockid_t i = start; i < start + num; ++i) {
        bm->free_block(cur->blocks[i]);
      }
  }

}


blockid_t inode_manager::get_nth_block(struct inode* cur, blockid_t n) {
  assert(cur );
    if (n < NDIRECT) {
      return cur->blocks[n];
    }
    char buf[BLOCK_SIZE];
    bm->read_block(cur->blocks[NDIRECT], buf);
    return ((blockid_t *)buf)[n-NDIRECT];
} 


/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */
  struct inode* cur = get_inode(inum);
  if (!cur) {
    printf("write file error: current inode is null");
    return;
  }

  blockid_t origin_num = cur->size/BLOCK_SIZE;
  if (cur->size % BLOCK_SIZE) origin_num ++;

  blockid_t block_num = size / BLOCK_SIZE ;
  if (size % BLOCK_SIZE) block_num ++;

  if (origin_num > block_num) {
    free_inode_block(cur, block_num, origin_num - block_num);
  }else if (origin_num < block_num) {
    alloc_inode_block(cur, origin_num, block_num - origin_num);
  }

  
  for (uint i = 0; i < block_num; ++i) {
    bm->write_block(get_nth_block(cur, i), buf+i*BLOCK_SIZE);
  }
  

  cur->atime = cur->mtime = time(NULL);
  cur->size = size;
  put_inode(inum, cur);
  free(cur);
  return;
}

void
inode_manager::get_attr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  struct inode* node = get_inode(inum);
  if (!node) {
    printf("error");
    return;
  }
  a.atime = node->atime;
  a.ctime = node->ctime;
  a.mtime = node->mtime;
  a.type = node->type;
  a.size = node->size;

  free(node);
  
  return;
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your code goes here
   * note: you need to consider about both the data block and inode of the file
   */
  
  struct inode* cur = get_inode(inum);
  if (!cur) return;

  blockid_t block_num = cur->size / BLOCK_SIZE;
  if (cur->size % BLOCK_SIZE) block_num++;

  for (uint i = 0; i < block_num; ++i) {
    bm->free_block(get_nth_block(cur, i));
  }
  if (block_num >= NDIRECT)
    bm->free_block(NDIRECT);
    
  free(cur);
  free_inode(inum);
  
  return;
}
