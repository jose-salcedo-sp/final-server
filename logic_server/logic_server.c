#include "logic_server.h"
int sd;
int udp_sd;

const char *db_ips[LB_COUNT] = { "127.0.0.1"};
const char *client_ips[LB_COUNT] = { "127.0.0.1"};
int db_ports_udp[LB_COUNT]= {7070};
int client_ports_udp[LB_COUNT]= {7071};
int db_ports_tcp[LB_COUNT]= {6060};
int client_ports_tcp[LB_COUNT]= {6061};
const char *string_tcp_addr = MAKE_ADDR(IP, TCP_PORT);
const char *string_udp_addr = MAKE_ADDR(IP, UDP_PORT);

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

	if(action == PING || action == VALIDATE_USER || action == CREATE_USER){
		switch (action) {
			case PING:
				log_info("Handling PING locally");
				*handled_locally = true;
				cJSON_Delete(json);
				return strdup("{\"response\":\"pong\"}");

			case VALIDATE_USER:
			case CREATE_USER:
				log_info("Forwarding action %d to DB", action);
				*handled_locally = false;
				{
					char *out = cJSON_PrintUnformatted(json);
					cJSON_Delete(json);
					return out;
				}
			default:{}
		}
	}
	else{
		//Necessary token validation
		cJSON *token_json = cJSON_GetObjectItemCaseSensitive(json, "token");
		if (!cJSON_IsString(token_json) || token_json->valuestring == NULL) {
			log_warn("Missing or invalid token");
			cJSON_Delete(json);
			*handled_locally = true;
			return strdup("{\"error\":\"unauthorized: token missing\"}");
		}

		int user_id;
		if (!validate_token(token_json->valuestring, &user_id)) {
			log_warn("Invalid or expired token");
			cJSON_Delete(json);
			*handled_locally = true;
			return strdup("{\"error\":\"unauthorized: invalid token\"}");
		}

		log_info("Token validated. User ID: %d", user_id);
		//cJSON_ReplaceItemInObject(json, "user_id", cJSON_CreateNumber(user_id));
		switch (action) {
			case GET_USER_INFO:
			case CREATE_CHAT:
			case ADD_TO_GROUP_CHAT:
			case SEND_MESSAGE:
			case GET_CHATS:
			case GET_CHAT_MESSAGES:{
					*handled_locally = false;
					cJSON_DeleteItemFromObject(json, "token");
					char *forward_json = cJSON_PrintUnformatted(json);
					cJSON_Delete(json);
					return forward_json;
				}
			default:{}

		}

		*handled_locally = false;
		char *forward_json = cJSON_PrintUnformatted(json);
		cJSON_Delete(json);
		return forward_json;
	}
	*handled_locally = true;
	return strdup("{\"error\":\"missing or invalid action\"}");
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
                free(response);
            } else {
                write(db_sock, response, strlen(response));
                free(response);
            }
        }

        if (fds[1].revents & POLLIN) {
            bytes_received = recv(db_sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) break;
            buffer[bytes_received] = '\0';

            log_info("Received response from DB: %s", buffer);

            // Si es LOGIN y fue exitoso, inyectamos token
            cJSON *json = cJSON_Parse(buffer);
            if (json) {
                cJSON *act = cJSON_GetObjectItem(json, "action");
                cJSON *status = cJSON_GetObjectItem(json, "status");
                if (act && act->valueint == CREATE_USER && status &&
                    strcmp(status->valuestring, "ok") == 0) {

                    int user_id = 42; // ⚠️ Simulado, idealmente viene del DB
                    char *jwt = create_token(user_id);
                    cJSON_AddStringToObject(json, "token", jwt);
                    free(jwt);

                    char *modified = cJSON_PrintUnformatted(json);
                    send(client_sock, modified, strlen(modified), 0);
                    free(modified);
                    cJSON_Delete(json);
                    continue;
                }
                cJSON_Delete(json);
            }

            send(client_sock, buffer, bytes_received, 0);
        }
    }

    close(client_sock);
    close(db_sock);
    exit(0);
}

// Stub de validación de token
bool validate_token(const char *jwt, int *out_user_id) {
    *out_user_id = 42;  // Simulado
    return true;
}

// Stub de creación de token
char *create_token(int user_id) {
    return strdup("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...");
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
