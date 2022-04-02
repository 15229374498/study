#ifndef __MEMPOOL_H__
#define __MEMPOOL_H__
#include <string.h>
#include <stdlib.h>

#define MP_ALLWAYS_FREE_BUCKET

#ifndef _NO_PRINT
#define log printf
#else
#define log(x)
#endif

#define MP_PAGE_SIZE (4 * 1024) // 页大小4k，不要让用户设置的 block大小超过这个值
#define MP_MIN_BLK_SZIE 128     // 一个块过小失去意义
#define MP_MEM_ALIGN 32
#define MP_MAX_BLOCK_FAIL_TIME 4

typedef unsigned char* ADDR;

struct _MP_BLOCK {
    struct _MP_BLOCK* next;	// block的next指针，链起pool中所有block
    ADDR start_of_rest;     // 当前 block剩余空间的起始地址
    ADDR end_of_block;      // 当前 block的最后一个地址加1
    int failed_time;        // 这个 block被申请内存时出现失败的次数
    int ref_counter;        // 引用计数
};
typedef struct _MP_BLOCK MP_BLOCK;

struct _MP_PIECE {
    MP_BLOCK* block;         	// 所处的 block
    unsigned char data[0];		// 分配的可用内存跟在_MP_PIECE后
};
typedef struct _MP_PIECE MP_PIECE;

struct _MP_BUCKET {         // 超过 BLOCK_SIZE 内存用 MP_BUCKET 描述
   struct _MP_BUCKET* next;
   int still_in_use;        // 这个bucket描述符当前有没有被释放掉，如果被释放了可以尝试复用
   ADDR start_of_bucket;	// 实际分配的bucket内存不跟在bucket描述符后
};
typedef struct _MP_BUCKET MP_BUCKET;

struct _MP_POOL {
    size_t block_size;
    MP_BLOCK* current_block;    // 当前使用的 block，申请内存时优先使用这个 block（也就是开始遍历的那个 block）
    MP_BUCKET* first_bucket;    // 内存池中的第一个 bucket的位置
    int auto_clear;             // 内存池是否自动做清理
    MP_BLOCK first_block[0];    // pool描述符后是内存池中的第一个 block的位置，block的可分配内存跟在block描述符后
};
typedef struct _MP_POOL MP_POOL;

MP_POOL* mp_create_pool(size_t size, int auto_clear);
void mp_destroy_pool(MP_POOL* pool);
void* mp_malloc(MP_POOL* pool, size_t size);
void mp_free(MP_POOL* pool, void* addr);
void mp_reset_pool(MP_POOL* pool);
void mp_pool_statistic(MP_POOL* pool);

#endif
