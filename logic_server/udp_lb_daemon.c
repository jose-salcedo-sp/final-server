// udp_lb_daemon.c

#include "logic_server.h"

#define MESSAGE_INTERVAL 5  // seconds between messages

UdpLoadBalancer db_lbs[LB_COUNT];
UdpLoadBalancer client_lbs[LB_COUNT];


void init_udp_load_balancers() {

    for (int i = 0; i < LB_COUNT; i++) {
        memset(&db_lbs[i], 0, sizeof(UdpLoadBalancer));
        db_lbs[i].addr.sin_family = AF_INET;
        db_lbs[i].addr.sin_port = htons(db_ports_udp[i]);
        inet_pton(AF_INET, db_ips[i], &db_lbs[i].addr.sin_addr);
        db_lbs[i].state = SENDING_ADDRESS;

        memset(&client_lbs[i], 0, sizeof(UdpLoadBalancer));
        client_lbs[i].addr.sin_family = AF_INET;
        client_lbs[i].addr.sin_port = htons(client_ports_udp[i]);
        inet_pton(AF_INET, client_ips[i], &client_lbs[i].addr.sin_addr);
        client_lbs[i].state = SENDING_ADDRESS;
    }
}

void udp_lb_daemon() {
	init_udp_load_balancers();
    int sockfd;
    struct sockaddr_in local_addr;
    char message[BUFFER_SIZE];

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        log_err("UDP socket");
        exit(1);
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(UDP_PORT);

    if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        log_err("UDP bind");
        close(sockfd);
        exit(1);
    }

    log_info("UDP daemon started on port %i", UDP_PORT);

    struct pollfd fds[1];
    fds[0].fd = sockfd;
    fds[0].events = POLLIN;

    time_t last_sent = 0;

    while (1) {
        time_t now = time(NULL);
        if (now - last_sent >= MESSAGE_INTERVAL) {
            snprintf(message, sizeof(message), "%s %s", string_tcp_addr, string_udp_addr);

            for (int i = 0; i < LB_COUNT; i++) {
                UdpLoadBalancer *lb[] = { &client_lbs[i], &db_lbs[i] };
                for (int j = 0; j < 2; j++) {
                    if (lb[j]->state == SENDING_ADDRESS) {
                        sendto(sockfd, message, strlen(message), 0,
                               (struct sockaddr *)&lb[j]->addr, sizeof(lb[j]->addr));
						log_info("Sent ADDRR to %s:%d", (char *)inet_ntoa(lb[j]->addr.sin_addr), ntohs(lb[j]->addr.sin_port));
                    } else {
                        sendto(sockfd, "OK", 2, 0,
                               (struct sockaddr *)&lb[j]->addr, sizeof(lb[j]->addr));
						log_info("Sent OK to %s:%d", (char *)inet_ntoa(lb[j]->addr.sin_addr), ntohs(lb[j]->addr.sin_port));
                    }
                }
            }
            last_sent = now;
        }

        int ret = poll(fds, 1, 1000);
        if (ret > 0 && (fds[0].revents & POLLIN)) {
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            char buf[BUFFER_SIZE];
            int n = recvfrom(sockfd, buf, sizeof(buf)-1, 0,
                             (struct sockaddr *)&from, &fromlen);
            if (n < 0) {
                log_warn("recvfrom");
                continue;
            }

            buf[n] = '\0';
            log_info("UDP recv: %s", buf);

            for (int i = 0; i < LB_COUNT; i++) {
                UdpLoadBalancer *lb[] = { &client_lbs[i], &db_lbs[i] };
                for (int j = 0; j < 2; j++) {
                    if (memcmp(&from, &lb[j]->addr, sizeof(from)) == 0) {
						log_info("PAIRED IP");
						//For test case we are adding \n
                        if (strcmp(buf, "OK\n") == 0) {
                            lb[j]->state = SENDING_OK;
						//For test case we are adding \n
                        } else if (strcmp(buf, "AUTH\n") == 0) {
                            lb[j]->state = SENDING_ADDRESS;
                        } else{
							log_info("NOT VALID ACTION");
						}
                    }
                }
            }
        }
    }
}
