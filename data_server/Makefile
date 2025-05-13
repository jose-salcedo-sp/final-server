
CC = gcc

TARGET = server

SRC = data_server.c

CFLAGS = $(shell mysql_config --cflags)
LDFLAGS = $(shell mysql_config --libs)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

