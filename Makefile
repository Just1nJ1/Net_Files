EXTENSIONS += -DEXT_A -DEXT_B -DEXT_C -DEXT_D

all: server client_U_RW

server: netfileserver.c
	gcc $(EXTENSIONS) -o server netfileserver.c

client_U_RW: client.c libnetfiles.c libnetfiles.h
	gcc $(EXTENSIONS) -DFILE_MODE=UNRESTRICTED_MODE -DOPEN_MODE=O_RDWR -DHOST="localhost" -DFILE_NAME="test.txt" client.c -o client_U_RW libnetfiles.c

clean:
	rm server
	rm client_U_RW
