#ifndef PROJECT_A0_LIBNETFILES_H
#define PROJECT_A0_LIBNETFILES_H

#include <unistd.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#define PORT 22417
#define PORTRANGE_BEGIN 22418
#define PORTRANGE_END 22426
#define EXTRA_PORTS PORTRANGE_END - PORTRANGE_BEGIN + 1
#define MAX_MESSAGE_SIZE (1 << 11)
//#define MAX_MESSAGE_SIZE 3
#define DEFAULT_PERM_LEN 16
#define UNRESTRICTED_MODE 0x0000
#define EXCLUSIVE_MODE 0x0001
#define TRANSACTION_MODE 0x0002
#define TIMEOUT 5
#define INVALID_FILE_MODE 6

typedef struct multiplex{
    int port_n, len, i;
    char *content;
}multiplex;

typedef struct node{
    struct node *next, *prev;
    time_t sec;
    int is_turn;
}node;

int netopen(const char *pathname, int flags);

ssize_t netread(int fildes, const void *buf, size_t nbyte);

ssize_t netwrite(int fildes, const void *buf, size_t nbyte);

int netclose(int fildes);

#ifndef EXT_A
int netserverinit(char *hostname);
#endif
#ifdef EXT_A
int netserverinit(char *hostname, int filemode);
#endif

#endif //PROJECT_A0_LIBNETFILES_H
