#ifndef _READFILE_H_
#define _READFILE_H_

#include<stdio.h>
#include<string.h>
#include<stdlib.h>

#define LINE_MAX_LEN	1024 	//每行内容支持最大长度

/******************************************
*name：		fileLine
*brief:		获取文件行数
*input:		file：文件
*output:	无
*return:	num：行数
******************************************/
int fileLine(FILE *file);

/******************************************
*name：		allocSpace
*brief:		文件行内容分配到堆区空间
*input:		file：文件；fileArr：文件内容二级指针
*output:	无
*return:	无
******************************************/
void allocSpace(FILE *file, char **fileArr);

/******************************************
*name：		printfLine
*brief:		测试打印每行内容
*input:		fileArr：文件内容二级指针；cnt：行数
*output:	无
*return:	无
******************************************/
void printfLine(char **fileArr, int cnt);

/******************************************
*name：		freeSpace
*brief:		释放每行内容的堆区空间
*input:		fileArr：文件内容二级指针；cnt：行数
*output:	无
*return:	无
******************************************/
void freeSpace(char **fileArr, int cnt);

#endif
