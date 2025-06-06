#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../lib/cjson/cJSON.h"
#include <unistd.h>

#define LOCAL_IP "10.7.12.50"
#define LOCAL_UDP_PORT 5001
#define LOCAL_TCP_PORT 5000
#define UDP_HEARTBEAT_INTERVAL 1

#define MAX_LOAD_BALANCERS 10

typedef struct {
    char ip[INET_ADDRSTRLEN];
    int port;
    int auth_done;
    char send_buffer[128];
} LoadBalancerInfo;

void* udp_daemon(void* arg);
int load_lb_config(const char *filename, LoadBalancerInfo *lbs, int *lb_count);
