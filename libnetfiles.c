#include "libnetfiles.h"

int ID = -1;
int ID_n = htonl(-1);
struct sockaddr_in server;

int conn(){
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;

    if (connect(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
        return -1;
    return sockfd;
}

int initial_check(int fildes){
    if (ID < 0) {
        h_errno = HOST_NOT_FOUND;
        return -1;
    }

    if (fildes == -1) {
        fprintf(stderr, "File descriptor should never be -1\n");
        return -1;
    }

    return 0;
}

multiplex *multiplex_creator(int port_n, int len, int i, char *t_content, int indicator){
    multiplex *val = calloc(1, sizeof(multiplex));
    val->port_n = port_n;
    val->len = len;
    val->i = i;
    if (indicator){
        val->content = calloc(len, sizeof(char));
        memcpy(val->content, t_content, len);
    }
    return val;
}

void *ext_b_receiver(void *arg){
    multiplex *args = (multiplex *)arg;
    struct sockaddr_in tmp = server;
    tmp.sin_port = htons(args->port_n);

    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        pthread_exit((void *) EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&tmp, sizeof(tmp)) < 0){
        perror("connect error");
        pthread_exit((void *) EXIT_FAILURE);
    }

//    args->content = calloc(args->len, sizeof(char));
//    char *buf = calloc(args->len, sizeof(char));
    args->content = calloc(args->len, sizeof(char));
    read(sockfd, args->content, args->len);
//    memcpy(args->content, buf, args->len);

    if (close(sockfd) == -1){
        perror("close error");
        pthread_exit((void *) EXIT_FAILURE);
    }

    pthread_exit((void *) EXIT_SUCCESS);
}

void *ext_b_sender(void *arg){
    multiplex args = *(multiplex *)arg;
    struct sockaddr_in tmp = server;
    tmp.sin_port = htons(args.port_n);

    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket error");
        pthread_exit((void *) EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&tmp, sizeof(tmp)) < 0){
        perror("connect error");
        pthread_exit((void *) EXIT_FAILURE);
    }
#ifdef DEBUG
    printf("thread connect\n");
#endif
    send(sockfd, args.content, args.len, 0);

    if (close(sockfd) == -1){
        perror("close error");
        pthread_exit((void *) EXIT_FAILURE);
    }

    pthread_exit((void *) EXIT_SUCCESS);
}

int netopen(const char *pathname, int flags){
    if (ID < 0) {
        h_errno = HOST_NOT_FOUND;
        return -1;
    }

    int sockfd = conn();

    if (flags > 0x0002 || flags < 0x0000) {
        errno = EINVAL;
        return -1;
    }
    // Send Client ID
    send(sockfd, &ID_n, sizeof(ID_n), 0);

    // Send mode
    char mode = 'o';
    send(sockfd, &mode, 1, 0);

    // Send flags
    int tmp = htonl(flags);
    send(sockfd, &tmp, sizeof(tmp), 0);

    // Send path length
    unsigned long len = strlen(pathname);
    len = htonl(len);
    send(sockfd, &len, sizeof(len), 0);

    // Send path
    send(sockfd, pathname, strlen(pathname), 0);

    // Receive neg_fd
    int fd = 0;
    read(sockfd, &fd, sizeof(fd));
    fd = ntohl(fd);

    if (fd == -1){
        // Receive errno
        errno_t err = 0;
        int herr;
        read(sockfd, &err, sizeof(err));
        read(sockfd, &herr, sizeof(herr));
        errno = ntohl(err);
        h_errno = ntohl(herr);
        fd = -1;
    }

    return fd;
}

ssize_t netread(int fildes, const void *buf, size_t nbyte){
    if (initial_check(fildes) < 0)
        return -1;

    int sockfd = conn();

    // Send Client ID
    send(sockfd, &ID_n, sizeof(ID_n), 0);

    // Send mode
    char mode = 'r';
    send(sockfd, &mode, sizeof(mode), 0);

    // Send neg_fd
    int tmp = htonl(fildes);
    send(sockfd, &tmp, sizeof(tmp), 0);

    // Send nbyte
    tmp = htonl(nbyte);
    send(sockfd, &tmp, sizeof(tmp), 0);

    // Receive nbyte
    read(sockfd, &tmp, sizeof(tmp));
    int cnt = ntohl(tmp);

    if (cnt == -1){
        // Receive errno
        errno_t err = 0;
        int herr;
        read(sockfd, &err, sizeof(err));
        read(sockfd, &herr, sizeof(herr));
        errno = ntohl(err);
        h_errno = ntohl(herr);
    } else {
        char *content = calloc(cnt + 1, sizeof(char));
#ifdef EXT_B
        // Receive num of segments
        int n_seg;
        read(sockfd, &n_seg, sizeof(n_seg));
        n_seg = ntohl(n_seg);

        // Receive segment length
        int seg_l;
        read(sockfd, &seg_l, sizeof(seg_l));
        seg_l = ntohl(seg_l);

        pthread_t *pid;
        multiplex **ms;
        int *tmp_ports;

        if (n_seg) {
            // Receive n_seg port numbers
            tmp_ports = calloc(n_seg, sizeof(int));
            for (int i = 0; i < n_seg; ++i) {
                read(sockfd, tmp_ports+i, sizeof(int));
                tmp_ports[i] = ntohl(tmp_ports[i]);
            }
        }

        // Read first segment
        read(sockfd, content, cnt - seg_l * n_seg);

        // Receive n_seg by worker thread
        pid = calloc(n_seg, sizeof(pthread_t));
        ms = calloc(n_seg, sizeof(multiplex));
        for (int i = 0; i < n_seg; ++i) {
            ms[i] = multiplex_creator(tmp_ports[i], seg_l, i, NULL, 0);
            pthread_create(pid + i, NULL, ext_b_receiver, ms[i]);
//            ext_b_receiver(&m);
        }

        for (int i = 0; i < n_seg; ++i) {
            void *result;
            pthread_join(pid[i], &result);
            memcpy(content + (cnt - seg_l * n_seg) + (n_seg - ms[i]->i - 1) * seg_l, ms[i]->content, ms[i]->len);
        }
#else
        read(sockfd, content, cnt);
#endif
        memcpy((char *)buf, content, cnt);
    }

    return cnt;
}

ssize_t netwrite(int fildes, const void *buf, size_t nbyte){
    if (initial_check(fildes) < 0)
        return -1;

    int sockfd = conn();

    // Send Client ID
    send(sockfd, &ID_n, sizeof(ID_n), 0);

    // Send mode
    char mode = 'w';
    send(sockfd, &mode, sizeof(mode), 0);

    // Send neg_fd
    int tmp = htonl(fildes);
    send(sockfd, &tmp, sizeof(tmp), 0);

    // Send nbyte
    tmp = htonl(nbyte);
    send(sockfd, &tmp, sizeof(tmp), 0);
#ifdef EXT_B
    // Receive num of segments
    int n_seg;
    read(sockfd, &n_seg, sizeof(n_seg));
    n_seg = ntohl(n_seg);

    if (n_seg == -1){
        // Receive errno
        errno_t err = 0;
        int herr;
        read(sockfd, &err, sizeof(err));
        read(sockfd, &herr, sizeof(herr));
        errno = ntohl(err);
        h_errno = ntohl(herr);
        return -1;
    }

    // Receive segment length
    int seg_l;
    read(sockfd, &seg_l, sizeof(seg_l));
    seg_l = ntohl(seg_l);

    pthread_t *pid;
    multiplex **ms;
    int *tmp_ports;

    char *content = calloc(nbyte, sizeof(char));
    strncpy(content, buf, nbyte);

    if (n_seg) {
        // Receive n_seg port numbers
        tmp_ports = calloc(n_seg, sizeof(int));
        for (int i = 0; i < n_seg; ++i) {
            read(sockfd, tmp_ports+i, sizeof(int));
            tmp_ports[i] = ntohl(tmp_ports[i]);
        }

        // Send n_seg by worker thread
        pid = calloc(n_seg, sizeof(pthread_t));
        ms = calloc(n_seg, sizeof(multiplex*));
        char *t_ptr = content + nbyte;
        for (int i = 0; i < n_seg; ++i) {
            char *t_content = calloc(seg_l, sizeof(char));
            t_ptr -= seg_l;
            memcpy(t_content, t_ptr, seg_l);
            ms[i] = multiplex_creator(tmp_ports[i], seg_l, i, t_content, 1);
            pthread_create(pid + i, NULL, ext_b_sender, ms[i]);
//                            ext_b_sender(&m);
        }
    }

    // Send first segment
    send(sockfd, content, nbyte - seg_l * n_seg, 0);
    for (int i = 0; i < n_seg; ++i) {
        void *result;
        pthread_join(pid[i], &result);
    }
#else
    send(sockfd, buf, nbyte, 0);
#endif
    // Receive nbyte
    read(sockfd, &tmp, sizeof(tmp));
    int cnt = ntohl(tmp);
    if (cnt == -1){
        // Receive errno
        errno_t err = 0;
        int herr;
        read(sockfd, &err, sizeof(err));
        read(sockfd, &herr, sizeof(herr));
        errno = ntohl(err);
        h_errno = ntohl(herr);
    }

    return cnt;
}

int netclose(int fildes){
    if (initial_check(fildes) < 0)
        return -1;

    int sockfd = conn();

    // Send Client ID
    send(sockfd, &ID_n, sizeof(ID_n), 0);

    // Send mode
    char mode = 'c';
    send(sockfd, &mode, sizeof(mode), 0);

    // Send neg_fd
    int tmp = htonl(fildes);
    send(sockfd, &tmp, sizeof(tmp), 0);

    // Receive return value
    read(sockfd, &tmp, sizeof(tmp));
    int result = ntohl(tmp);
    if (result == -1) {
        // Receive errno
        errno_t err = 0;
        int herr;
        read(sockfd, &err, sizeof(err));
        read(sockfd, &herr, sizeof(herr));
        errno = ntohl(err);
        h_errno = ntohl(herr);
    }
    return result;
}

#ifndef EXT_A
int netserverinit(char *hostname){
    struct hostent *hostnm = gethostbyname(hostname);
    if (hostnm == (struct hostent *) 0) {
        h_errno = HOST_NOT_FOUND;
        return -1;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = *((unsigned long *)hostnm->h_addr);

    int sockfd = conn();

    if (sockfd == -1)
        return -1;

    // Send Client ID
    send(sockfd, &ID_n, sizeof(ID_n), 0);

    // Send mode
    char mode = 'i';
    send(sockfd, &mode, sizeof(mode), 0);

    if (ID == -1) {
        // Receive Client ID
        read(sockfd, &ID, sizeof(ID));
        ID_n = htonl(ID);
    }
    return 0;
}
#else
int netserverinit(char *hostname, int filemode){
    if (filemode < 0x0000 || filemode > 0x0002) {
        h_errno = INVALID_FILE_MODE;
        return -1;
    }

    struct hostent *hostnm = gethostbyname(hostname);
    if (hostnm == (struct hostent *) 0) {
        h_errno = HOST_NOT_FOUND;
        return -1;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = *((unsigned long *)hostnm->h_addr);

    int sockfd = conn();

    if (sockfd == -1)
        return -1;

    // Send Client ID
    send(sockfd, &ID_n, sizeof(ID_n), 0);

    // Send mode
    char mode = 'i';
    send(sockfd, &mode, sizeof(mode), 0);

    // Send filemode
    filemode = htonl(filemode);
    send(sockfd, &filemode, sizeof(filemode), 0);

    if (ID == -1) {
        // Receive Client ID
        read(sockfd, &ID, sizeof(ID));
        ID_n = htonl(ID);
    }
    return 0;
}
#endif