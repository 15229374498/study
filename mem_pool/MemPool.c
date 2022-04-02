#include "MemPool.h"
#include <stdio.h>
#include <errno.h>

/******************************************
*name：		init_a_new_block
*brief:		初始化一个新block
*input:		pool：池对象；newblock：新block；
*output:	无
*return:	无
******************************************/
static inline void init_a_new_block(MP_POOL* pool, MP_BLOCK* newblock)
{
    newblock->next = NULL;
    newblock->ref_counter = 0;
    newblock->failed_time = 0;
    newblock->start_of_rest = (ADDR)newblock + sizeof(MP_BLOCK);
    newblock->end_of_block = newblock->start_of_rest + pool->block_size;
}

/******************************************
*name：		rest_block_space
*brief:		计算某block剩余可分配内存大小
*input:		block：需要计算的block
*output:	无
*return:	剩余大小
******************************************/
static inline size_t rest_block_space(MP_BLOCK* block)
{
    return block->end_of_block - block->start_of_rest;
}


/******************************************
*name：		malloc_a_block
*brief:		池中所有block空间不足时，新增一个block
*input:		pool：池对象
*output:	无
*return:	分配完成的block
******************************************/
static MP_BLOCK* malloc_a_block(MP_POOL* pool)
{
    MP_BLOCK* newblock;
    int ret = posix_memalign((void**)&newblock, MP_MEM_ALIGN, pool->block_size + sizeof(MP_BLOCK));
    if(ret)
    {
        log("[%d]posix_memalign error[%d].\n", __LINE__, errno);
        return NULL;
    }
    init_a_new_block(pool, newblock);

	//新block加入到pool的block链表末尾
    MP_BLOCK* block = pool->current_block;  // 从 current_block 开始找链表尾部就行
    while(block->next) block = block->next;
    block->next = newblock;     
    
    return newblock;
}

/******************************************
*name：		malloc_a_piece
*brief:		从池中某block分配一片内存
*input:		pool：池对象；size：分配大小
*output:	无
*return:	分配完成的内存的起始地址
******************************************/
static ADDR malloc_a_piece(MP_POOL* pool, size_t size)
{
    MP_BLOCK* block = pool->current_block;
    size_t real_piece_size = size + sizeof(MP_PIECE);

	//1、尝试找到一个剩余空间足够的block
    while(block)
    {
    	//找到了
        if(rest_block_space(block) >= real_piece_size)
        {   
            MP_PIECE* piece = (MP_PIECE*)block->start_of_rest;
            block->start_of_rest += real_piece_size;
            block->ref_counter++;
            piece->block = block;
            return (ADDR)piece->data;
        }

        // 对于申请失败的 block，增加其失败次数
        block->failed_time++;
        if(block->failed_time >= MP_MAX_BLOCK_FAIL_TIME)
            pool->current_block = block->next;    // 如果当前这个 block的失败次数太多了，就放弃这个 block，下一次从其后面开始遍历
        
        block = block->next;
    }
	
    //2、没找到，那就新建一个 block
    block = malloc_a_block(pool);
    if(block)
    {
        MP_PIECE* piece = (MP_PIECE*)block->start_of_rest;
        block->start_of_rest += real_piece_size;
        block->ref_counter++;
        piece->block = block;
        return (ADDR)piece->data;
    }
    else 
        return NULL;
}

/******************************************
*name：		malloc_a_bucket
*brief:		需要分配的内存大于block最大值，另外申请
*input:		pool：池对象；size：分配大小
*output:	无
*return:	分配完成的内存的起始地址
******************************************/
static ADDR malloc_a_bucket(MP_POOL* pool, size_t size)
{
    MP_BUCKET* bucket = pool->first_bucket; 
    MP_BUCKET* prev_bucket = bucket;

	//1、当前所有bucket中尝试找一个可用描述符
    while(bucket) 
    {    
        prev_bucket = bucket;
        if(bucket->start_of_bucket == NULL)
            break;  // bucket 描述符是可以复用的，因此找到一个已经被释放掉的 bucket就可以直接用它的描述符
        bucket = bucket->next;
    }

	//2、如果所有 bucket描述符当前都在使用，那就再从 block中申请一个新的 bucket 描述符
    if(bucket == NULL)
    {   
        bucket = (MP_BUCKET*)malloc_a_piece(pool, sizeof(MP_BUCKET));  
        if(bucket == NULL)
        {
            return NULL;
        }
		
        //该bucket所在的 block引用由 malloc_a_piece增加，此处不需要处理
        bucket->still_in_use = 0;
        if(prev_bucket) // 将新 bucket插到链表末尾
            prev_bucket->next = bucket;
        else            // prev_bucket 为 NULL说明这是链表的第一个节点
            pool->first_bucket = bucket;
    }
    else
    {   // 如果是复用之前的 bucket 描述符，则将其所在的 block 引用增加
        MP_PIECE *piece = (MP_PIECE *)((ADDR)bucket - sizeof(MP_PIECE));
        piece->block->ref_counter++;
    }

	//3、bucket描述符绑定新分配好的内存
    int ret = posix_memalign((void**)&bucket->start_of_bucket, MP_MEM_ALIGN, size);
    if(ret)
    {
        MP_PIECE *piece = (MP_PIECE *)((ADDR)bucket - sizeof(MP_PIECE));
        piece->block->ref_counter--;   // 出错则不能增加 block引用
        log("[%d]posix_memalign error[%d].\n", __LINE__, errno);
        return NULL;
    }

    bucket->still_in_use = 1;
    return bucket->start_of_bucket;
}

/******************************************
*name：		find_bucket_with_addr
*brief:		根据bucket内存起始地址，查找该bucket描述符
*input:		pool：池对象；addr：bucket内存起始地址
*output:	无
*return:	bucket*
******************************************/
static MP_BUCKET* find_bucket_with_addr(MP_POOL* pool, ADDR addr)
{
    MP_BUCKET* bucket = pool->first_bucket;
    while(bucket)
    {
        if(bucket->start_of_bucket == addr)
            return bucket;
        bucket = bucket->next;
    }
    return NULL;
}

/******************************************
*name：		find_block_with_addr
*brief:		根据一片内存的起始地址，查找该block描述符
*input:		pool：池对象；addr：内存起始地址
*output:	无
*return:	block*
******************************************/
static MP_BLOCK* find_block_with_addr(MP_POOL* pool, ADDR addr)
{
    MP_BLOCK* block = pool->first_block;
    while(block)
    {
        if( ((ADDR)block + sizeof(MP_BLOCK)) <= addr && addr < (ADDR)block->start_of_rest )
            return block;
        block = block->next;
    }

    return NULL;
}

/******************************************
*name：		clear_block
*brief:		若某block未被引用，自动清空该block内容，不是释放该block
*input:		pool：池对象；block：要清空的block
*output:	无
*return:	无
******************************************/
static void clear_block(MP_POOL* pool, MP_BLOCK* block)
{
	//1、该block仍有被引用，则不清空直接返回
    if(block->ref_counter > 0)
        return;

    //2、清空 block前先确保其中bucket描述符的内存都释放了
    MP_BUCKET* bucket = pool->first_bucket;
    MP_BUCKET* prev_bucket = bucket;
    while(bucket)
    {
        MP_PIECE *piece = (MP_PIECE *)((ADDR)bucket - sizeof(MP_PIECE));
        if(piece->block == block)   // 找出属于这个 block的 bucket确保其独立内存释放掉
        {
            if(bucket->start_of_bucket)
            {
                free(bucket->start_of_bucket);
                bucket->start_of_bucket = NULL;
            }
            //删除 bucket 要注意相应的调整 bucket 链表
            if(bucket == pool->first_bucket)
            {
                pool->first_bucket = bucket->next;
                prev_bucket = pool->first_bucket;
            }
            else
            {
                prev_bucket->next = bucket->next;
            }
        }
        else
        {
            prev_bucket = bucket;
        }
        bucket = bucket->next;
    }

    //3、恢复 block 参数
    block->ref_counter = 0;
    block->failed_time = 0;
    block->start_of_rest = (ADDR)block + sizeof(MP_BLOCK);
    pool->current_block = pool->first_block;    // 一定要将 current_block 也复位，否则可能导致后面一直不会用到这个 block
}

/******************************************
*name：		mp_create_pool
*brief:		创建线程池
*input:		size：指定池内存块大小；auto_clear：是否自动清理内存
*output:	无
*return:	返回线程池对象
******************************************/
MP_POOL* mp_create_pool(size_t size, int auto_clear)
{
    if(size < MP_MIN_BLK_SZIE) 
	{
		return NULL;
    }

	//1、分配空间
    MP_POOL* pool;
    size_t block_size = size < MP_PAGE_SIZE ? size : MP_PAGE_SIZE;
	size_t real_size = sizeof(MP_POOL) + block_size;
    int ret = posix_memalign((void**)&pool, MP_MEM_ALIGN, real_size);
    if(ret)
    {
        log("[%d]posix_memalign error[%d].\n", __LINE__, errno);
        return NULL;
    }

	//2、初始化池
    pool->block_size = block_size;
    pool->auto_clear = auto_clear;
    pool->first_bucket = NULL;
    pool->current_block = pool->first_block;  // first_block是柔性数组，不需要赋值，实际已经指向正确的位置

	//3、初始化第一个块
    init_a_new_block(pool, pool->first_block);
    return pool;
}

/******************************************
*name：		mp_destroy_pool
*brief:		释放整个池空间
*input:		pool：池对象
*output:	无
*return:	无
******************************************/
void mp_destroy_pool(MP_POOL* pool)
{
    mp_reset_pool(pool);
    MP_BLOCK* block = pool->first_block->next;  // 从第二个 block 开始释放内存！ 第一个比较特殊
    MP_BLOCK* prev_block = block;
    while (block)
    {
        prev_block = block;
        block = block->next;
        free(prev_block);
    }
    free(pool); // 释放池的描述符空间以及第一个初始 block
}

/******************************************
*name：		mp_malloc
*brief:		从内存池中分配内存
*input:		pool：池对象；size：分配大小；
*output:	无
*return:	分配完成的内存的起始地址
******************************************/
void* mp_malloc(MP_POOL* pool, size_t size)
{
    if(size <= 0 || pool == NULL) return NULL;

	//若小于等于block大小，从block分配
    if(size <= pool->block_size)
    {
        return malloc_a_piece(pool, size);
    }
    else 	//需要使用bucket
    {
        return malloc_a_bucket(pool, size);
    }
}

/******************************************
*name：		mp_free
*brief:		释放池中取出的空间
*input:		pool：池对象；addr：曾分配的内存起始地址；
*output:	无
*return:	无
******************************************/
void mp_free(MP_POOL* pool, void* addr)
{
    if(pool == NULL || addr == NULL)
        return ;

    MP_BLOCK* block = NULL;
    MP_BUCKET* bucket = find_bucket_with_addr(pool, addr);

	//1、先看一下这个地址是不是一个 bucket
    if(bucket)  // 这个地址是 bucket
    {
        #ifdef MP_ALLWAYS_FREE_BUCKET   // 每次清理 bucket时都将其独立内存空间释放，这里可能可以做优化
        free(bucket->start_of_bucket);
        bucket->start_of_bucket = NULL;
        #endif
        bucket->still_in_use = 0;
        MP_PIECE *piece = (MP_PIECE *)((ADDR)bucket - sizeof(MP_PIECE));
        block = piece->block;
    }
    else    // 如果这个地址不是 bucket， 则什么都不需要处理, 找出它所在的 block就行了
    {
        MP_PIECE *piece = (MP_PIECE *)(addr - sizeof(MP_PIECE));
        block = piece->block;
    }

	//2、减少block引用计数
    if(block)
        block->ref_counter--; 
    else
        return ; // 这个地址没有对应的内存块

    // 如果启用了自动清理内存池，则在适当的条件下执行清理操作
    if(pool->auto_clear)
        clear_block(pool, block);
}

/******************************************
*name：		mp_reset_pool
*brief:		主动重置各block空间，释放bucket
*input:		pool：池对象；
*output:	无
*return:	无
******************************************/
void mp_reset_pool(MP_POOL* pool)
{
    if(pool == NULL)
        return;
    
    //1、释放掉所有 bucket 的空间
    MP_BUCKET* bucket = pool->first_bucket;
    while(bucket)
    {
        if(bucket->start_of_bucket)
        {
            free(bucket->start_of_bucket);
            bucket->start_of_bucket = NULL;
        }
        
        bucket = bucket->next;
    }
    pool->first_bucket = NULL;

	//2、重置各block
    MP_BLOCK* block = pool->first_block;
    while(block)
    {
        block->ref_counter = 0;
        block->failed_time = 0;
        block->start_of_rest = (ADDR)block + sizeof(MP_BLOCK);
        block = block->next;
    }

    pool->current_block = pool->first_block;
}

/******************************************
*name：		mp_pool_statistic
*brief:		输出当前内存池统计信息
*input:		内存池对象
*output:	无
*return:	无
******************************************/
void mp_pool_statistic(MP_POOL* pool)
{
    if(pool == NULL) return;

    int bnum = 0;	//block总数
    int currnum = 0;	//当前block编号
    MP_BLOCK* block = pool->first_block;
    while(block)
    {
        bnum++;
        if(block == pool->current_block)
            currnum = bnum;
        block = block->next;
    }

	printf("###################################################\n");
	printf("# block size: %lu\n", pool->block_size);
    printf("# block(s) num: %d\n", bnum);
    printf("# block current: %d\n", currnum);
	printf("-------------------------------\n");

    block = pool->first_block;
    bnum = 0;
    while(block)
    {
        bnum++;
        printf("#### block %d", bnum);
        if(block == pool->current_block)
            printf(" *\n");
        else
            printf("\n");
        printf("space used: %ld\n", block->start_of_rest - ((ADDR)block + sizeof(MP_BLOCK)));
        printf("space free: %lu\n", rest_block_space(block));

        int bucket_in_this_block = 0;	//当前block有多少bucket
        int bucket_in_this_block_in_use = 0;	//当前bloc有多少bucket仍被使用
        MP_BUCKET* bucket = pool->first_bucket;
        while(bucket)
        {
            MP_PIECE *piece = (MP_PIECE *)((ADDR)bucket - sizeof(MP_PIECE));
            if(piece->block == block)
            {
                bucket_in_this_block++;
                if(bucket->still_in_use)
                    bucket_in_this_block_in_use++;
            }
            bucket = bucket->next;
        }
        printf("buckets in the block: %d\n", bucket_in_this_block);
        printf("buckets in use: %d\n", bucket_in_this_block_in_use);
        printf("reference remain: %d\n", block->ref_counter);
        printf("failed time: %d\n", block->failed_time);

        block = block->next;
    }
}

