# Compiler and flags
CC = gcc
CFLAGS = -Wall -I../lib/cjson
LDFLAGS = -lmysqlclient

# Source files
SRC = data_server.c user_manager.c chat_manager.c heartbeat_manager.c ../lib/cjson/cJSON.c
OBJ = $(SRC:.c=.o)

# Output binary
TARGET = server

# Default rule
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)
	rm -f *.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -f $(OBJ) $(TARGET)
