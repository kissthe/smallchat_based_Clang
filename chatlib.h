#ifndef CHATLIB_H
#define CHATLIB_H
#include<stddef.h>
/* 封装的网络部分函数 */
int createTCPServer(int port);
int socketSetNonBlockNoDelay(int fd);
int acceptClient(int server_socket);
int TCPConnect(char *addr, int port, int nonblock);

/* 封装的分配函数 */
void *chatMalloc(size_t size);
void *chatRealloc(void *ptr, size_t size);

#endif // CHATLIB_H
