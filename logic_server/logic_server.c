#include "logic_server.h"
int sd;
int udp_sd;

void abort_handler(int sig) {
    printf("Shutting down server...\n");
    close(sd);
    close(udp_sd);
    exit(0);
}

void sigchld_handler(int sig) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

char* process_client_request(const char *raw_json, int backend_fd, bool *handled_locally) {
    cJSON *json = cJSON_Parse(raw_json);
    if (!json) {
        log_warn("Invalid JSON from client");
        *handled_locally = true;
        return strdup("{\"error\":\"invalid JSON\"}");
    }

    cJSON *action_json = cJSON_GetObjectItemCaseSensitive(json, "action");
    if (!cJSON_IsNumber(action_json)) {
        log_warn("Missing or invalid 'action'");
        cJSON_Delete(json);
        *handled_locally = true;
        return strdup("{\"error\":\"missing or invalid action\"}");
    }

    ACTIONS action = (ACTIONS)action_json->valueint;

    switch (action) {
        case PING:
            log_info("Handling PING locally");
            *handled_locally = true;
            cJSON_Delete(json);
            return strdup("{\"response\":\"pong\"}");

        case LOGIN:
        case REGISTER:
            log_info("Forwarding action %d to DB", action);
            *handled_locally = false;
            cJSON_Delete(json);
            return strdup(raw_json);

        default:
            log_warn("Unknown action: %d", action);
            *handled_locally = true;
            cJSON_Delete(json);
            return strdup("{\"error\":\"unknown action\"}");
    }
}

int connect_to_db(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res, *rp;
    int sock;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0) {
        perror("getaddrinfo");
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;

        close(sock);
    }

    freeaddrinfo(res);
    if (rp == NULL) return -1;

    return sock;
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_received;

    int db_sock = connect_to_db("127.0.0.1", "6060");
    if (db_sock < 0) {
        log_err("Failed to connect to DB");
        close(client_sock);
        exit(1);
    }

    struct pollfd fds[2];
    fds[0].fd = client_sock;
    fds[0].events = POLLIN;
    fds[1].fd = db_sock;
    fds[1].events = POLLIN;

    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            log_err("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) break;
            buffer[bytes_received] = '\0';

            log_info("Received JSON from client: %s", buffer);

            bool handled_locally = false;
            char *response = process_client_request(buffer, db_sock, &handled_locally);
        
            if (handled_locally) {
                send(client_sock, response, strlen(response), 0);
            } else {
                write(db_sock, response, strlen(response));
            }
        
            free(response);
        }

        if (fds[1].revents & POLLIN) {
            bytes_received = recv(db_sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) break;
            buffer[bytes_received] = '\0';

            log_info("Received response from DB: %s", buffer);

            send(client_sock, buffer, bytes_received, 0);
        }
    }

    close(client_sock);
    close(db_sock);
    exit(0);
}

int main() {
    struct sockaddr_in sind, pin;
    int addrlen = sizeof(pin);
    pid_t pid;

    if (signal(SIGINT, abort_handler) == SIG_ERR) {
        log_err("Could not set SIGINT handler");
        return 1;
    }

    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) {
        log_err("Could not set SIGCHLD handler");
        return 1;
    }

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        log_err("socket");
        return 1;
    }

    memset(&sind, 0, sizeof(sind));
    sind.sin_family = AF_INET;
    sind.sin_addr.s_addr = INADDR_ANY;
    sind.sin_port = htons(TCP_PORT);

    if (bind(sd, (struct sockaddr *)&sind, sizeof(sind)) == -1) {
        log_err("bind");
        close(sd);
        return 1;
    }

    if (listen(sd, 5) == -1) {
        log_err("listen");
        close(sd);
        return 1;
    }

    pid = fork();
    if (pid == 0) {
        udp_lb_daemon();
        exit(0);
    }

    log_info("Forking echo server running on port %d...", TCP_PORT);

    while(1) {
        int client_sock = accept(sd, (struct sockaddr *)&pin, (socklen_t*)&addrlen);
        if (client_sock == -1) {
            log_err("accept");
            continue;
        }

        log_success("New client connected");

        pid = fork();
        if (pid == -1) {
            log_err("fork");
            close(client_sock);
            continue;
        }

        if (pid == 0) {
            close(sd);
            handle_client(client_sock);
        } else {
            close(client_sock);
        }
    }

    close(sd);
    return 0;
}
