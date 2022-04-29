#include "readFile.h"

/******************************************
*name：		test
*brief:		测试文件分配到堆空间上是否正常
*input:		无
*output:	无
*return:	无
******************************************/
void test()
{
    FILE *file = fopen("./test.txt","r");
    if (file == NULL)
    {
       printf("fopen test.txt error\n");
       return;
    }
	
    int lineCnt = fileLine(file);
    printf("===========Line total：%d\n",lineCnt);
	
    char **fileArr = malloc(sizeof(char*) * lineCnt);	//文件二级指针

	allocSpace(file, fileArr);		//读文件内容并分配到堆区
    printfLine(fileArr, lineCnt);	//打印测试文件内容（此处可进行其他操作）
    freeSpace(fileArr, lineCnt);	//释放空间

	free(fileArr);	//释放文件二级指针
}

int main()
{
    test();
    return 0;
}

