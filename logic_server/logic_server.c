#include "logic_server.h"
int sd;  /* socket descriptor for main listening socket */

/* Signal handler for graceful shutdown */
void abort_handler(int sig) {
    printf("Shutting down server...\n");
    close(sd);
    exit(0);
}

/* Signal handler to reap zombie processes */
void sigchld_handler(int sig) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

/* Function to handle client communication in child process */
void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_received;

    while (1) {
        bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                log_info("Client disconnected\n");
            } else {
                log_err("recv failed");
            }
            break;
        }

        buffer[bytes_received] = '\0';  // Null-terminate the string

        log_info("Received raw JSON: %s", buffer);

        // Parse JSON
        cJSON *json = cJSON_Parse(buffer);
        if (!json) {
            log_warn("Received invalid JSON");
            continue;
        }

        // Extract action
        cJSON *action_json = cJSON_GetObjectItemCaseSensitive(json, "action");
        if (!cJSON_IsNumber(action_json)) {
            log_warn("Missing or invalid 'action' field");
            cJSON_Delete(json);
            continue;
        }

		ACTIONS action = (ACTIONS)action_json->valueint; 

        switch (action) {
			case PING:  // Example: PING
                log_info("Action: PING from client");
                send(client_sock, "{\"response:pong\"}", 17, 0);
                break;

            case CHATJOIN:  // Example: JOIN_CHAT
            {
                cJSON *chat_id = cJSON_GetObjectItemCaseSensitive(json, "chat_id");
                if (!cJSON_IsString(chat_id)) {
                    log_warn("Missing or invalid 'chat_id'");
                    break;
                }
                log_info("Client requested to join chat %s", chat_id->valuestring);
                // You won't implement rooms, but you could acknowledge it
                send(client_sock, "{\"response\":\"joined\"}", 23, 0);
                break;
            }

            default:
                log_warn("Unknown action: %d", action);
                send(client_sock, "{\"error\":\"unknown action\"}", 27, 0);
                break;
        }

        cJSON_Delete(json);
    }

    close(client_sock);
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

    /* Set up address structure */
    sind.sin_family = AF_INET;
    sind.sin_addr.s_addr = INADDR_ANY;
    sind.sin_port = htons(PORT);

    /* Bind socket to port */
    if (bind(sd, (struct sockaddr *)&sind, sizeof(sind)) == -1) {
        log_err("bind");
        close(sd);
        return 1;
    }

    /* Start listening */
    if (listen(sd, 5) == -1) {
        log_err("listen");
        close(sd);
        return 1;
    }

    log_info("Forking echo server running on port %d...", PORT);

    while(1) {
        /* Wait for client connection */
        int client_sock = accept(sd, (struct sockaddr *)&pin, (socklen_t*)&addrlen);
        if (client_sock == -1) {
            log_err("accept");
            continue;
        }

        log_success("New client connected");

        /* Fork a new process to handle this client */
        pid = fork();
        if (pid == -1) {
            log_err("fork");
            close(client_sock);
            continue;
        }

        if (pid == 0) {  /* Child process */
            close(sd);  /* Close listening socket in child */
            handle_client(client_sock);
        } else {  /* Parent process */
            close(client_sock);  /* Close client socket in parent */
        }
    }

    /* Clean up (though we won't normally reach here) */
    close(sd);
    return 0;
}
