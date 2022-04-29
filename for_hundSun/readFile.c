#include "readFile.h"

/******************************************
*name：		fileLine
*brief:		获取文件行数
*input:		file：文件
*output:	无
*return:	num：行数
******************************************/
int fileLine(FILE *file)
{
    if (file == NULL)
    {
    	return - 1;
    }
	
    char buf[LINE_MAX_LEN];
    int num = 0;

	//获取一行内容
    while (fgets(buf, LINE_MAX_LEN, file) != NULL)
    {
     	num++;
    }

    fseek(file, 0, SEEK_SET);
    return num;
}

/******************************************
*name：		allocSpace
*brief:		文件行内容分配到堆区空间
*input:		file：文件；fileArr：文件内容二级指针
*output:	无
*return:	无
******************************************/
void allocSpace(FILE *file, char **fileArr)
{
    if (file == NULL || fileArr == NULL)
    {
    	return;
    }

    char buf[LINE_MAX_LEN] = {0};
    int pos = 0;

	//获取一行内容并分配堆空间
    while (fgets(buf, LINE_MAX_LEN, file) != NULL)
    {           
       int curLen = strlen(buf) + 1;
       char *curPtr = malloc(sizeof(char) * curLen);
	   
       //拷贝行内容到堆区内存中，并写到二级指针
       strcpy(curPtr,buf);
       fileArr[pos]=curPtr;
	   ++pos;
       memset(buf, 0, LINE_MAX_LEN);
    }
}

/******************************************
*name：		printfLine
*brief:		测试打印每行内容
*input:		fileArr：文件内容二级指针；cnt：行数
*output:	无
*return:	无
******************************************/
void printfLine(char **fileArr, int cnt)
{
    for (int i = 0; i < cnt; i++)
    {
       printf("Line %d：%s\n",i+1, fileArr[i]);
    }
}

/******************************************
*name：		freeSpace
*brief:		释放每行内容的堆区空间
*input:		fileArr：文件内容二级指针；cnt：行数
*output:	无
*return:	无
******************************************/
void freeSpace(char **fileArr, int cnt)
{
    for (int i = 0; i < cnt; i++)
    {
       if (fileArr[i] != NULL)
       {
           free(fileArr[i]);
           fileArr[i] = NULL;
       }
    }
}

