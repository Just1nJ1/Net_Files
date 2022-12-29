#include <sys/stat.h>
#define _OPEN_SYS_MUTEX_EXT
#include "libnetfiles.h"

typedef struct client_permissions {
    int filemode;
    int read_i;
    int read_len;
    ino_t *read_c;
    int write_i;
    int write_len;
    ino_t *write_c;
} client_perms;

typedef struct file_usages {
    ino_t st_ino;
    int read_i;
    int read_len;
    int *read_c;
    int write_i;
    int write_len;
    int *write_c;
    node **header;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
} file_usages;

int ID = 0;
int file_ID = 0;
int client_len = 32;
int file_len = 32;
client_perms **clients;
file_usages **files;
int neg_1 = htonl(-1);
int *ports;
int available_ports;

file_usages *file_use_creator(ino_t ino) {
    file_usages *val = calloc(1, sizeof(file_usages));
    val->st_ino=ino;
    val->read_len=DEFAULT_PERM_LEN;
    val->read_i=0;
    val->read_c=calloc(DEFAULT_PERM_LEN, sizeof(int));
    val->write_len=DEFAULT_PERM_LEN;
    val->write_i=0;
    val->write_c=calloc(DEFAULT_PERM_LEN, sizeof(int));
    pthread_mutex_init(&val->mtx, NULL);
    pthread_cond_init(&val->cv, NULL);
    return val;
}

client_perms *client_perm_creator(int filemode) {
    client_perms *val = calloc(1, sizeof(client_perms));
    val->filemode = filemode;
    val->read_len=DEFAULT_PERM_LEN;
    val->read_i=0;
    val->read_c=calloc(DEFAULT_PERM_LEN, sizeof(ino_t));
    val->write_len=DEFAULT_PERM_LEN;
    val->write_i=0;
    val->write_c=calloc(DEFAULT_PERM_LEN, sizeof(ino_t));
    return val;
}

multiplex *multiplex_creator(int port_n, int len, int i, char *t_content, int indicator){
    multiplex *val = calloc(1, sizeof(multiplex));
    val->port_n = port_n;
    val->len = len;
    val->i = i;
    if (indicator) {
        val->content = calloc(len, sizeof(char));
        memcpy(val->content, t_content, len);
    }
    return val;
}

void *ext_b_sender(void *arg){
    multiplex args = *(multiplex *)arg;
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed\n");
        pthread_exit((void *)EXIT_FAILURE);
    }
    fflush(stdout);

    struct sockaddr_in myaddr;
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(args.port_n);
    myaddr.sin_addr.s_addr = INADDR_ANY;

    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
        perror("Socket setsockopt failed\n");
        close(sockfd);
        pthread_exit((void *)EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr *) &myaddr, sizeof(myaddr)) < 0) {
        perror("Socket binding failed\n");
        fprintf(stderr, "port %d\n", args.port_n);
        fprintf(stderr, "port %s\n", args.content);
        fprintf(stderr, "port %d\n", args.i);
        close(sockfd);
        pthread_exit((void *)EXIT_FAILURE);
    }
    fflush(stdout);

    if (listen(sockfd, 0) < 0) {
        perror("Socket listening failed\n");
        close(sockfd);
        pthread_exit((void *)EXIT_FAILURE);
    }
    fflush(stdout);

    int connfd = accept(sockfd, (struct sockaddr *) 0, (int *) 0);
#ifdef DEBUG
    printf("thread connect\n");
#endif
    send(connfd, args.content, args.len, 0);

    close(connfd);
    close(sockfd);

    pthread_exit((void *)EXIT_SUCCESS);
}

void *ext_b_receiver(void *arg){
    multiplex *args = (multiplex *)arg;
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed\n");
        return (void *)EXIT_FAILURE;
    }

    struct sockaddr_in myaddr;
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(args->port_n);
    myaddr.sin_addr.s_addr = INADDR_ANY;

    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
        perror("Socket setsockopt failed\n");
        close(sockfd);
        pthread_exit((void *)EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr *) &myaddr, sizeof(myaddr)) < 0) {
        perror("Socket binding failed\n");
        fprintf(stderr, "port %d\n", args->port_n);
        fprintf(stderr, "port %s\n", args->content);
        fprintf(stderr, "port %d\n", args->i);
        close(sockfd);
        pthread_exit((void *)EXIT_FAILURE);
    }

    if (listen(sockfd, 0) < 0) {
        perror("Socket listening failed\n");
        close(sockfd);
        pthread_exit((void *)EXIT_FAILURE);
    }

    int connfd = accept(sockfd, (struct sockaddr *) 0, (int *) 0);

//    args->content = calloc(args->len, sizeof(char));
//    char *buf = calloc(args->len, sizeof(char));
    args->content = calloc(args->len, sizeof(char));
    read(connfd, args->content, args->len);
//    memcpy(args->content, buf, args->len);

    close(connfd);

    if (close(sockfd) == -1){
        perror("close error");
        pthread_exit((void *)EXIT_FAILURE);
    }

    pthread_exit((void *)EXIT_SUCCESS);
}

void *ext_d_monitor(void *arg){
    while (1){
        for (int i = 0; i < file_ID; ++i) {
            if (files[i]->header != NULL){
                node *cur = *files[i]->header;
                while (cur != NULL){
                    if (time(NULL) - cur->sec > 2) {
                        cur->is_turn = -1;
                        pthread_cond_broadcast(&files[i]->cv);
                    }
                    cur = cur->next;
                }
            }
        }
        sleep(3);
    }
}

void *handler(void *fd) {
    int connfd = (int) fd;

    // Receive Client ID
    int client_id;
    read(connfd, &client_id, sizeof(client_id));
    client_id = ntohl(client_id);

    char mode = ' ';
    // Receive mode
    read(connfd, &mode, 1);

    if (client_id == -1) {
        int filemode = UNRESTRICTED_MODE;
#ifdef EXT_A
        read(connfd, &filemode, sizeof(filemode));
        filemode = ntohl(filemode);
#endif
        // Send new Client ID
        send(connfd, &ID, sizeof(ID), 0);
        clients[ID++] = client_perm_creator(filemode);
        if (client_len <= ID) {
            client_len = client_len << 1;
            clients = realloc(clients, client_len * sizeof(client_perms*));
        }
    } else {
        switch (mode) {
            case 'o': {
                // Receive flags
                int flags = 0;
                read(connfd, &flags, sizeof(flags));
                flags = ntohl(flags);

                // Receive path length
                unsigned long len;
                read(connfd, &len, sizeof(len));
                len = ntohl(len);

                // Receive path (+1 for \0)
                char *path = calloc(len + 1, sizeof(char));
                read(connfd, path, len);

#ifdef DEBUG
                printf("Client %d open file %s\n", client_id, path);
                fflush(stdout);
#endif
                int flag = 1, file = -1, is_added = 0;
                // Stores file into files list
                struct stat f_stat;
                if (stat(path, &f_stat) != -1) {
                    int i;
                    file_usages *target_file;
                    client_perms *target_client = clients[client_id];
                    for (i = 0; i < file_ID; ++i) {
                        target_file = files[i];
                        if (target_file->st_ino == f_stat.st_ino)
                            break;
                    }

                    if (i == file_ID) {
                        files[file_ID] = file_use_creator(f_stat.st_ino);
                        target_file = files[file_ID++];
                    }
                    if (file_len <= file_ID) {
                        file_len = file_len << 1;
                        files = realloc(files, file_len * sizeof(file_usages*));
                    }

                    node *n;

                    do {
#ifdef EXT_C
                        pthread_mutex_lock(&target_file->mtx);
#endif
                        // Two flags indicate the conflict has occurred or not, 1 means it is allowed to open in that mode
                        int read_flag = 1, write_flag = 1;
                        flag = 1;

                        // If current client in TRANSACTION mode, then anyone open the file will cause failed read/write
                        if (target_client->filemode == TRANSACTION_MODE) {
                            if (target_file->read_i != 0 || target_file->write_i != 0)
                                read_flag = 0, write_flag = 0;
                        } else {
                            // If someone open target file in read
                            if (target_file->read_i != 0) {
                                for (int j = 0; j < target_file->read_i && read_flag; ++j) {
                                    client_perms tmp = *clients[target_file->read_c[j]];

                                    // If anyone open in TRANSACTION mode, then current client cannot read/write
                                    if (tmp.filemode == TRANSACTION_MODE)
                                        read_flag = 0, write_flag = 0;
                                        // If anyone open in EXCLUSIVE mode, then current client cannot write
                                        // This duplicated check is because maybe in EXCLUSIVE mode with read permission only
                                    else if (tmp.filemode == EXCLUSIVE_MODE)
                                        write_flag = 0;
                                }
                            }
                            if (target_file->write_i != 0) {
                                // If someone open target file in write
                                if (target_client->filemode == EXCLUSIVE_MODE)
                                    // If current client want to open in EXCLUSIVE mode, then current client cannot write
                                    write_flag = 0;
                                for (int j = 0; j < target_file->write_i && write_flag; ++j) {
                                    client_perms tmp = *clients[target_file->write_c[j]];
                                    // If anyone open in TRANSACTION mode, then current client cannot read/write
                                    // This duplicated check is because maybe in TRANSACTION mode with write permission only
                                    if (tmp.filemode == TRANSACTION_MODE)
                                        read_flag = 0, write_flag = 0;
                                        // If anyone open in EXCLUSIVE mode, then current client cannot write
                                    else if (tmp.filemode == EXCLUSIVE_MODE)
                                        write_flag = 0;
                                }
                            }
                        }

                        // If current client wants to open file with read permission and read_flag is false
                        if ((flags == O_RDONLY || flags == O_RDWR) && !read_flag)
                            flag = 0;
                        // If current client wants to open file with write permission and write_flag is false
                        if ((flags == O_WRONLY || flags == O_RDWR) && !write_flag)
                            flag = 0;
#ifdef EXT_C
                        if (flag == 0){
                            if (!is_added){
                                n = calloc(1, sizeof(node));
                                n->next = NULL;
                                n->is_turn = 0;
                                n->sec = time(NULL);
                                if (target_file->header == NULL || *target_file->header == NULL) {
                                    target_file->header = &n;
                                    n->prev = NULL;
                                } else {
                                    node *cur = *target_file->header;
                                    while (cur->next != NULL)
                                        cur = cur->next;
                                    cur->next = n;
                                    n->prev = cur;
                                }
                                is_added = 1;
                            }
                            if (n->next != NULL) {
                                node *cur = n;
                                while (cur->next != NULL && cur->next->is_turn == -1)
                                    cur = cur->next;
                                if (cur->next != NULL)
                                    cur->next->is_turn = 1;
                            }
                            while (n->is_turn == 0)
                                pthread_cond_wait(&target_file->cv, &target_file->mtx);
                        }
                        pthread_mutex_unlock(&target_file->mtx);
                        if (n != NULL && n->is_turn == -1)
                            break;
#else
                        break;
#endif
                    } while (flag == 0);
#ifdef EXT_C
                    if (is_added){
                        if (n->next != NULL) {
                            n->next->is_turn = 1;
                        }

                        if (n->prev != NULL) {
                            n->prev = n->next;
                        }
                        else {
                            target_file->header = &n->next;
                        }
                        if (n->next != NULL) {
                            n->next->prev = n->prev;
                        }

                        n->prev = NULL;
                        n->next = NULL;
                    }

                    if (n != NULL && n->is_turn == -1) {
                        send(connfd, &neg_1, sizeof(neg_1), 0);
                        int err = htonl(EWOULDBLOCK);
                        int herr = htonl(TIMEOUT);
                        send(connfd, &err, sizeof(err), 0);
                        send(connfd, &herr, sizeof(herr), 0);
                        free(n);
                        break;
                    }
#else
                    if (flag == 0){
                        // Send neg_fd
                        send(connfd, &neg_1, sizeof(neg_1), 0);
                        // Send errno
                        int err = htonl(EACCES);
                        int herr = htonl(h_errno);
                        send(connfd, &err, sizeof(err), 0);
                        send(connfd, &herr, sizeof(herr), 0);
                        break;
                    }
#endif

                    // If the file open successfully
                    if ((file = open(path, flags)) != -1) {
                        if (flags == O_RDONLY || flags == O_RDWR) {
                            if (target_file->read_i >= target_file->read_len) {
                                target_file->read_len = target_file->read_len << 1;
                                target_file->read_c = realloc(target_file->read_c, target_file->read_len * sizeof(int));
                            }
                            target_file->read_c[target_file->read_i++] = client_id;

                            if (target_client->read_i >= target_client->read_len) {
                                target_client->read_len = target_client->read_len << 1;
                                target_client->read_c = realloc(target_client->read_c,
                                                               target_client->read_len * sizeof(ino_t));
                            }
                            target_client->read_c[target_client->read_i++] = f_stat.st_ino;
                        }

                        if (flags == O_WRONLY || flags == O_RDWR) {
                            if (target_file->write_i >= target_file->write_len) {
                                target_file->write_len = target_file->write_len << 1;
                                target_file->write_c = realloc(target_file->write_c, target_file->write_len * sizeof(int));
                            }
                            target_file->write_c[target_file->write_i++] = client_id;

                            if (target_client->write_i >= target_client->write_len) {
                                target_client->write_len = target_client->write_len << 1;
                                target_client->write_c = realloc(target_client->write_c,
                                                                target_client->write_len * sizeof(ino_t));
                            }
                            target_client->write_c[target_client->write_i++] = f_stat.st_ino;
                        }
                    }
                }

                // Negative file descriptor
                int neg_fd = htonl(-file);

                if (file == -1) {
                    // Send neg_fd
                    send(connfd, &neg_1, sizeof(neg_1), 0);
                    // Send errno
                    int err = htonl(errno);
                    int herr = htonl(h_errno);
                    send(connfd, &err, sizeof(err), 0);
                    send(connfd, &herr, sizeof(herr), 0);
                } else {
                    // Send neg_fd
                    send(connfd, &neg_fd, sizeof(neg_fd), 0);
                }
                break;
            }
            case 'r': {
                // Receive neg_fd
                int tmp = 0;
                read(connfd, &tmp, sizeof(tmp));
                int target_fd = ntohl(tmp);
                target_fd = -target_fd;

                // Receive nbyte
                read(connfd, &tmp, sizeof(tmp));
                int nbyte = ntohl(tmp);

#ifdef DEBUG
                printf("Client %d read file %d bytes\n", client_id, nbyte);
                fflush(stdout);
#endif

                struct stat f_stat;
                int flag = 0;
                if (fstat(target_fd, &f_stat) == -1) {
                    send(connfd, &neg_1, sizeof(neg_1), 0);
                    // Send errno
                    int err = htonl(errno);
                    int herr = htonl(h_errno);
                    send(connfd, &err, sizeof(err), 0);
                    send(connfd, &herr, sizeof(herr), 0);
                    break;
                }

                client_perms target_client = *clients[client_id];
                for (int i = 0; i < target_client.read_i; ++i) {
                    if (target_client.read_c[i] == f_stat.st_ino) {
                        flag = 1;
                        break;
                    }
                }

                if (!flag) {
                    send(connfd, &neg_1, sizeof(neg_1), 0);
                    // Send errno
                    int err = htonl(errno);
                    int herr = htonl(h_errno);
                    send(connfd, &err, sizeof(err), 0);
                    send(connfd, &herr, sizeof(herr), 0);
                    break;
                }

                char *content = calloc(nbyte + 1, sizeof(char));
//                char *message = calloc(MAX_MESSAGE_SIZE + 1, sizeof(char));
                ssize_t cnt = read(target_fd, content, nbyte);

                // Send nbyte
                tmp = htonl(cnt);
                send(connfd, &tmp, sizeof(tmp), 0);

                if (cnt == -1) {
                    // Send errno
                    int err = htonl(errno);
                    int herr = htonl(h_errno);
                    send(connfd, &err, sizeof(err), 0);
                    send(connfd, &herr, sizeof(herr), 0);
                } else {
#ifdef EXT_B
                    int n_seg = 0;
                    int seg_enough = 1;
                    int *tmp_ports;
                    if (cnt > MAX_MESSAGE_SIZE) {
                        n_seg = (int)cnt / MAX_MESSAGE_SIZE - (cnt % MAX_MESSAGE_SIZE == 0);
                        if (n_seg > available_ports){
                            n_seg = available_ports;
                            seg_enough = 0;
                        }
                        available_ports -= n_seg;
                        tmp_ports = calloc(n_seg, sizeof(int));
                        for (int i = 0, j = 0; i < n_seg && j < EXTRA_PORTS; ++j) {
                            if (!ports[j]){
                                ports[j] = 1;
                                tmp_ports[i++] = j;
                            }
                        }
                    }

                    // Send number of segments
                    int n_seg_m = htonl(n_seg);
                    send(connfd, &n_seg_m, sizeof(n_seg_m), 0);

                    int seg_l;
                    if (seg_enough)
                        seg_l = MAX_MESSAGE_SIZE;
                    else
                        seg_l = (int)cnt / (n_seg + 1);

                    // Send segment length
                    int seg_l_m = htonl(seg_l);
                    send(connfd, &seg_l_m, sizeof(seg_l_m), 0);

                    pthread_t *pid;
                    multiplex **ms;

                    if (n_seg) {
                        // Send n_seg port numbers
                        for (int i = 0; i < n_seg; ++i){
                            int port_m = htonl(PORTRANGE_BEGIN + tmp_ports[i]);
                            send(connfd, &port_m, sizeof(port_m), 0);
                        }

                        // Send n_seg by worker thread
                        pid = calloc(n_seg, sizeof(pthread_t));
                        ms = calloc(n_seg, sizeof(multiplex*));
                        char *t_ptr = content + cnt;
                        for (int i = 0; i < n_seg; ++i) {
                            char *t_content = calloc(seg_l, sizeof(char));
                            t_ptr -= seg_l;
                            memcpy(t_content, t_ptr, seg_l);
                            ms[i] = multiplex_creator(PORTRANGE_BEGIN + tmp_ports[i], seg_l, i, t_content, 1);
                            pthread_create(pid + i, NULL, ext_b_sender, ms[i]);
//                            ext_b_sender(&m);
                        }
                    }

                    // Send first segment
                    send(connfd, content, nbyte - seg_l * n_seg, 0);
                    for (int i = 0; i < n_seg; ++i) {
                        void *result;
                        pthread_join(pid[i], &result);
                        ports[tmp_ports[i]] = 0;
                        available_ports ++;
                    }
#ifdef DEBUG
                    printf("main end\n");
#endif
#else
                    send(connfd, content, cnt, 0);
#endif
                }
                break;
            }
            case 'w': {
                // Receive neg_fd
                int tmp = 0;
                read(connfd, &tmp, sizeof(tmp));
                int target_fd = ntohl(tmp);
                target_fd = -target_fd;

                // Receive nbyte
                read(connfd, &tmp, sizeof(tmp));
                int nbyte = ntohl(tmp);

#ifdef DEBUG
                printf("Client %d write file %d bytes\n", client_id, nbyte);
                fflush(stdout);
#endif
                struct stat f_stat;
                int flag = 0;
                if (fstat(target_fd, &f_stat) == -1) {
                    send(connfd, &neg_1, sizeof(neg_1), 0);
                    // Send errno
                    int err = htonl(errno);
                    int herr = htonl(h_errno);
                    send(connfd, &err, sizeof(err), 0);
                    send(connfd, &herr, sizeof(herr), 0);
                    break;
                }

                client_perms target_client = *clients[client_id];
                for (int i = 0; i < target_client.write_i; ++i) {
                    if (target_client.write_c[i] == f_stat.st_ino) {
                        flag = 1;
                        break;
                    }
                }

                if (!flag) {
                    send(connfd, &neg_1, sizeof(neg_1), 0);
                    // Send errno
                    int err = htonl(errno);
                    int herr = htonl(h_errno);
                    send(connfd, &err, sizeof(err), 0);
                    send(connfd, &herr, sizeof(herr), 0);
                    break;
                }

                char *content = calloc(nbyte + 1, sizeof(char));
#ifdef EXT_B
                int n_seg = 0;
                int seg_enough = 1;
                int *tmp_ports;
                if (nbyte > MAX_MESSAGE_SIZE) {
                    n_seg = (int)nbyte / MAX_MESSAGE_SIZE - (nbyte % MAX_MESSAGE_SIZE == 0);
                    if (n_seg > available_ports){
                        n_seg = available_ports;
                        seg_enough = 0;
                    }
                    available_ports -= n_seg;
                    tmp_ports = calloc(n_seg, sizeof(int));
                    for (int i = 0, j = 0; i < n_seg && j < EXTRA_PORTS; ++j) {
                        if (!ports[j]){
                            ports[j] = 1;
                            tmp_ports[i++] = j;
                        }
                    }
                }

                // Send number of segments
                int n_seg_m = htonl(n_seg);
                send(connfd, &n_seg_m, sizeof(n_seg_m), 0);

                int seg_l;
                if (seg_enough)
                    seg_l = MAX_MESSAGE_SIZE;
                else
                    seg_l = (int)nbyte / (n_seg + 1);

                // Send segment length
                int seg_l_m = htonl(seg_l);
                send(connfd, &seg_l_m, sizeof(seg_l_m), 0);

                pthread_t *pid;
                multiplex **ms;

                if (n_seg) {
                    // Receive n_seg by worker thread
                    pid = calloc(n_seg, sizeof(pthread_t));
                    ms = calloc(n_seg, sizeof(multiplex));

                    // Send n_seg port numbers
                    for (int i = 0; i < n_seg; ++i){
                        ms[i] = multiplex_creator(PORTRANGE_BEGIN + tmp_ports[i], seg_l, i, NULL, 0);
                        pthread_create(pid + i, NULL, ext_b_receiver, ms[i]);
                        int port_m = htonl(PORTRANGE_BEGIN + tmp_ports[i]);
                        send(connfd, &port_m, sizeof(port_m), 0);
//            ext_b_receiver(&m);
                    }
                }

                // Read first segment
                read(connfd, content, nbyte - seg_l * n_seg);

                for (int i = 0; i < n_seg; ++i) {
                    void *result;
                    pthread_join(pid[i], &result);
                    ports[tmp_ports[i]] = 0;
                    available_ports ++;
                    memcpy(content + (nbyte - seg_l * n_seg) + (n_seg - ms[i]->i - 1) * seg_l, ms[i]->content, ms[i]->len);
                }
#else
                read(connfd, content, nbyte);
#endif
                ssize_t cnt = write(target_fd, content, nbyte);

                // Send real nbyte
                tmp = htonl(cnt);
                send(connfd, &tmp, sizeof(tmp), 0);

                if (cnt == -1) {
                    // Send errno
                    int err = htonl(errno);
                    int herr = htonl(h_errno);
                    send(connfd, &err, sizeof(err), 0);
                    send(connfd, &herr, sizeof(herr), 0);
                }
                break;
            }
            case 'c': {
                // Receive neg_fd
                int tmp = 0;
                read(connfd, &tmp, sizeof(tmp));
                int target_fd = ntohl(tmp);
                target_fd = -target_fd;

                struct stat f_stat;
                fstat(target_fd, &f_stat);
                int i;
                file_usages *target_file;
                client_perms *target_client = clients[client_id];
                for (i = 0; i < file_ID; ++i) {
                    target_file = files[i];
                    if (target_file->st_ino == f_stat.st_ino)
                        break;
                }

                if (i == file_ID) {
                    // This should not happen
                } else {
                    // Clear record in read list
                    // if (flags == O_RDONLY || flags == O_RDWR)
                    int flag = 0;
                    for (int j = 0; j < target_file->read_i; ++j) {
                        if (target_file->read_c[j] == client_id)
                            flag = 1;
                        if (flag && j != target_file->read_i)
                            target_file->read_c[j] = target_file->read_c[j + 1];
                    }

                    if (flag)
                        target_file->read_i--;

                    flag = 0;
                    for (int j = 0; j < target_client->read_i; ++j) {
                        if (target_client->read_c[j] == f_stat.st_ino)
                            flag = 1;
                        if (flag && j != target_client->read_i)
                            target_client->read_c[j] = target_client->read_c[j + 1];
                    }

                    if (flag)
                        target_client->read_i--;

                    // Clear record in write list
                    // if (flags == O_WRONLY || flags == O_RDWR)
                    flag = 0;
                    for (int j = 0; j < target_file->write_i; ++j) {
                        if (target_file->write_c[j] == client_id)
                            flag = 1;
                        if (flag && j != target_file->read_i)
                            target_file->write_c[j] = target_file->write_c[j + 1];
                    }

                    if (flag)
                        target_file->write_i--;

                    flag = 0;
                    for (int j = 0; j < target_client->write_i; ++j) {
                        if (target_client->write_c[j] == f_stat.st_ino)
                            flag = 1;
                        if (flag && j != target_client->read_i)
                            target_client->write_c[j] = target_client->write_c[j + 1];
                    }

                    if (flag)
                        target_client->write_i--;
                }

                int result = close(target_fd);

                // Send return value
                tmp = htonl(result);
                send(connfd, &tmp, sizeof(tmp), 0);

                if (result == -1) {
                    // Send errno
                    int err = htonl(errno);
                    int herr = htonl(h_errno);
                    send(connfd, &err, sizeof(err), 0);
                    send(connfd, &herr, sizeof(herr), 0);
                }
                if (target_file->header != NULL && *target_file->header != NULL) {
                    while (*target_file->header != NULL && (*target_file->header)->is_turn == -1)
                        *target_file->header = (*target_file->header)->next;
                    if (*target_file->header != NULL)
                        (*target_file->header)->is_turn = 1;
                    pthread_cond_broadcast(&target_file->cv);
                }
                break;
            }
            default: {
                break;
            }
        }
    }
    pthread_exit((void *)EXIT_SUCCESS);
}

int main() {
    int sockfd, connfd;

    clients = calloc(client_len, sizeof(client_perms*));
    files = calloc(file_len, sizeof(file_usages*));
    ports = calloc(EXTRA_PORTS, sizeof(int));
    available_ports = EXTRA_PORTS;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed\n");
        exit(EXIT_FAILURE);
    }
    fflush(stdout);

    struct sockaddr_in myaddr;
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(PORT);
    myaddr.sin_addr.s_addr = INADDR_ANY;

    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0){
        perror("Socket setsockopt failed\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr *) &myaddr, sizeof(myaddr)) < 0) {
        perror("Socket binding failed\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    fflush(stdout);

    if (listen(sockfd, 0) < 0) {
        perror("Socket listening failed\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    fflush(stdout);

#if defined(EXT_C) && defined(EXT_D)
    pthread_t pid;
    pthread_create(&pid, NULL, ext_d_monitor, NULL);
#endif

    while (1) {
        connfd = accept(sockfd, (struct sockaddr *) 0, (int *) 0);
#ifdef DEBUG
        printf("Client is connected\n");
        fflush(stdout);
#endif
        if (connfd == -1) {
            perror("Socket accepting failed\n");
            continue;
        }
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handler, (void *) connfd);
    }
}