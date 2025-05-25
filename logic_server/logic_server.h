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
#include <jwt.h>

#define BUFFER_SIZE 4096
#define UUIDv7_SIZE 32
#define IP "127.0.0.1"
#define TCP_PORT 8080
#define UDP_PORT 9090
#define LB_COUNT 1
#define TIMEOUT 2

#define STRINGIFY(x) #x
#define MAKE_ADDR(ip, port) ip ":" STRINGIFY(port) 

extern const char *db_ips[LB_COUNT];
extern const char *client_ips[LB_COUNT];
extern int db_ports_udp[LB_COUNT];
extern int client_ports_udp[LB_COUNT];
extern int db_ports_tcp[LB_COUNT];
extern int client_ports_tcp[LB_COUNT];

extern const char *string_tcp_addr;
extern const char *string_udp_addr;

typedef char UUID[UUIDv7_SIZE];

typedef enum {
  	VALIDATE_USER = 0,
  	LOGOUT = 1,
  	CREATE_USER = 2, 
  	GET_USER_INFO = 3, 
  	CREATE_CHAT = 4, 
	ADD_TO_GROUP_CHAT = 5, 
	SEND_MESSAGE = 6, 
	GET_CHATS = 7, 
	GET_CHAT_MESSAGES = 8,
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

bool validate_token(const char *jwt, int *out_user_id);
char *create_token(int user_id);
