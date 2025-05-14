#include "../dbg.h"
#include "../lib/cjson/cJSON.h"
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 4096
#define UUIDv7_SIZE 32
#define  PORT      6969

typedef char UUID[UUIDv7_SIZE];

typedef enum {
  LOGIN = 0,
  LOGOUT = 1,
  REGISTER = 2,
  MSGSEND = 3,
  CHATJOIN = 5,
  PING = 69,
} ACTIONS;
