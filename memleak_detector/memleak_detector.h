#ifndef __MEMLEAK_DET_H__
#define __MEMLEAK_DET_H__
#include <unistd.h>

void* __malloc(size_t size, const char* file, int line);
void __free(void* ptr, const char* file, int line);

#ifdef __MALLOC_DEBUG
#define malloc(size) __malloc(size, __FILE__, __LINE__)
#define free(ptr) __free(ptr, __FILE__, __LINE__)
#endif

#endif