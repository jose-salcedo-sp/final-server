CC=gcc
CFLAGS=-Wall -g

SRC = proxy.c ../lib/cjson/cJSON.c ../lib/queue/queue.c
OBJ = $(SRC:.c=.o)

all: server

server: $(OBJ)
	$(CC) $(CFLAGS) -o proxy $(OBJ) -lpthread

clean:
	rm -f server $(OBJ)
