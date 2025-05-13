CC=gcc
CFLAGS=-Wall -g

SRC = load_balancer.c ../lib/cjson/cJSON.c ../lib/queue/queue.c
OBJ = $(SRC:.c=.o)

all: server

server: $(OBJ)
	$(CC) $(CFLAGS) -o load_balancer $(OBJ) -lpthread

clean:
	rm -f load_balancer $(OBJ)
