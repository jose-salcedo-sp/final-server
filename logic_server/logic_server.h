#include "../dbg.h"
#include "../lib/cjson/cJSON.h"
#include <arpa/inet.h>
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
#include <stdbool.h> 
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#define BUFFER_SIZE 4096
#define UUIDv7_SIZE 32
#define TCP_PORT 8080
#define UDP_PORT 9090
#define LB_COUNT 1

typedef char UUID[UUIDv7_SIZE];

typedef enum {
  LOGIN = 0,
  LOGOUT = 1,
  REGISTER = 2,
  MSGSEND = 3,
  CHATJOIN = 5,
  PING = 69,
} ACTIONS;

typedef enum {
    SENDING_ADDRESS,
    SENDING_OK
} LBState;

typedef struct {
    struct sockaddr_in addr;
    LBState state;
} UdpLoadBalancer;

void udp_lb_daemon();
