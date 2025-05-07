CC=gcc
CFLAGS=-Wall -g

SRC = server.c cjson/cJSON.c
OBJ = $(SRC:.c=.o)

all: server

server: $(OBJ)
	$(CC) $(CFLAGS) -o server $(OBJ) -lpthread

clean:
	rm -f server $(OBJ)
