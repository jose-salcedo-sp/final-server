CC = gcc
CFLAGS = -Wall -Wextra -g -I/usr/local/include
LDFLAGS = -L/usr/local/lib
LDLIBS = -ljwt

SRC = logic_server.c udp_lb_daemon.c ../lib/cjson/cJSON.c
OUT = logic_server

all:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(OUT) $(SRC) $(LDLIBS)