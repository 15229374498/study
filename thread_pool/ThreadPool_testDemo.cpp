#include "ThreadPool.h"
#include <iostream>
#include <string>
using namespace std;

//测试任务参数的结构体
struct workdata
{
    int worknum;	//任务编号
    int waittime;	//任务睡眠时间
};

/******************************************
*name：		work
*brief:		测试任务，实际在线程池中运行
*input:		测试任务参数
*output:	无
*return:	无
******************************************/
void* work(void* arg)
{
    workdata* data = (workdata*)arg;
    if(!data)
	{
        return NULL;
	}
    cout << "work[id:" << data->worknum << "] run "<< data->waittime <<"s...\n";

	//用休眠来模拟不同任务函数的不同执行时间
    this_thread::sleep_for(chrono::seconds(data->waittime));  
    delete data;
    return NULL;
}

int main()
{
	//1、初始化线程池
    ThreadPool pool(20);
    if(false == pool.init())
    {
		cout << "pool init error, return";
	}

	//2、放入10个任务，每个的运行时间不一致
    for(int i = 0; i < 30 ; i++)    
    {
        workdata* data = new workdata{i + 1, i + 1};
        string name = "work" + to_string(i + 1);
        pool.acceptATask(work, data, name);
    }

	//3、等待任务队列中所有任务都被取走
    pool.waitForAllRuningTaskDone();    
    cout << "all task done.\n";
    return 0;
}