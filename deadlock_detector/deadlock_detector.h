#ifndef __DEADLOCK_DET_H__
#define __DEADLOCK_DET_H__
#define _GNU_SOURCE
#include <pthread.h>

int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_unlock(pthread_mutex_t* mutex);
void init_detector();
#endif