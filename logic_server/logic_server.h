#include "../dbg.h"
#include "../lib/cjson/cJSON.h"
//#include "bcrypt.h"
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
#define IP "10.7.14.51"
#define TCP_PORT 8080
#define UDP_PORT 9090
#define LB_COUNT 2
#define TIMEOUT 2
#define MAX_PENDING_REQUESTS 100
#define CESAR_SHIFT 3 


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
  	CREATE_USER = 2, 
  	GET_USER_INFO = 3, 
  	CREATE_CHAT = 4, 
	ADD_TO_GROUP_CHAT = 5, 
	SEND_MESSAGE = 6, 
	GET_CHATS = 7, 
	GET_CHAT_MESSAGES = 8,
	GET_CHAT_INFO = 9,
	REMOVE_FROM_CHAT = 10,
	EXIT_CHAT = 11,
  	PING = 100,
} ACTIONS;

typedef enum {
    SENDING_ADDRESS,
    SENDING_OK
} LBState;

typedef enum {
    AUTH_STATE_INITIAL,
    AUTH_STATE_VALIDATED,
    AUTH_STATE_GOT_USER_INFO
} AuthState;

typedef struct {
    struct sockaddr_in addr;
    LBState state;
} UdpLoadBalancer;

typedef struct {
    int code;
    const char *text;
} ErrorResponse;

typedef struct {
    ACTIONS action;
    const char *required_fields[10];
} ActionValidation;

typedef struct {
    ACTIONS action;
    cJSON *request_json;  // Original request from client
    AuthState auth_state; // For tracking authentication flow
    char *key;       // Store username between requests
    int current_db_sock;  // Track the current DB socket
} CurrentRequest;

void udp_lb_daemon();
bool validate_token(const char *jwt, int *out_user_id);
char *create_token(int user_id);
bool verify_password(const char *input_pass, const char *hashed_pass);

