#include "libnetfiles.h"

#define XSTR(s) STR(s)
#define STR(s) #s

int main(int argc, char **argv){
    int file_mode = UNRESTRICTED_MODE;
    int open_mode = O_RDWR;
    char host[128] = "localhost";
    char filename[128] = "test.txt";

#if defined(EXT_A) && defined(FILE_MODE)
    if (FILE_MODE >= 0 && FILE_MODE <= 2)
        file_mode = FILE_MODE;
#endif

#ifdef OPEN_MODE
    if (OPEN_MODE >= 0 && OPEN_MODE <= 2)
        open_mode = OPEN_MODE;
#endif

#ifdef HOST
    strncpy(host, XSTR(HOST), 127);
#endif

#ifdef EXT_A
    netserverinit(host, file_mode);
#else
    netserverinit(host);
#endif

#ifdef FILE_NAME
    strncpy(filename, XSTR(FILE_NAME), 127);
#endif
    printf("filename: %s\n", filename);
    int fd = netopen(filename, open_mode);

#ifdef DEBUG
    printf("Client open\n");
#endif

    if (fd == -1) {
        perror("pError (open):");
        if (h_errno == TIMEOUT)
            fprintf(stderr, "TIMEOUT");
    }
    char *buff = calloc((1 << 6) + 1, sizeof(char));
#ifdef DEBUG
    printf("Client read\n");
#endif
    if ((netread(fd, buff, 1 << 6)) < 0){
        perror("pError (read):");
        herror("hError (read):");
    } else {
        printf("Content from netread: %s\n", buff);
        fflush(stdout);
    }
    for (int i = 0; i < 20; ++i) {
        buff[i] = 'A';
    }
    if ((netwrite(fd, buff, 1 << 6)) < 0){
        perror("pError (read):");
        herror("hError (read):");
    } else {
        printf("Content to netwrite: %s\n", buff);
        fflush(stdout);
    }

    char something[128];
    printf("Press enter to close file\n");
    read(STDIN_FILENO, something, 127);
    if ((netclose(fd)) < 0){
        perror("pError (close):");
        herror("hError (close):");
    }
    free(buff);
#ifdef DEBUG
    printf("Client exit");
#endif
    return 0;
}