#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/******************************************
*name：		__malloc
*brief:		分配内存，并新建文件，记录是哪个文件分配的，行号及大小
*input:		size：分配大小；file：哪个文件中执行的分配；line：哪一行
*output:	无
*return:	返回分配空间的起始地址
******************************************/
void* __malloc(size_t size, const char* file, int line) {
    void* p = malloc(size);
    if(p)
	{
        char buf[256] = {0};
        char filename[64] = {0};
        snprintf(filename, 63, "%p.mem", p);
        int len = snprintf(buf, 255, "[%s, line %d] size=%lu\n", file, line, size);

		int fd = open(filename, O_RDWR | O_CREAT, S_IRUSR);
        if(fd < 0)
		{
            printf("open %s failed in __malloc.\n", filename);
        }
        else
		{
            write(fd, buf, len);
            close(fd);
        }
    }

    return p;
}

/******************************************
*name：		__free
*brief:		释放内存，删除对应的文件
*input:		ptr：需要释放的地址；file：哪个文件中执行的释放；line：哪一行
*output:	无
*return:	返回分配空间的起始地址
******************************************/
void __free(void* ptr, const char* file, int line)
{
    char filename[64] = {0};
    snprintf(filename, 63, "%p.mem", ptr);

	if(unlink(filename) < 0)
	{
        printf("[%s, line %d] %p double free!\n", file, line, ptr);
    }

    free(ptr);
}


