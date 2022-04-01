#ifndef __THREAD_POOL_H_
#define __THREAD_POOL_H_

#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <list>
#include <mutex>
#include <condition_variable>

typedef void* (*TaskCallback)(void* args);
class Task
{
public:
    Task(TaskCallback cb, void *args, std::string& name);
    virtual ~Task() {};
    virtual void* Run() { return m_callback(m_args); }	// 该函数由执行的线程调用运行 m_callback回调函数
    std::string& getName() {return m_name; }
private:
    TaskCallback m_callback;	// 任务函数
    void* m_args;				// 任务函数执行时传入的参数
    std::string m_name;			// 任务名字，用于识别任务
};

class ThreadPool;
class Thread
{
    friend void thread_function(Thread* t);
private:
    std::thread::id m_id;
    bool m_needToTerminate;
    ThreadPool* m_pool;
public:
    Thread(ThreadPool* tp) : m_needToTerminate(false), m_pool(tp) {}
    virtual ~Thread() {}
    void setToTerminate() { m_needToTerminate = true; } // 需要终止某个线程是调用该函数
    void setTid(std::thread::id id){ m_id = id; }
    unsigned long getTid()  // thread类给出的id是std::thread::id，不是整型，需要做转换
    {
        std::stringstream sin;
        sin << m_id;
        return std::stoul(sin.str());
    }
};

class ThreadPool
{
    friend void thread_function(Thread* t);
private:
    std::list<Task*> m_task_list;
    std::list<Thread*> m_thd_list;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    int m_max_thd_num;       // 最大允许线程数
    const int m_min_thd_num; // 最小备用数量
    int m_busy_thd_num;      // 当前处于忙状态的线程数，我们不关心具体哪个线程在忙，只关心总体上线程够不够用
    std::mutex m_notask_mutex;      // notask锁和条件变量 用于任务队列中的所有任务都已被取出但还有线程空闲时通知线程池或用户
    std::condition_variable m_notask_cond;
protected:
    virtual Thread* createAThread();
    virtual int terminateSomeThread();  // 在任务很少的时候减少一部分线程
    virtual int activeSomeThread();  // 在任务增多的时候增加一部分线程
public:
    ThreadPool(int max_thread_num = 10) : m_max_thd_num(max_thread_num), m_min_thd_num(4), m_busy_thd_num(0) {} // 可修改最小备用值
    virtual ~ThreadPool();

    virtual bool init();
    virtual int acceptATask(TaskCallback cb, void* args, std::string& taskName);
    virtual bool waitForAllRuningTaskDone();    // 提供给用户用于阻塞等待当前任务队列中的所有任务都被取走
};

void thread_function(Thread* t);

#endif