#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>


#define MAX_PORT		10
#define MAX_BUFFER_SIZE 1024
#define MAX_EVENTS_NUM (1024*1024)  // 100W个事件同时监听

static int client_cnt = 0;  // 统计连接数

struct sockitem
{
	int sockfd;
	int (*callback)(void *arg);	//回调函数
    int epfd;	// sockitem 中增加一个epfd成员以便回调函数中使用

    char recvbuffer[MAX_BUFFER_SIZE]; // 接收缓冲
	char sendbuffer[MAX_BUFFER_SIZE]; // 发送缓冲
    int recvlength; // 接收缓冲区中的数据长度
    int sendlength; // 发送缓冲区中的数据长度
};

struct reactor
{
    int epfd;
    struct epoll_event events[MAX_EVENTS_NUM];
};
struct reactor ra;  // 放到全局变量，避免大内存进入栈中

int recv_cb(void *arg);

/******************************************
*name：		fdSetNonBlock
*brief:		设置fd为非阻塞
*input:		fd：需要设置的fd
*output:	无
*return:	0：成功；-1：失败；
******************************************/
static int fdSetNonBlock(int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if(flags < 0) return flags;

    flags |= O_NONBLOCK;
    if(fcntl(fd, F_SETFL, flags) < 0) return -1;

    return 0;
}

/******************************************
*name：		sockSetReuseAddr
*brief:		设置socket SO_REUSEADDR
*input:		sockfd：需要设置的fd
*output:	无
*return:	0：成功
******************************************/
static int sockSetReuseAddr(int sockfd)
{
    int reuse = 1;
    return setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
}

/******************************************
*name：		send_cb
*brief:		发送给客户端数据。配置客户端fd的sockitem回调为recv_cb、epoll监听EPOLLIN
*input:		arg：sockitem；
*output:	无
*return:	返回写入长度
******************************************/
int send_cb(void *arg)
{
    struct sockitem *si = arg;
    struct epoll_event ev;

    int clientfd = si->sockfd;
    
    //写回的数据此处先简单处理 
    int ret = send(clientfd, si->sendbuffer, si->sendlength, 0);

	//配置sockitem
    si->callback = recv_cb;	//发送完数据切回接收

	//配置epoll监听
    ev.events = EPOLLIN;
    ev.data.ptr = si;
    epoll_ctl(si->epfd, EPOLL_CTL_MOD, si->sockfd, &ev);

    return ret;
}

/******************************************
*name：		recv_cb
*brief:		接收客户端的数据。配置客户端fd的sockitem回调为send_cb、epoll监听EPOLLOUT
*input:		arg：sockitem；
*output:	无
*return:	返回接收长度
******************************************/
int recv_cb(void *arg)
{
    struct sockitem *si = arg;
    struct epoll_event ev;

    int clientfd = si->sockfd;
    int ret = recv(clientfd, si->recvbuffer, MAX_BUFFER_SIZE, 0);

	//1、recv失败
	if(ret <= 0)
    {
        if(ret < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)	//被打断直接返回的情况
            {
                return ret;
            }
			printf("# client err... [%d]\n", --client_cnt);
        }
        else
        {
            printf("# client disconn... [%d]\n", --client_cnt);
        }
        
        //将当前客户端socket从epoll中删除
        ev.events = EPOLLIN;
        ev.data.ptr = si;
        epoll_ctl(si->epfd, EPOLL_CTL_DEL, clientfd, &ev);  
		close(clientfd);
        free(si);
    }
    else	//2、recv成功
    {
        //配置sockitem
        si->recvlength = ret;
        memcpy(si->sendbuffer, si->recvbuffer, si->recvlength);	//将接收到的数据拷贝的发送缓冲区，或其他操作
        si->sendlength = si->recvlength;
        si->callback = send_cb;	//接收完的下一步是发送数据

		//配置epoll监听
		struct epoll_event ev;
        ev.events = EPOLLOUT | EPOLLET;	//写的时候最好还是用ET
        ev.data.ptr = si;
        epoll_ctl(si->epfd, EPOLL_CTL_MOD, si->sockfd, &ev);
    }

    return ret;
}

/******************************************
*name：		accept_cb
*brief:		接收客户端的连接。配置客户端fd的sockitem回调为recv_cb、epoll监听EPOLLIN（accept也属于读IO操作的回调）
*input:		arg：sockitem；
*output:	无
*return:	返回接收的客户端 fd；失败返回       <0
******************************************/
int accept_cb(void *arg)
{
    struct sockitem *si = arg;
    struct epoll_event ev;

    struct sockaddr_in client;
    memset(&client, 0, sizeof(struct sockaddr_in));
    socklen_t caddr_len = sizeof(struct sockaddr_in);

    int clientfd = accept(si->sockfd, (struct sockaddr*)&client, &caddr_len);
    if(clientfd < 0)
    {
        printf("# accept error\n");
        return clientfd;
    }

	fdSetNonBlock(clientfd);
    sockSetReuseAddr(clientfd);

    char str[INET_ADDRSTRLEN] = {0};
    printf("Accept from %s:%d [%d]\n", inet_ntop(AF_INET, &client.sin_addr, str, sizeof(str)),
        ntohs(client.sin_port), ++client_cnt);

	//配置sockitem
    struct sockitem *client_si = (struct sockitem*)malloc(sizeof(struct sockitem));
    client_si->sockfd = clientfd;
    client_si->callback = recv_cb;  // accept完的下一步就是接收客户端数据
    client_si->epfd = si->epfd;

	//配置epoll监听
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.ptr = client_si;
    epoll_ctl(si->epfd, EPOLL_CTL_ADD, clientfd, &ev);

    return clientfd;
}

/******************************************
*name：		init_port_and_listen
*brief:		初始化listen fd。配置listen fd的sockitem回调为accept_cb、epoll监听EPOLLIN
*input:		port：绑定的端口；epfd：需要加入的epoll fd；
*output:	无
*return:	返回建立的listen fd；失败返回       <0
******************************************/
int init_port_and_listen(int port, int epfd)
{
	//创建对应port的listen fd
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
		return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if(bind(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0)
        return -2;

    if(listen(sockfd, 5) < 0)
        return -3;

    //配置sockitem
    struct sockitem *si = (struct sockitem*)malloc(sizeof(struct sockitem));    // 自定义数据，用于传递给回调函数
    si->sockfd = sockfd;
    si->callback = accept_cb;	//回调
    si->epfd = epfd; 

	//配置epoll监听
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.ptr = si;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

    return sockfd;
}

int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        return 0;
    }

    int port = atoi(argv[1]);	//server端口
    int listenfds[MAX_PORT] = {0};	//所有端口的fd
	struct sockitem *si;

    ra.epfd = epoll_create(1);	//创建epoll fd

	//1、创建10个端口listen，并且加入epoll监听
	int i;
    for(i = 0; i < MAX_PORT; i++)
    {
        listenfds[i] = init_port_and_listen(port + i, ra.epfd);
    }

    while(1)
    {
    	//2、wait事件
        int nready = epoll_wait(ra.epfd, ra.events, MAX_EVENTS_NUM, -1);
        if(nready < 0)
        {
            printf("epoll_wait error.\n");
            break;
        }

		//3、响应事件
        int i;
        for(i = 0; i < nready; i++)
        {
            si = ra.events[i].data.ptr;	//事件对应的sockitem
            if(ra.events[i].events & (EPOLLIN | EPOLLOUT))
            {
                if(si->callback != NULL)
                    si->callback(si);  // 调用回调函数
            }
        }
    }

	//close所有fd
    for(i = 0; i < MAX_PORT; i++)
    {
        if(listenfds[i] > 0)
        {
            close(listenfds[i]);
        }
    }
}
