#include <cstring>
#include "ThreadPool.h"

using namespace std;
#define log_error printf
#define log_warn printf
#define log_info printf

Task::Task(TaskCallback cb, void *args, string& name)
{
    m_args = args;
    m_callback = cb;
    m_name = name;
}

ThreadPool::~ThreadPool()
{
    unique_lock<mutex> lock(m_mutex);

    for(Thread* t : m_thd_list)
        t->setToTerminate();
    m_cond.notify_all();
    // 等待所有线程终止
    m_cond.wait(lock, [this]{ return m_thd_list.empty(); });
    // 清空任务队列
    for(Task* task : m_task_list)
        delete task;
    m_task_list.clear();
}

/******************************************
*name：		createAThread
*brief:		创建池中备用线程thread_function
*input:		无
*output:	无
*return:	返回绑定好线程的Thread*
******************************************/
Thread* ThreadPool::createAThread()
{
    Thread* pth = new Thread(this);
    if(pth)
    {
        thread t(thread_function, pth);
        pth->setTid(t.get_id());
        t.detach();
        m_thd_list.emplace_back(pth);
        log_info("a new thread[%lu] created. total[%lu]\n", pth->getTid(), m_thd_list.size());
    }
    return pth;
}

/******************************************
*name：		init
*brief:		线程池初始化，创建最小备用数量线程
*input:		无
*output:	无
*return:	true-成功；	false-失败；
******************************************/
bool ThreadPool::init()
{
    lock_guard<mutex> lock(m_mutex);
    for(int i = 0; i < m_min_thd_num; i++)
    {
        Thread* pth = createAThread();
        if(!pth)
            return false;
    }

    return true;
}


/******************************************
*name：		terminateSomeThread
*brief:		在比较清闲的时候将执行队列中的线程减少一半，但不小于最小备用数量
*input:		无
*output:	无
*return:	实际减少的线程数
******************************************/
int ThreadPool::terminateSomeThread()
{   // 此函数调用者加锁，内部不加锁
    int terminateNum = 0;
    if(m_task_list.empty() && m_busy_thd_num == 0)   
    {
        int needThreadNum = m_thd_list.size() >> 1; // 线程数减半
        needThreadNum = max(needThreadNum, m_min_thd_num); // 但必须大于最小备用线程数
        terminateNum = m_thd_list.size() - needThreadNum;
        int i = 0;
        for(auto it = m_thd_list.begin(); i < terminateNum && it != m_thd_list.end(); i++, it++)
        {
            Thread* t = *it;
            t->setToTerminate();    // 通知该线程终止
        }
    }
	
	//log_info("DELETE %d thread\n", terminateNum);
    return terminateNum;
}

/******************************************
*name：		activeSomeThread
*brief:		任务队列增长但没有更多线程可用时将执行队列中的线程数量扩大一半，但不大于最大允许数量
*input:		无
*output:	无
*return:	实际增加的线程数
******************************************/
int ThreadPool::activeSomeThread()
{   // 此函数调用者加锁，内部不加锁
    int needThreadNum = 0;
    if(m_busy_thd_num >= m_thd_list.size() && 
            m_task_list.size() >= (m_thd_list.size() >> 1))
    {
        needThreadNum = m_thd_list.size() << 1; // 线程数翻倍
        needThreadNum = min(needThreadNum, m_max_thd_num); // 但必须小于最大允许线程数
        needThreadNum = needThreadNum - m_thd_list.size(); // 最终实际增加的线程数

        for(int i = 0; i < needThreadNum; i++)
        {
            Thread* pth = createAThread();
            if(!pth)
                return 0;
        }
    }
			
	//log_info("ADD %d thread\n", needThreadNum);
    return needThreadNum;
}

/******************************************
*name：		acceptATask
*brief:		接收一个任务至pool中的任务链表
*input:		cb：			任务回调
			args：		任务参数
			taskName：	任务名
*output:	无
*return:	成功返回0
******************************************/
int ThreadPool::acceptATask(TaskCallback cb, void* args, string& taskName)
{
    Task *task = new Task(cb, args, taskName);
    
    lock_guard<mutex> lock(m_mutex);
    m_task_list.emplace_back(task);
    m_cond.notify_one();    // 通知任意一个就绪的线程
    log_info("ACCEPT task[%s] . now[%lu]\n", taskName.c_str(), m_task_list.size());
    return 0;
}

/******************************************
*name：		waitForAllRuningTaskDone
*brief:		等待所有任务执行完
*input:		无
*output:	无
*return:	完成后返回 true
******************************************/
bool ThreadPool::waitForAllRuningTaskDone()
{
    unique_lock<mutex> lock(m_notask_mutex);
    m_notask_cond.wait(lock);
    return true;
}

/******************************************
*name：		thread_function
*brief:		1）为该实例“争取到”任务去执行（即从任务队列中取任务实例，执行完毕后释放任务实例）；
            2）在该实例被线程池指定必须终止时，从执行队列删除该线程实例并使线程退出
            3）在适当的时机放缩线程池
*input:		Thread*
*output:	无
*return:	无
******************************************/
void thread_function(Thread* t)
{
    ThreadPool* pool = t->m_pool;
    unique_lock<mutex> lock(pool->m_mutex);

    do
    {
    	//条件变量争取任务执行
        pool->m_cond.wait(lock, [pool, t]{ return !pool->m_task_list.empty() || t->m_needToTerminate; });

		//若线程需要终止    
        if(t->m_needToTerminate) 
        {
            pool->m_thd_list.remove(t);
            log_info("thread[%lu] is terminated. now[%lu]\n", t->getTid(), pool->m_thd_list.size());
            delete t;
            if(pool->m_thd_list.size() == 0)
            {   // 所有线程都被销毁了，这只可能发生在销毁整个线程池的情况下
                pool->m_cond.notify_all(); // 通知线程池析构函数执行队列中所有线程都已销毁，可完成析构
            }
            return ; // 此处直接退出， unique_lock不需要手动解锁
        }

        /* 当前线程从任务队列中取任务 */
        Task* task = pool->m_task_list.front();
        pool->m_task_list.pop_front();
        pool->m_busy_thd_num++; // 一个线程开始忙了

        //这里判断是否需要增加更多线程
        pool->activeSomeThread();

        lock.unlock();  // 运行任务前先解锁
        log_info("RUN task[%s] was take by [%lu]. now [%lu]task left\n", 
                    task->getName().c_str(), t->getTid(), pool->m_task_list.size());
        task->Run();
        log_info("END thread[%lu] done the task [%s]\n", t->getTid(), task->getName().c_str());
        delete task;    // 任务执行完要记得释放
        lock.lock();    // 再加锁进入下一次循环等待
        pool->m_busy_thd_num--; // 有一个线程解放了，去接下一个任务

        //这里判断是否需要减少线程
        pool->terminateSomeThread();
        
        if(pool->m_task_list.empty())
        {
            pool->m_notask_cond.notify_all();// 通知用户或线程池任务队列空了
        }
    } while (true);
}