#include "heartbeat_manager.h"

void* udp_daemon(void* arg) {
    LoadBalancerInfo load_balancers[MAX_LOAD_BALANCERS];
    int lb_count = 0;

	if (load_lb_config("load_balancers.json", load_balancers, &lb_count) != 0 || lb_count == 0) {
        fprintf(stderr, "No se pudieron cargar los load balancers\n");
        pthread_exit(NULL);
    }

    int udp_sock;
    struct sockaddr_in local_addr, lb_addrs[MAX_LOAD_BALANCERS];

    char tcp_addr[32];
    char udp_addr[32];
    char recv_buffer[128];

    if ((udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket");
        pthread_exit(NULL);
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(LOCAL_UDP_PORT);

    if (bind(udp_sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind UDP");
        close(udp_sock);
        pthread_exit(NULL);
    }

    snprintf(tcp_addr, sizeof(tcp_addr), "%s:%d", LOCAL_IP, LOCAL_TCP_PORT);
    snprintf(udp_addr, sizeof(udp_addr), "%s:%d", LOCAL_IP, LOCAL_UDP_PORT);


    for (int i = 0; i < lb_count; i++) {
        memset(&lb_addrs[i], 0, sizeof(struct sockaddr_in));
        lb_addrs[i].sin_family = AF_INET;
        lb_addrs[i].sin_port = htons(load_balancers[i].port);
        if (inet_pton(AF_INET, load_balancers[i].ip, &lb_addrs[i].sin_addr) <= 0) {
            fprintf(stderr, "Invalid IP address: %s\n", load_balancers[i].ip);
            continue;
        }
    }

int heartbeat_counter = 0;
char update = 0;

while (1) {
    heartbeat_counter++;

    for (int i = 0; i < lb_count; i++) {
        if (load_balancers[i].auth_done)
            strcpy(load_balancers[i].send_buffer, "OK");
        else
            snprintf(load_balancers[i].send_buffer, sizeof(load_balancers[i].send_buffer), "%s %s", tcp_addr, udp_addr);

        if (sendto(udp_sock, load_balancers[i].send_buffer, strlen(load_balancers[i].send_buffer), 0,
                   (struct sockaddr*)&lb_addrs[i], sizeof(lb_addrs[i])) < 0) {
            perror("sendto UDP");
        }
    }


    for (int i = 0; i < lb_count; i++) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int received = recvfrom(udp_sock, recv_buffer, sizeof(recv_buffer) - 1,
                                MSG_DONTWAIT, (struct sockaddr*)&from_addr, &from_len);
        if (received > 0) {
            update = 1;
            recv_buffer[received] = '\0';

            char from_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(from_addr.sin_addr), from_ip, INET_ADDRSTRLEN);

            for (int j = 0; j < lb_count; j++) {
                if (strcmp(from_ip, load_balancers[j].ip) == 0) {
                    if (strcmp(recv_buffer, "OK") == 0)
                        load_balancers[j].auth_done = 1;
                    else if (strcmp(recv_buffer, "AUTH") == 0)
                        load_balancers[j].auth_done = 0;
                    break;
                }
            }
        }
    }

    if ((heartbeat_counter % 5 == 0) || update) {
        printf("[UDP #%d]", heartbeat_counter);
        for (int i = 0; i < lb_count; i++) {
            printf(" | %s:%d:%s",
                   load_balancers[i].ip,
                   load_balancers[i].port,
                   load_balancers[i].auth_done ? "OK" : "AUTH");
        }
        printf("\n");
    }

    update = 0;
    sleep(UDP_HEARTBEAT_INTERVAL);
}

    close(udp_sock);
    pthread_exit(NULL);
}

int load_lb_config(const char *filename, LoadBalancerInfo *lbs, int *lb_count) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char *data = malloc(len + 1);
    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(data);
    free(data);
    if (!root) {
        fprintf(stderr, "Error parsing JSON\n");
        return -1;
    }

    cJSON *array = cJSON_GetObjectItem(root, "load_balancers");
    if (!cJSON_IsArray(array)) {
        fprintf(stderr, "load_balancers must be an array\n");
        cJSON_Delete(root);
        return -1;
    }

    *lb_count = 0;
    cJSON *entry;
    cJSON_ArrayForEach(entry, array) {
        if (*lb_count >= MAX_LOAD_BALANCERS) break;

        cJSON *ip = cJSON_GetObjectItem(entry, "ip");
        cJSON *port = cJSON_GetObjectItem(entry, "port");

        if (!cJSON_IsString(ip) || !cJSON_IsNumber(port)) continue;

        strncpy(lbs[*lb_count].ip, ip->valuestring, INET_ADDRSTRLEN);
        lbs[*lb_count].port = port->valueint;
        (*lb_count)++;
    }

    cJSON_Delete(root);
    return 0;
}
