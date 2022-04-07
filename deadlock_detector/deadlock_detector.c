#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "deadlock_detector.h"

struct resource_lock;

//线程顶点
struct vertex 
{
    struct vertex* next;
    pthread_t tid;        // 每个线程是一个节点 vertex，因此用线程 id 作为节点的 id
    struct resource_lock* waitting;  // waitting 不为 NULL时，指向当前正在等待释放的锁资源，通过 resource_lock 可以获取到 holder 从而构建图
    int visited;    // 遍历时用于标记是否访问过
};

//锁资源
struct resource_lock
{
    struct resource_lock* next;
    struct vertex* holder;  // 持有当前锁的 vertex
    pthread_mutex_t* lock; // 该锁资源的 mutx 地址
    int used;       // 有多少线程当前在持有该资源或等待该资源
};

//有向图
struct task_graph
{
    struct vertex vertex_list;  // 辅助节点，实际第一个节点是vertex_list.next
    struct resource_lock lock_list;// 辅助节点，实际第一个节点是lock_list.next
    pthread_mutex_t mutex;
};

typedef int (*pthread_mutex_lock_ptr)(pthread_mutex_t* mutex);
pthread_mutex_lock_ptr __pthread_mutex_lock;

typedef int (*pthread_mutex_unlock_ptr)(pthread_mutex_t* mutex);
pthread_mutex_unlock_ptr __pthread_mutex_unlock;

static struct task_graph graph;


/******************************************
*name：		add_vertex
*brief:		新增线程顶点
*input:		tid：线程的ID
*output:	无
*return:	顶点地址
******************************************/
static struct vertex* add_vertex(pthread_t tid)
{
    struct vertex* v = (struct vertex*)malloc(sizeof(struct vertex));
    if(v != NULL)
	{
        memset(v, 0, sizeof(struct vertex));
        v->tid = tid;
    }
    return v;
}

/******************************************
*name：		add_lock
*brief:		新增锁资源
*input:		mtx：锁的ID
*output:	无
*return:	锁资源地址
******************************************/
static struct resource_lock* add_lock(pthread_mutex_t* mtx)
{
    struct resource_lock* r = (struct resource_lock*)malloc(sizeof(struct resource_lock));
    if(r != NULL)
	{
        memset(r, 0, sizeof(struct resource_lock));
        r->lock = mtx;
    }
    return r;
}

/******************************************
*name：		add_relation_before_lock
*brief:		获取到锁之前，记录线程顶点请求的锁资源（即线程到锁的边）
*input:		vtx：线程顶点；lock：锁资源
*output:	无
*return:	无
******************************************/
static inline void add_relation_before_lock(struct vertex* vtx, struct resource_lock* lock)
{
    vtx->waitting = lock;   // 边
    lock->used++;
}

/******************************************
*name：		become_holder_after_lock
*brief:		获取到锁后，记录锁资源的拥有者，并删除边
*input:		vtx：线程顶点；lock：锁资源
*output:	无
*return:	无
******************************************/
static inline void become_holder_after_lock(struct vertex* vtx, struct resource_lock* lock)
{
    lock->holder = vtx;     // 成为其持有者
    vtx->waitting = NULL;
}

/******************************************
*name：		dereference_after_unlock
*brief:		释放锁后，删除锁资源的拥有者
*input:		lock：锁资源
*output:	无
*return:	无
******************************************/
static inline void dereference_after_unlock(struct resource_lock* lock)
{
    lock->used--;
    lock->holder = NULL;
}

/******************************************
*name：		get_wait_vertex
*brief:		获取某顶点正在等待的锁资源的拥有者（即某顶点正在等待的顶点）
*input:		vtx：线程顶点
*output:	无
*return:	顶点地址，没有则返回NULL
******************************************/
static inline struct vertex* get_wait_vertex(struct vertex* vtx)
{
    return vtx->waitting ? vtx->waitting->holder : NULL;
}

/******************************************
*name：		search_vertex
*brief:		查找有向图中是否已有该线程的顶点
*input:		g：有向图；tid：线程ID
*output:	无
*return:	返回该线程顶点，未找到返回NULL
******************************************/
static struct vertex* search_vertex(struct task_graph* g, pthread_t tid) 
{
    struct vertex* v = g->vertex_list.next;
    while(v)
	{
        if(tid == v->tid)
		{
            return v;
        }
        v = v->next;
    }

    return NULL;
}

/******************************************
*name：		search_lock
*brief:		查找有向图中是否已有该锁的资源
*input:		g：有向图；mtx：锁ID
*output:	无
*return:	返回该锁资源，未找到返回NULL
******************************************/
static struct resource_lock* search_lock(struct task_graph* g, pthread_mutex_t* mtx) 
{
    struct resource_lock* l = g->lock_list.next;
    while(l)
	{
        if(mtx == l->lock)
		{
            return l;
        }
        l = l->next;
    }

    return NULL;
}

/******************************************
*name：		add_vertex_to_graph
*brief:		将线程顶点加入有向图
*input:		g：有向图；vtx：线程顶点
*output:	无
*return:	无
******************************************/
static void add_vertex_to_graph(struct task_graph* g, struct vertex* vtx)
{
    struct vertex* node = g->vertex_list.next;
    struct vertex* prev = &g->vertex_list;

	while(node)
	{
        if(node == vtx)
		{
            return; // 避免重复添加
        }
        node = node->next;
        prev = prev->next;
    }
    prev->next = vtx;
}

/******************************************
*name：		add_lock_to_graph
*brief:		将锁资源加入有向图
*input:		g：有向图；lock：锁资源
*output:	无
*return:	无
******************************************/
static void add_lock_to_graph(struct task_graph* g, struct resource_lock* lock)
{
    struct resource_lock* node = g->lock_list.next;
    struct resource_lock* prev = &g->lock_list;

	while(node)
	{
        if(node == lock)
		{
            return; // 避免重复添加
        }
        node = node->next;
        prev = prev->next;
    }
    prev->next = lock;
}

/******************************************
*name：		before_lock
*brief:		实际上锁前，在有向图中建立对应顶点和资源的边
*input:		g：有向图；tid：请求线程的ID；mtx：请求锁的ID
*output:	无
*return:	无
******************************************/
static void before_lock(struct task_graph* g, pthread_t tid, pthread_mutex_t* mtx) {

    struct vertex* vtx;
    struct resource_lock* lock;

    __pthread_mutex_lock(&g->mutex);

	//1、查找该线程顶点，没有则新建
    if((vtx = search_vertex(g, tid)) == NULL) 
	{
        vtx = add_vertex(tid);
        if(vtx) {
            add_vertex_to_graph(g, vtx);
        }
        else {
            printf("add_vertex error...\n");
            assert(0);
        }
    }

	//2、查找该锁资源，没有则新建
    if((lock = search_lock(g, mtx)) == NULL) 
	{
        lock = add_lock(mtx);
        if(lock) {
            add_lock_to_graph(g, lock);
        }
        else {
            printf("add_lock error...\n");
            assert(0);
        }
    }

	//3、建立请求关系（即图的边）
    add_relation_before_lock(vtx, lock);

    __pthread_mutex_unlock(&g->mutex);
}

/******************************************
*name：		after_lock
*brief:		实际上锁成功后，删除请求边，更新锁资源的拥有者
*input:		g：有向图；tid：请求线程的ID；mtx：请求锁的ID
*output:	无
*return:	无
******************************************/
static void after_lock(struct task_graph* g, pthread_t tid, pthread_mutex_t* mtx)
{
    struct vertex* vtx;
    struct resource_lock* lock;

    __pthread_mutex_lock(&g->mutex);

    vtx = search_vertex(g, tid);
    lock = search_lock(g, mtx);

    if(vtx == NULL || lock == NULL) {
        printf("ERROR: vtx == NULL || lock == NULL in after_lock...\n");
        assert(0);
    }
	
    //正式拥有当前的锁资源
    become_holder_after_lock(vtx, lock);

    __pthread_mutex_unlock(&g->mutex);
}

/******************************************
*name：		after_unlock
*brief:		释放锁后，删除锁资源的拥有者。锁资源无人使用时删除
*input:		g：有向图；mtx：请求释放锁的ID
*output:	无
*return:	无
******************************************/
static void after_unlock(struct task_graph* g, pthread_mutex_t* mtx) {

    __pthread_mutex_lock(&g->mutex);

    struct resource_lock* lock = search_lock(g, mtx);
    if(lock == NULL){
        printf("ERROR: lock NULL in after_unlock...\n");
        assert(0);
    }

    dereference_after_unlock(lock);
    if(lock->used == 0)   // lock 没有使用者就将其释放
	{
        struct resource_lock* node = g->lock_list.next;
        struct resource_lock* prev = &g->lock_list;

		while(node != NULL)
		{
            if(node == lock)
			{
                prev->next = node->next;
                free(lock);
                break;
            }

            node = node->next;
            prev = prev->next;
        }
    }

    __pthread_mutex_unlock(&g->mutex);
}

/******************************************
*name：		pthread_mutex_lock
*brief:		封装后的上锁过程
*input:		mutex：需要上锁的ID
*output:	无
*return:	无
******************************************/
int pthread_mutex_lock(pthread_mutex_t* mutex)
{
    pthread_t tid = pthread_self();
    before_lock(&graph, tid, mutex);
    int ret = __pthread_mutex_lock(mutex);
    after_lock(&graph, tid, mutex);
    return ret;
}

/******************************************
*name：		pthread_mutex_unlock
*brief:		封装后的释放锁过程
*input:		mutex：需要释放锁的ID
*output:	无
*return:	无
******************************************/
int pthread_mutex_unlock(pthread_mutex_t* mutex)
{
    int ret = __pthread_mutex_unlock(mutex);
    after_unlock(&graph, mutex);

    return ret;
}

/******************************************
*name：		all_vertex_unvisited
*brief:		清空所有顶点访问标识
*input:		vtx：顶点头结点
*output:	无
*return:	无
******************************************/
static void all_vertex_unvisited(struct vertex* vtx)
{
    while(vtx)
	{
        vtx->visited = 0;
        vtx = vtx->next;
    }
}

/******************************************
*name：		search_cross
*brief:		判断有向图中某顶点是否有环
*input:		vtx：起始顶点
*output:	无
*return:	起始顶点
******************************************/
static struct vertex* search_cross(struct vertex* vtx)
{
    do
    {
        if(vtx->visited == 1)
		{  // 重新回到了已访问过的节点，存在环
            return vtx;
        }
		
        vtx->visited = 1;
        vtx = get_wait_vertex(vtx);
    } while(vtx);

    return NULL;    
}

/******************************************
*name：		print_cycle
*brief:		打印当前有向图中的环
*input:		cross：环起点
*output:	无
*return:	无
******************************************/
static void print_cycle(struct vertex* cross) {
    struct vertex* vtx = cross;
    char buf[32];

    do 
	{
        pthread_getname_np(vtx->tid, buf, 32);
        printf("%s ---> ", buf);
        vtx = get_wait_vertex(vtx);
    } while(vtx && vtx != cross);
	
    pthread_getname_np(vtx->tid, buf, 32);
    printf("%s\n", buf);
}

/******************************************
*name：		detector_routine
*brief:		循环检查有向图中是否有环（即是否存在死锁）
*input:		无
*output:	无
*return:	无
******************************************/
static void* detector_routine(void* arg) 
{
    struct vertex* vtx;
    while(1) 
	{
        __pthread_mutex_lock(&graph.mutex);

        vtx = graph.vertex_list.next;
        while(vtx) 
		{
            all_vertex_unvisited(graph.vertex_list.next);	//重置访问记录
            struct vertex* cross = search_cross(vtx);	//检查某顶点是否有环

			//1、存在环
			if(cross) 
			{
                print_cycle(cross);
                break;
            }

			//2、不存在环检查下一个顶点。当前已经被标记为 visited 的节点不需要重新查找
            while(vtx && vtx->visited) 
			{    
                vtx = vtx->next;
            }
        }
        
        __pthread_mutex_unlock(&graph.mutex);
        sleep(5);
    }

    return NULL;
}

/******************************************
*name：		init_detector
*brief:		初始化死锁检测，创建检测线程
*input:		无
*output:	无
*return:	无
******************************************/
void init_detector() {

    //劫持动态库中函数的入口
    __pthread_mutex_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");
	if(__pthread_mutex_lock == NULL)
	{
		printf("dlsym pthread_mutex_lock error\n");
	}
	
    __pthread_mutex_unlock = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
	if(__pthread_mutex_lock == NULL)
	{
		printf("dlsym pthread_mutex_unlock error\n");
	}

    memset(&graph, 0, sizeof(struct task_graph));
    pthread_mutex_init(&graph.mutex, NULL);

    pthread_t tid;
    pthread_create(&tid, NULL, detector_routine, NULL);
    pthread_detach(tid);

}

