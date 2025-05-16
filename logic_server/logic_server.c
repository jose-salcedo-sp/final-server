#include "logic_server.h"
int sd;  /* socket descriptor for main listening socket */
int udp_sd;  // UDP socket

/* Signal handler for graceful shutdown */
void abort_handler(int sig) {
    printf("Shutting down server...\n");
    close(sd);
    close(udp_sd);
    exit(0);
}

/* Signal handler to reap zombie processes */
void sigchld_handler(int sig) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

/* Function to handle client communication in child process */
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
            return strdup(raw_json);  // Forward original request to DB

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

    // Connect to DB server
    int db_sock = connect_to_db(DB_IP, DB_PORT);  // use actual DB address/port
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

        // Client -> Logic
        if (fds[0].revents & POLLIN) {
            bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) break;
            buffer[bytes_received] = '\0';

            log_info("Received JSON from client: %s", buffer);

            // (Optional) parse and validate JSON before forwarding to DB
			bool handled_locally = false;
			char *response = process_client_request(buffer, db_sock, &handled_locally);
		
			if (handled_locally) {
				send(client_sock, response, strlen(response), 0);
			} else {
				write(db_sock, response, strlen(response));
			}
		
			free(response);
        }

        // DB -> Logic
        if (fds[1].revents & POLLIN) {
            bytes_received = recv(db_sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) break;
            buffer[bytes_received] = '\0';

            log_info("Received response from DB: %s", buffer);

            // Send response back to client
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

    /* Set up signal handlers */
    if (signal(SIGINT, abort_handler) == SIG_ERR) {
        log_err("Could not set SIGINT handler");
        return 1;
    }

    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) {
        log_err("Could not set SIGCHLD handler");
        return 1;
    }

    /* Create internet socket */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        log_err("socket");
        return 1;
    }
    
    // Create socket (UDP)
    if ((udp_sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        log_err("socket UDP");
        close(sd);
        return 1;
    }

    /* Set up address structure */
    memset(&sind, 0, sizeof(sind));
    sind.sin_family = AF_INET;
    sind.sin_addr.s_addr = INADDR_ANY;
    sind.sin_port = htons(PORT);

    /* Bind socket to port */
    if (bind(sd, (struct sockaddr *)&sind, sizeof(sind)) == -1) {
        log_err("bind");
        close(sd);
        close(udp_sd);
        return 1;
    }

    // Bind UDP to the same port
    if (bind(udp_sd, (struct sockaddr *)&sind, sizeof(sind)) == -1) {
        log_err("bind UDP");
        close(sd);
        close(udp_sd);
        return 1;
    }

    /* Start listening */
    if (listen(sd, 5) == -1) {
        log_err("listen");
        close(sd);
        close(udp_sd);
        return 1;
    }

    log_info("Forking echo server running on port %d (TCP+UDP)…", PORT);

    struct pollfd fds[2];
    fds[0].fd     = sd;
    fds[0].events = POLLIN;      // new TCP connection
    fds[1].fd     = udp_sd;
    fds[1].events = POLLIN;      // incoming UDP datagram

    while(1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            log_err("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            /* Wait for client connection */
            int client_sock = accept(sd, (struct sockaddr *)&pin, (socklen_t*)&addrlen);
            if (client_sock == -1) {
                log_err("accept");
            } else {
                log_success("New client connected");

                /* Fork a new process to handle this client */
                pid = fork();
                if (pid < 0) {
                    log_err("fork");
                    close(client_sock);
                } else if (pid == 0) {  /* Child process */
                    close(sd);  /* Close listening socket in child */
                    close(udp_sd);    // child doesn't need the UDP listener
                    handle_client(client_sock);
                } else {  /* Parent process */
                    close(client_sock);  /* Close client socket in parent */
                }
            }
        }

        /* ——— Paquete UDP entrante ——— */
        if (fds[1].revents & POLLIN) {
            char buf[BUFFER_SIZE];
            struct sockaddr_in cli;
            socklen_t clen = sizeof(cli);
            int n = recvfrom(udp_sd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&cli, &clen);
            if (n < 0) {
                log_err("recvfrom UDP");
            } else {
                buf[n] = '\0';
                log_info("UDP recv: %s", buf);

                /* Process JSON like TCP, use process_client_request */
                bool handled_locally = false;
                char *resp = process_client_request(buf, -1, &handled_locally);

                if (handled_locally) {
                    sendto(udp_sd, resp, strlen(resp), 0, (struct sockaddr*)&cli, clen);
                } else {
                    /* Option: resend to DB (not implemented) */
                    log_warn("UDP action needs forwarding but is unsupported");
                }
                free(resp);
            }
        }
    }

    /* Clean up (though we won't normally reach here) */
    close(sd);
    close(udp_sd);
    return 0;
}
