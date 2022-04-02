#include "MemPool.h"
#include <stdio.h>

int main()
{
	//创建池对象
    MP_POOL* pool = mp_create_pool(MP_PAGE_SIZE, 1);
    if(pool == NULL)
    {
        printf("mp_create_pool failed.\n");
        return -1;
    }

    int i;

    //测试申请和释放 piece
    printf("------------ piece malloc test --------------\n");
    char* mem[10];
    for(i = 0; i < 10; i++)
        mem[i] = (char*)mp_malloc(pool, 512);
    
    mp_pool_statistic(pool);

    printf("------------ piece free test 1 --------------\n");
    for(i = 0; i < 5; i++)
        mp_free(pool, mem[i]);
    
    mp_pool_statistic(pool);
    printf("------------ piece free test 2 --------------\n");
    for(; i < 10; i++)
        mp_free(pool, mem[i]);

    mp_pool_statistic(pool);

    //测试申请和释放 bucket
    printf("------------ bucket malloc test --------------\n");
    for(i = 0; i < 4; i++)
        mem[i] = (char*)mp_malloc(pool, 8192);

    mp_pool_statistic(pool);

    printf("------------ bucket free test 1 --------------\n");
    for(i = 0; i < 2; i++)
        mp_free(pool, mem[i]);
    
    mp_pool_statistic(pool);
    printf("------------ bucket free test 2 --------------\n");
    for(; i < 4; i++)
        mp_free(pool, mem[i]);

    mp_pool_statistic(pool);

    mp_destroy_pool(pool);
    return 0;
}