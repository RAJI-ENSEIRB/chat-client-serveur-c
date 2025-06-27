CFLAGS=-Wall
#LDFLAGS=-lpthread

all: client server

client: client.c common.h
	gcc $(CFLAGS) -o client client.c $(LDFLAGS)

server: server.c common.h
	gcc $(CFLAGS) -o server server.c $(LDFLAGS)

clean:
	rm -f client server

.PHONY: all clean