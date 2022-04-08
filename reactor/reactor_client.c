#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define MAX_CONNECTION  100000 // 单个客户端建立10W个连接
#define MAX_BUFSIZE		128
#define MAX_PORT		10	    // 最大端口数量

#define TIME_MS_USED(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)  // 用于计算耗时
struct epoll_event events[MAX_CONNECTION];

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

int main(int argc, char *argv[])
{
    if(argc < 3)
    {
        printf("Usage: %s <ip> <port>", argv[0]);
        return 0;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);
    int connections = 0;    // 建立连接的计数，用于统计
    char buffer[MAX_BUFSIZE] = {0};
    int i;
    int portOffset = 0;
    int epfd = epoll_create(1);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip);

    struct timeval loopBegin, loopEnd;


    while(1)
    {
        struct epoll_event ev;
		int sockfd = 0;

		//1、连接数量未达到最大值就一直新建连接并connect到服务端
        if(connections < MAX_CONNECTION)
        {
            sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if(sockfd < 0)
            {
                perror("socket");
                goto errExit;
            }

            addr.sin_port = htons(port + portOffset);
            portOffset = (portOffset + 1) % MAX_PORT;   // 均匀地使用这10个端口

            if(connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            {
                perror("connect");
                goto errExit;
            }

            fdSetNonBlock(sockfd);
            sockSetReuseAddr(sockfd);

			//配置加入epoll监听
            ev.events = EPOLLIN | EPOLLOUT;
            ev.data.fd = sockfd;
            epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

            connections++; // 连接数增加
        }

        //2、每增加10000个连接就执行一次
        if(connections % 10000 == 0 || connections == MAX_CONNECTION)
        {
            gettimeofday(&loopBegin, NULL);	//起始时间
			
			int nready = epoll_wait(epfd, events, 10000, 100);
			for(i = 0; i < nready; i++)
			{
				int clientfd = events[i].data.fd;

				//可读
				if (events[i].events & EPOLLIN) 
				{
					char rBuffer[MAX_BUFSIZE] = {0};				
					ssize_t length = recv(clientfd, rBuffer, MAX_BUFSIZE, 0);
					if (length > 0) 
					{
						//printf("# Recv from server: %s\n", rBuffer);	//output1打印较多
					} 
					else if (length == 0) 
					{
						printf("# Disconnected. clientfd:%d\n", clientfd);
						connections --;
						epoll_ctl(epfd, EPOLL_CTL_DEL, clientfd, &events[i]);
						close(clientfd);
					} 
					else 
					{
						if (errno == EINTR) continue;

						printf(" Error clientfd:%d, errno:%d\n", clientfd, errno);
						epoll_ctl(epfd, EPOLL_CTL_DEL, clientfd, &events[i]);
						close(clientfd);
					}
				} 
				else if (events[i].events & EPOLLOUT)	//可写
				{
					sprintf(buffer, "client data from fd %d\n", clientfd);
					send(clientfd, buffer, strlen(buffer), 0);
					//printf("# Send to server: %s\n", buffer);	//output2打印较多
				}
				else 
				{
					printf(" clientfd:%d, unknown events:%d\n", clientfd, events[i].events);
				}
			}
			
            //计算响应时间			
            gettimeofday(&loopEnd, NULL);
            int tempMs = TIME_MS_USED(loopEnd, loopBegin);
            printf("########%d client time_used:%d\n", nready, tempMs);	//output3（可关闭output1、output2查看较为清晰）
        }
    }

    return 0;

errExit:
	printf("error : %s\n", strerror(errno));
	return 0;
}