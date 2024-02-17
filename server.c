#include"chatlib.h"
#include<stdio.h>
#include<string.h>
#include<assert.h>
#include<stdlib.h>
#include<unistd.h>
#include <sys/select.h>
#define MAX_CLIENTS 1000
#define SERVER_PORT 7711

struct client {
    int fd;     // 用户套接字.
    char *nick; // 用户昵称.
};

struct chatState{
    int serversock;//服务器套接字
    int numclients;//当前用户数
    int maxclient;//最大的用户序号
    struct client*clients[MAX_CLIENTS];//存放用户指针的数组
};

struct chatState* Chat;//初始化


/* ====================== Small chat core implementation ========================
 *我们的想法很简单：接受新的连接，读取客户端发送的信息，并且
 把该信息发送给其他所有客户（自身除外）
 * =========================================================================== */

/* 当有新连接的时候创建一个绑定到fd的新客户，并且更新全局Chat信息*/

struct client*createClient(int fd){
    /*接收一个文件描述符*/
    char nick[32];
    int nicklen = snprintf(nick,sizeof(nick),"user:%d",fd);//创建初始昵称，nicklen存储的是格式化字符串的总长度（不包括结束符）
    struct client*c = chatMalloc(sizeof(*c));//动态申请内存因为会在运行中长期存在，注意这里一定先对c进行解引用
    socketSetNonBlockNoDelay(fd);//将该套接字（描述符）设置为非阻塞状态

    c->fd=fd;
    c->nick=chatMalloc(nicklen+1);
    memcpy(c->nick,nick,nicklen);

    assert(Chat->clients[c->fd]==NULL);
    Chat->clients[c->fd]=c;
    /* 更新最大的用户序号 */
    if(c->fd>Chat->maxclient)Chat->maxclient=c->fd;
    Chat->numclients++;
    return c;
}

/* 回收一个用户，并且在全局变量Chat中更新*/

void freeClient(struct client*c){
    /*
        在这里一定要完成回收操作
    */
    free(c->nick);
    close(c->fd);//关闭客户端的文件描述符
    /*更新全局变量*/
    Chat->clients[c->fd]=NULL;
    Chat->numclients--;
    if(Chat->maxclient==c->fd){
        /* 如果删除的用户的序号是当前最大的（max），那么需要进行遍历找到新的最大的 */
        int j;
        for(j=Chat->maxclient-1;j>=0;j--){
            if(Chat->maxclient!=NULL){
                Chat->maxclient=j;
                break;
            }
        }
        if(j==-1)Chat->maxclient=-1;// 没有用户了
    }
    free(c);
}

/* 分配并初始化全局变量Chat. */
void initChat(void){
    Chat = chatMalloc(sizeof(*Chat));
    memset(Chat,0,sizeof(*Chat));
    /* 开始时没有客户. */
    Chat->maxclient=-1;
    Chat->numclients=0;
    /* 创建一个套接字，服务器将会监听这个套接字，客户将会对这个套接字发送请求 */
    Chat->serversock = createTCPServer(SERVER_PORT);
    if(Chat->serversock == -1){
        perror("Creating listening socket");
        exit(1);
    }
}

/*将指定的字符串发送到所有连接的客户端，
但套接字描述符为“excluded”的客户端除外。
如果你想向每个客户端发送一些东西，
只需将excluded设置为一个不可能的套接字：-1。*/
void sendMsgToAllClientsBut(int excluded,char*s,size_t len){
    for(int j=0;j<=Chat->maxclient;j++){
        if(Chat->clients[j]==NULL||
        Chat->clients[j]->fd==excluded)continue;

        /* Important: we don't do ANY BUFFERING. We just use the kernel
         * socket buffers. If the content does not fit, we don't care.
         * This is needed in order to keep this program simple. */
        /*即这里没有考虑缓冲策略而是使用了内核的套接字缓冲区，
        通常我们都会在这里做一些缓冲，但是为了程序的简洁这里没有考虑。*/

        write(Chat->clients[j]->fd,s,len);//往描述符中写内容
        /*
            write 函数写入数据时，数据实际上会被放入这个缓冲区，
            然后由内核异步地将数据发送到目标。
            套接字缓冲区的大小取决于操作系统和网络配置。
            但如果消息超过了缓冲区的大小，可能会发生部分消息被截断或者丢失的情况
        */
    }
}

/* main函数实现了主要的逻辑:
 * 1. 接收客户的连接
 * 2. 检查是否有客户发送消息
 * 3. 将消息转发给其他客户*/

int main(void){
    initChat();//初始化全局变量

    while(1){
        fd_set readfds;
        struct timeval tv;
        int retval;

        FD_ZERO(&readfds);//先清空文件描述符集合
        /* When we want to be notified by select() that there is
         * activity? If the listening socket has pending clients to accept
         * or if any other client wrote anything. */

        /*FD_SET会将指定的文件描述符加入到文件描述符集合中*/

        FD_SET(Chat->serversock,&readfds);//首先将服务器的套接字加入

        for(int j=0;j<=Chat->maxclient;j++){
            if(Chat->clients[j])FD_SET(j,&readfds);
        }//再将客户的文件描述符加入

        /* 为select设置一个等待时间*/
        tv.tv_sec =1;//1秒的超时时间
        tv.tv_usec = 0;

        /* Select需要使用的最大文件描述符加一作为第一个参数。
        它可以是我们的一个客户端，也可以是服务器套接字本身。*/

        int maxfd = Chat->maxclient;//刚开始是-1
        if(maxfd<Chat->serversock)maxfd=Chat->serversock;//总之要找到包括服务器fd在内的最大值；
        retval = select(maxfd+1,&readfds,NULL,NULL,&tv);//查看readfds中哪些描述符已经准备好读取
        if(retval==-1){
            perror("select() error");
            exit(1);
        }else if(retval){
            /* 如果侦听套接字是“可读的”，则实际上意味着有新的客户端连接等待接受。*/
            if(FD_ISSET(Chat->serversock,&readfds)){
                /*若是服务器的套接字可读，说明有新的连接请求，创建一个新的Client。*/
                int fd = acceptClient(Chat->serversock);//获得一个与服务器链接的新的套接字
                struct client*c = createClient(fd);//将一个新客户和套接字连接起来
                /* 发送欢迎信息. */
                char*welcome_msg=
                    "Welcome to Simple Chat!"
                    "Use /nick <nick> to set your nick.\n";
                write(c->fd,welcome_msg,strlen(welcome_msg));
                /*
                    疑问：通过这个函数如何写到对应的控制台？
                */
                printf("Connected client fd=%d\n",fd);
            }
            /* 对于每个连接服务器的用户，检查是否有消息准备发送给我们*/
            char readbuf[256];//用来存储消息
            for(int j=0;j<=Chat->maxclient;j++){
                if(Chat->clients[j]==NULL)continue;
                if(FD_ISSET(j,&readfds)){
                    /* Here we just hope that there is a well formed
                     * message waiting for us. But it is entirely possible
                     * that we read just half a message. In a normal program
                     * that is not designed to be that simple, we should try
                     * to buffer reads until the end-of-the-line is reached. */
                    /*
                        我们想要处理完整的消息，但是在现实中由于网络是分组传输，
                        因此最好在这里引入缓冲的机制，等待完整的一句话后再继续进行；
                    */
                    int nread = read(j,readbuf,sizeof(readbuf)-1);//从客户端套接字读取消息（假设消息是完整的）

                    if(nread<=0){
                        /* Error or short read means that the socket
                         * was closed. */
                        /*错误或短读取标识套接字已经关闭*/
                        printf("Disconnected client fd=%d, nick=%s\n",
                        j,Chat->clients[j]->nick);
                        freeClient(Chat->clients[j]);
                    }else{
                        /* 客户端套接字中有内容，需要将消息转发给其他所有客户端 */
                        struct client*c =Chat->clients[j];
                        readbuf[nread]=0;//设置一个斜杠0
                        /* If the user message starts with "/", we
                         * process it as a client command. So far
                         * only the /nick <newnick> command is implemented. */

                        if(readbuf[0]=='/'){
                            /*如果消息以'/'开头说明客户发送的是一个命令*/
                            char*p;
                            p=strchr(readbuf,'\r');if(p)*p=0;
                            p=strchr(readbuf,'\n');if(p)*p=0;//去除换行符
                            /* 检查空格后命令的参数*/
                            char*arg=strchr(readbuf,' ');
                            if(arg){
                                *arg=0;/* 把readbuf给切开，用0分割，后面cmp的时候有用 */
                                arg++;/* 参数在空格后的一个字节 */
                            
                            }
                            if(!strcmp(readbuf,"/nick")&&arg){
                                /*
                                    客户端昵称更新。
                                */
                                free(c->nick);
                                int nicklen=strlen(arg);
                                c->nick=chatMalloc(nicklen+1);
                                memcpy(c->nick,arg,nicklen+1);
                            }else{
                                /* 不支持的命令，发送一个错误 */
                                char*errmsg = "Unsupported command\n";
                                write(c->fd,errmsg,strlen(errmsg)); 
                            }
                        } else{
                            /*客户发送的是信息而非命令*/
                            char msg[256];
                            int msglen = snprintf(msg,sizeof(msg),
                            "%s> %s",c->nick,readbuf);
                            /*msglen可能长度大于256，那么我们在这里就只能够截断消息了 */
                            if(msglen>=(int)sizeof(msg))
                                msglen=sizeof(msg)-1;
                            printf("%s",msg);
                            /* 转发给其他用户 */
                            sendMsgToAllClientsBut(j,msg,msglen);


                        }
                    }


                }
            }
        }else{
           /* Timeout occurred. We don't do anything right now, but in
             * general this section can be used to wakeup periodically
             * even if there is no clients activity. */ 
            /*
            发生超时。我们现在不做任何事情，但通常情况下，
            即使没有客户端活动，也可以使用此部分定期唤醒。
            */
        }

    }


    return 0;
}
