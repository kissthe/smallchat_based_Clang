#define _POSIX_C_SOURCE 200112L

#include"chatlib.h"
#include<netinet/in.h>
#include<fcntl.h>
#include<arpa/inet.h>
#include <netinet/tcp.h>
#include<errno.h>
#include<stdlib.h>
#include<stdio.h>
#include<netdb.h>
#include<string.h>
#include<unistd.h>




int createTCPServer(int port){
    /*port是传入的端口号*/
    int s,yes=1;
    struct sockaddr_in sa;//该结构体存放socket地址信息

// 创建TCP socket
    if((s=socket(AF_INET,SOCK_STREAM,0))==-1)return -1;//IPV4,TCP
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));//允许地址重用
 // 初始化sockaddr_in结构体，设置地址族、端口和IP地址
    memset(&sa,0,sizeof(sa));//将该内存地址初始化为0
    sa.sin_family = AF_INET;
    sa.sin_port=htons(port);
    sa.sin_addr.s_addr=htonl(INADDR_ANY);
// 将socket绑定到指定的地址和端口，并开始监听连接
    if(bind(s,(struct sockaddr*)&sa,sizeof(sa))==-1||listen(s,511)==-1)
    {
        close(s);//若绑定或者监听失败，则关闭socket
        return -1;
    }
    return s;

}

/* 将指定的套接字设置为非阻塞模式（non-blocking mode）以减少延迟，
并启用 TCP No Delay（禁用 Nagle 算法）。
Nagle算法会将小的数据块组合成较大的数据块儿来减少网络上的小数据包数量 */
int socketSetNonBlockNoDelay(int fd) {
    int flags, yes = 1;

    /* 设置套接字不阻塞。
     * 注意F_GETFL和F_SETFL的fcntl（2）不能被信号中断。
      */
    if ((flags = fcntl(fd, F_GETFL)) == -1) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;

    /* This is best-effort. No need to check for errors. */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return 0;
}

/*接受新的客户端连接，如果连接成功，
则返回新的客户端套接字，如果出现错误则返回 -1 */
int acceptClient(int server_socket){
    /*接收服务器套接字，并返回一个客户端套接字*/
    int s;//存储新的客户端套接字

    while (1)
    {
        struct sockaddr_in sa;//存储客户端地址信息
        socklen_t slen = sizeof(sa);
        //accept调用接受客户端连接
        s = accept(server_socket,(struct sockaddr*)&sa,&slen);
        if(s==-1){
            if(errno==4)continue;/* 如果是被信号中断，则尝试再次接受连接，这里的4代表的是“ENTER” */
            else return -1;// 如果不是被信号中断，则返回 -1 表示出现错误。
        }
        break;// 如果成功接受连接，跳出循环。
    }
    return s;
    
}


/* 我们还定义了一个总是在内存不足时崩溃的分配器*/
void *chatMalloc(size_t size){
    void*ptr=malloc(size);
    if(ptr == NULL){
        perror("Out of memory");
        exit(1);
    }
    return ptr;
}

void *chatRealloc(void*ptr,size_t size){
    ptr = realloc(ptr,size);
    if(ptr == NULL){
        perror("Out of memory");
        exit(1);
    }
    return ptr;
}

/* Create a TCP socket and connect it to the specified address.
 * On success the socket descriptor is returned, otherwise -1.
 *
 * If 'nonblock' is non-zero, the socket is put in nonblocking state
 * and the connect() attempt will not block as well, but the socket
 * may not be immediately ready for writing. */
int TCPConnect(char*addr,int port,int nonblock){
    /*相当于封装了一个connect函数*/
    int s,retval=-1;
    
    struct addrinfo *servinfo,*p;
    struct addrinfo hints;

    char portstr[6];
    snprintf(portstr,sizeof(portstr),"%d",port);
    /*设置一下*/
    memset(&hints,0,sizeof(hints));
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;

    if(getaddrinfo(addr,portstr,&hints,&servinfo)!=0)return -1;//将服务名翻译成一组套接字地址，并存放在serveinfo中
    //servinfo是一个链表指针
    //之所以将结果存放在一个链表中，是因为一次域名解析可能对应多个IP地址。

    for(p=servinfo;p!=NULL;p=p->ai_next){
        /* Try to create the socket and to connect it.
         * If we fail in the socket() call, or on connect(), we retry with
         * the next entry in servinfo. */
        /*尝试创建套接字并连接它。
        如果我们在 socket（） 调用或 connect（） 中失败，我们会使用 servinfo 中的下一个条目重试。*/
        if((s=socket(p->ai_family,p->ai_socktype,p->ai_protocol))==-1)
        continue;

        /* 设置为非阻塞模式 */
        if(nonblock&&socketSetNonBlockNoDelay(s)==-1){
            close(s);
            break;
        }
        /* 尝试链接. */
        if(connect(s,p->ai_addr,p->ai_addrlen)==-1){
             /* 如果服务器端口被设置为非阻塞状态，那么这里返回EINPROGRESS表示正在链接 */
            if(errno==EINPROGRESS&&nonblock)return s;
            /* 否则的话就是错误 */
            close(s);
            break;
        }
        /* 如果没有错误的话，表明我们成功连接了一个socket，可以返回了 */
        retval = s;
        break;
    }
    
    freeaddrinfo(servinfo);
    /*虽然 servinfo 是一个指向链表的指针，但是 getaddrinfo 在内部动态分配了内存来存储地址信息链表的内容。
    因此，通过 freeaddrinfo(servinfo) 来释放这些动态分配的内存，防止内存泄漏。*/
    return retval;/*如果没有连接成功的话会返回-1 */

}
