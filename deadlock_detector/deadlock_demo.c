#include "deadlock_detector.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>


pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex3 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex4 = PTHREAD_MUTEX_INITIALIZER;

/******************************************
*name：		thread1
*brief:		thread1启动时获取mutex1，睡眠1秒后请求mutex2
*input:		无
*output:	无
*return:	无
******************************************/
void* thread1(void* arg) 
{
    pthread_setname_np(pthread_self(), "thread1");
    printf("thread1:%lu start.\n", pthread_self());
    pthread_mutex_lock(&mutex1);
    sleep(1);
    pthread_mutex_lock(&mutex2);

    pthread_mutex_unlock(&mutex2);
    pthread_mutex_unlock(&mutex1);
    printf("thread1:%lu end.\n", pthread_self());
    return NULL;
}

/******************************************
*name：		thread2
*brief:		thread2启动时获取mutex2，睡眠1秒后请求mutex3
*input:		无
*output:	无
*return:	无
******************************************/
void* thread2(void* arg) 
{
    pthread_setname_np(pthread_self(), "thread2");
    printf("thread2:%lu start.\n", pthread_self());
    pthread_mutex_lock(&mutex2);
    sleep(1);
    pthread_mutex_lock(&mutex3);

    pthread_mutex_unlock(&mutex3);
    pthread_mutex_unlock(&mutex2);
    printf("thread2:%lu end.\n", pthread_self());
    return NULL;
}

/******************************************
*name：		thread3
*brief:		thread3启动时获取mutex3，睡眠1秒后请求mutex4
*input:		无
*output:	无
*return:	无
******************************************/
void* thread3(void* arg) 
{
    pthread_setname_np(pthread_self(), "thread3");
    printf("thread3:%lu start.\n", pthread_self());
    pthread_mutex_lock(&mutex3);
    sleep(1);
    pthread_mutex_lock(&mutex4);

    pthread_mutex_unlock(&mutex4);
    pthread_mutex_unlock(&mutex3);
    printf("thread3:%lu end.\n", pthread_self());
    return NULL;
}

/******************************************
*name：		thread4
*brief:		thread4启动时获取mutex4，睡眠1秒后请求mutex1
*input:		无
*output:	无
*return:	无
******************************************/
void* thread4(void* arg) 
{
    pthread_setname_np(pthread_self(), "thread4");
    printf("thread4:%lu start.\n", pthread_self());
    pthread_mutex_lock(&mutex4);
    sleep(1);
    pthread_mutex_lock(&mutex1);

    pthread_mutex_unlock(&mutex1);
    pthread_mutex_unlock(&mutex4);
    printf("thread4:%lu end.\n", pthread_self());
    return NULL;
}


int main() 
{
    init_detector();    // 初始化检测组件

    pthread_t tid1, tid2, tid3, tid4;
    pthread_create(&tid1, NULL, thread1, NULL);
    pthread_create(&tid2, NULL, thread2, NULL);
    pthread_create(&tid3, NULL, thread3, NULL);
    pthread_create(&tid4, NULL, thread4, NULL);

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    pthread_join(tid3, NULL);
    pthread_join(tid4, NULL);
	
    return 0;
}