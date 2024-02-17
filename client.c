#include "chatlib.h"
#include<stdio.h>
#include<stdlib.h>
#include <sys/select.h>
#include <unistd.h>
#include<termios.h>
#include<errno.h>


/*为了解决输入的时候接收到其他客户的消息的问题，需要使用其他方法来进行解决*/
/*当程序在终端运行时，终端通常处于"行缓冲模式"，这意味着输入会被缓冲直到用户按下Enter键。
在某些情况下，我们可能希望程序能够立即处理每个按键，而不是等待用户按下Enter键。
这就是所谓的"原始模式"（raw mode）。*/

void disableRawModeAtExit(void);


int setRawMode(int fd,int enable){
    /*有几个静态（static）变量用于保持一些全局状态，
    确保在整个程序运行过程中能够正确设置和还原终端的状态。*/
    static struct termios orig_termios;//存放原来的终端状态,这个结构体里面很多终端属性;
    static int atexit_registered=0;//避免多次注册atexit()函数
    static int rawmode_is_set=0;//如果用raw mode那么这个变量就是true
    struct termios raw;//存储修改后的终端属性

    /*如果enable是0，如果当前处于raw mode 我们得取消他*/
    if(enable==0){ 
        /*
        tcsetattr 被用来将修改后的终端属性应用到终端，
        实现原始模式的配置。TCSAFLUSH选项会刷新输入和输出缓冲区，确保之前的输入和输出不影响新的属性设置。如果设置成功，tcsetattr 返回0，否则返回-1，
        并在全局变量 errno 中设置相应的错误码。*/
        if(rawmode_is_set&&tcsetattr(fd,TCSAFLUSH,&orig_termios)!=-1){
            rawmode_is_set=0;
        }
        return 0;
    }

    /*启用raw mode*/
    if (!isatty(fd)) goto fatal;//检查文件描述符是否关联到终端。如果不是，则跳转到fatal标签，即错误处理。
    /*如果atexit_registered为0，表示还没有注册过atexit函数，那么就注册disableRawModeAtExit函数。
    这样可以确保在程序退出时能够调用相应的函数来恢复终端状态。*/
    if (!atexit_registered) {
        atexit(disableRawModeAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd, &orig_termios) == -1) goto fatal;//获取当前终端属性，并将其保存在orig_termios中。如果获取失败，跳转到fatal标签，即错误处理。

    raw = orig_termios;//将raw结构体初始化为orig_termios的副本，以便对其进行修改
    /*修改终端属性，将终端配置为原始模式。原始模式允许程序直接处理输入字符，而不会有一些默认的终端行为，
    比如输入缓冲、回显等。*/
    
    raw.c_iflag&=~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);//修改终端属性，将终端配置为原始模式。原始模式允许程序直接处理输入字符，而不会有一些默认的终端行为，比如输入缓冲、回显等。
    /* output modes - do nothing. We want post processing enabled so that
     * \n will be automatically translated to \r\n. */
    // raw.c_oflag &= ...
    /* control modes - set 8 bit chars */

    /*设置了控制模式，通过按位或运算将 CS8 标志位置为1。CS8 表示使用8位字符。*/
    raw.c_cflag |= (CS8);

    /*这一部分设置本地模式，通过按位与运算清除了一些标志位：

    ECHO: 禁用回显（不显示输入字符）。
    ICANON: 禁用规范模式，即禁用行缓冲，每次输入的字符都立即传递给程序。
    IEXTEN: 禁用扩展功能，这包括非标准输入处理，例如Ctrl-V。
    */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    /*这一部分设置控制字符，指定了read函数的返回条件。
    在这里，VMIN 设置为1表示至少读取一个字节，VTIME 设置为0表示不使用超时*/
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    /*TCSAFLUSH标志表示在应用更改之前刷新输入输出缓冲区。*/
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    rawmode_is_set = 1;
    return 0;

fatal:
/*将errno设置为ENOTTY，表示不是一个合法的终端，
然后返回-1，表示操作失败。*/
    errno=ENOTTY;
    return -1;
}

/*结束时要把客户端还原到原来的模式*/

void disableRawModeAtExit(void){
    setRawMode(STDIN_FILENO,0);
}

void terminalCleanCurrentLine(void){
    write(fileno(stdout),"\e[2K",4);
}

void terminalCursorAtLineStart(void){
    write(fileno(stdout),"\r",1);
}

#define IB_MAX 128
struct InputBuffer
{
    char buf[IB_MAX];//缓冲区
    int len;//当前长度
};

/*inputBuffer*()返回以下几种参数：*/
#define IB_ERR 0 //错误
#define IB_OK 1 //接受新字符，执行操作
#define IB_GOTLINE 2 //有新的一行到来

/*把新字符追加到缓冲区中*/
int inputBufferAppend(struct InputBuffer* ib,int c){
    if(ib->len>=IB_MAX)return IB_ERR;//空间不够了

    ib->buf[ib->len]=c;
    ib->len++;
    return IB_OK;
}

int inputBufferFeedChar(struct InputBuffer*ib,int c){
    switch(c) {
    case '\n':
        break;          // Ignored. We handle \r instead.
    case '\r':
        return IB_GOTLINE;
    case 127:           // Backspace.
        if (ib->len > 0) {
            ib->len--;
            inputBufferHide(ib);
            inputBufferShow(ib);
        }
        break;
    default:
        if (inputBufferAppend(ib,c) == IB_OK)
            write(fileno(stdout),ib->buf+ib->len-1,1);
        break;
    }
    return IB_OK;
}

/* 隐藏用户正在键入的行. */
void inputBufferHide(struct InputBuffer *ib) {
    (void)ib; // Not used var, but is conceptually part of the API.
    terminalCleanCurrentLine();
    terminalCursorAtLineStart();
}

/* 展示当前行. Usually called after InputBufferHide(). */
void inputBufferShow(struct InputBuffer *ib) {
    write(fileno(stdout),ib->buf,ib->len);
}

/* 清空缓冲区. */
void inputBufferClear(struct InputBuffer *ib) {
    ib->len = 0;
    inputBufferHide(ib);
}


int main(int argc,char**argv){
    if(argc!=3){
        printf("Usage: %s <host> <port>\n",argv[0]);
        exit(1);
    }
    
    /*与服务器建立连接*/
    int s = TCPConnect(argv[1],atoi(argv[2]),0);
    if(s==-1){
        perror("Connection to server");
        exit(1);
    }
    /* Put the terminal in raw mode: this way we will receive every
     * single key stroke as soon as the user types it. No buffering
     * nor translation of escape sequences of any kind. */
    setRawMode(fileno(stdin),1);

    /*等待标准输入或服务器套接字的数据*/
    fd_set readfds;
    int stdin_fd = fileno(stdin);

    struct InputBuffer ib;
    inputBufferClear(&ib);

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(s,&readfds);
        FD_SET(stdin_fd,&readfds);
        int maxfd = s>stdin_fd?s:stdin_fd;

        int num_events = select(maxfd+1,&readfds,NULL,NULL,NULL);
        if(num_events==-1){
            perror("select() error");
            exit(1);
        }else if (num_events)
        {
            char buf[128];//缓冲区
            if (FD_ISSET(s,&readfds))
            {
                /* 来自服务器的数据？ */
                ssize_t count = read(s,buf,sizeof(buf));
                if(count<=0){
                    printf("Connection lost\n");
                    exit(1);
                }
                inputBufferHide(&ib);
                write(fileno(stdout),buf,count);
                inputBufferShow(&ib);
            }else if (FD_ISSET(stdin_fd,&readfds))
            {
                /* 来自键盘输入的数据？ */
                ssize_t count = read(stdin_fd,buf,sizeof(buf));
                for (int j = 0; j < count; j++) {
                    int res = inputBufferFeedChar(&ib,buf[j]);
                    switch(res) {
                    case IB_GOTLINE:
                        inputBufferAppend(&ib,'\n');
                        inputBufferHide(&ib);
                        write(fileno(stdout),"you> ", 5);
                        write(fileno(stdout),ib.buf,ib.len);
                        write(s,ib.buf,ib.len);
                        inputBufferClear(&ib);
                        break;
                    case IB_OK:
                        break;
                    }
                }
            }
                
        }
        
    }
    close(s);
    return 0;
    
}