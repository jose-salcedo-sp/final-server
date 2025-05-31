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
static const char *HMAC_SECRET = "mi_secreto_super_fuerte";

const ErrorResponse ERROR_INVALID_JSON = {400, "Invalid JSON"};
const ErrorResponse ERROR_MISSING_ACTION = {400, "Missing or invalid action"};
const ErrorResponse ERROR_MISSING_FIELDS = {400, "Missing required fields for this action"};
const ErrorResponse ERROR_MISSING_TOKEN = {401, "Unauthorized: token missing"};
const ErrorResponse ERROR_INVALID_TOKEN = {401, "Unauthorized: invalid token"};
const ErrorResponse ERROR_UNKNOWN_ACTION = {404, "Unknown action"};
const ErrorResponse ERROR_DB_UNAVAILABLE = {503, "Service unavailable: all DB load balancers are unreachable"};

const ActionValidation validation_rules[] = {
    {VALIDATE_USER, {"key", "password", NULL}},
    {CREATE_USER, {"username", "email", "password", NULL}},
    {GET_USER_INFO, {"key", NULL}},
    {CREATE_CHAT, {"is_group", "chat_name", "created_by", "participant_ids", NULL}},
    {ADD_TO_GROUP_CHAT, {"chat_id", "added_by", "participant_ids", NULL}},
    {SEND_MESSAGE, {"chat_id", "sender_id", "content", "message_type", NULL}},
    {GET_CHATS, {"user_id", "last_update_timestamp", NULL}},
    {GET_CHAT_MESSAGES, {"chat_id", "last_update_timestamp", NULL}},
    {PING, {NULL}}
};


void abort_handler(int sig) {
    printf("Shutting down server...\n");
    close(sd);
    close(udp_sd);
    exit(0);
}

void sigchld_handler(int sig) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

char* create_error_response(ErrorResponse error) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "response_code", error.code);
    cJSON_AddStringToObject(json, "response_text", error.text);
    char *result = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return result;
}

bool validate_request(ACTIONS action, cJSON *json) {
    // Find validation rules for this action
    const ActionValidation *rules = NULL;
    for (size_t i = 0; i < sizeof(validation_rules)/sizeof(validation_rules[0]); i++) {
        if (validation_rules[i].action == action) {
            rules = &validation_rules[i];
            break;
        }
    }
    
    if (!rules) return false; // No rules defined for this action
    
    // Check required fields
    for (int i = 0; rules->required_fields[i] != NULL; i++) {
        if (!cJSON_HasObjectItem(json, rules->required_fields[i])) {
            return false;
        }
    }
    
    return true;
}

char* process_client_request(const char *raw_json, int backend_fd, bool *handled_locally) {
    cJSON *json = cJSON_Parse(raw_json);
    if (!json) {
        log_warn("Invalid JSON from client");
        *handled_locally = true;
		return create_error_response(ERROR_INVALID_JSON);
    }

    cJSON *action_json = cJSON_GetObjectItemCaseSensitive(json, "action");
    if (!cJSON_IsNumber(action_json)) {
        log_warn("Missing or invalid 'action'");
        cJSON_Delete(json);
        *handled_locally = true;
		return create_error_response(ERROR_MISSING_ACTION);
    }

    ACTIONS action = (ACTIONS)action_json->valueint;
	// After getting the action but before processing:
	if (!validate_request(action, json)) {
		cJSON_Delete(json);
		*handled_locally = true;
		return create_error_response(ERROR_MISSING_FIELDS);
	}

	if(action == PING || action == VALIDATE_USER || action == CREATE_USER){
		switch (action) {
			case PING:
				log_info("Handling PING locally");
				*handled_locally = true;
				cJSON_Delete(json);
				return strdup("{\"response\":\"pong\"}");

                //LOGIN UwU
			case VALIDATE_USER:
                // 1. Extraer nombre de usuario y contraseña del JSON recibido del cliente
                cJSON *username = cJSON_GetObjectItem(json, "key");
                cJSON *password = cJSON_GetObjectItem(json, "password");
                
                // 2. Crear un nuevo JSON para hacer la consulta a la base de datos
                cJSON *db_query = cJSON_CreateObject();
                cJSON_AddNumberToObject(db_query, "action", GET_USER_AUTH_DATA);
                cJSON_AddStringToObject(db_query, "username", username->valuestring);
                
                // 3. Serializar el JSON y enviarlo al backend
                char *db_request = cJSON_PrintUnformatted(db_query);
                write(backend_fd, db_request, strlen(db_request));
                free(db_request);
                cJSON_Delete(db_query);
                
                // 4. Esperar la respuesta del backend
                char db_response[BUFFER_SIZE];
                int len = read(backend_fd, db_response, BUFFER_SIZE - 1);
                db_response[len] = '\0';
                
                // 5. Parsear la respuesta de la base de datos
                cJSON *db_json = cJSON_Parse(db_response);
                if (!db_json) {
                    cJSON_Delete(json);
                    return create_error_response((ErrorResponse){500, "Invalid DB response"});
                }
                
                cJSON *db_password = cJSON_GetObjectItem(db_json, "password");
                if (!db_password || !cJSON_IsString(db_password)) {
                    cJSON_Delete(db_json);
                    return create_error_response((ErrorResponse){401, "Invalid credentials"});
                }
                
                // 6. Comparación DIRECTA de contraseñas 
                if (strcmp(password->valuestring, db_password->valuestring) == 0) {
                    // 6.a. Si coinciden, generar token
                    int user_id = cJSON_GetObjectItem(db_json, "id")->valueint;
                    char *token = create_token(user_id);
                    
                    // 6.b. Crear respuesta
                    cJSON *response = cJSON_CreateObject();
                    cJSON_AddStringToObject(response, "status", "ok");
                    cJSON_AddStringToObject(response, "token", token);
                    char *response_str = cJSON_PrintUnformatted(response);
                    
                    // Limpiar
                    cJSON_Delete(db_json);
                    cJSON_Delete(response);
                    free(token);
                    return response_str;
                } else {
                    // 6.c. Si no coinciden
                    cJSON_Delete(db_json);
                    return create_error_response((ErrorResponse){401, "Invalid credentials"});
                }
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
			return create_error_response(ERROR_MISSING_TOKEN);
		}

		int user_id;
		if (!validate_token(token_json->valuestring, &user_id)) {
			log_warn("Invalid or expired token");
			cJSON_Delete(json);
			*handled_locally = true;
			return create_error_response(ERROR_INVALID_TOKEN);
		}

		log_info("Token validated. User ID: %d", user_id);
		cJSON_ReplaceItemInObject(json, "user_id", cJSON_CreateNumber(user_id));
        cJSON_DeleteItemFromObject(json, "token");

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
	return create_error_response(ERROR_MISSING_ACTION);
}

int connect_to_db_balancers(const char **hosts, const int *ports, int count) {
    struct addrinfo hints = {0}, *res, *rp;
    char port_str[6];
    int sock;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    for (int i = 0; i < count; i++) {
        snprintf(port_str, sizeof(port_str), "%d", ports[i]);

        if (getaddrinfo(hosts[i], port_str, &hints, &res) != 0) {
            log_warn("getaddrinfo failed for %s:%s", hosts[i], port_str);
            continue;
        }

        for (rp = res; rp != NULL; rp = rp->ai_next) {
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sock == -1) continue;

            if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
                freeaddrinfo(res);
                log_info("Connected to DB LB at %s:%s", hosts[i], port_str);
                return sock;
            }

            close(sock);
        }

        freeaddrinfo(res);
        log_warn("Failed to connect to DB LB at %s:%s", hosts[i], port_str);
    }

    log_err("All DB load balancers are unreachable");
    return -1;
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_received;

    int db_sock = connect_to_db_balancers(db_ips, db_ports_tcp, LB_COUNT);
    if (db_sock < 0) {
        log_err("Failed to connect to DB");
        const char *error_json = "{ \"response_code\": 503, \"response_text\": \"Service unavailable: all DB load balancers are unreachable\" }";
        send(client_sock, error_json, strlen(error_json), 0);
        close(client_sock);
        exit(1);
    }

    // Configurar timeout de 5 segundos para el socket de DB
    struct timeval tv;
    tv.tv_sec = 15;  // Timeout en segundos
    tv.tv_usec = 0;
    setsockopt(db_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(db_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

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
            if (bytes_received <= 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    log_warn("DB response timeout");
                    send(client_sock, create_error_response(ERROR_DB_UNAVAILABLE), 
                         strlen(create_error_response(ERROR_DB_UNAVAILABLE)), 0);
                }
                break;
            }
            buffer[bytes_received] = '\0';

            log_info("Received response from DB: %s", buffer);

            // Si es LOGIN y fue exitoso, inyectamos token
            cJSON *json = cJSON_Parse(buffer);
            if (json) {
                cJSON *act = cJSON_GetObjectItem(json, "action");
                cJSON *status = cJSON_GetObjectItem(json, "status");
                if (act && act->valueint == CREATE_USER && status &&
                    strcmp(status->valuestring, "ok") == 0) {

                    int user_id = 42; // Simulado, idealmente viene del DB
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
bool validate_token(const char *jwt_str, int *out_user_id) {
  jwt_t *jwt = NULL;
  if (jwt_decode(&jwt, jwt_str, (unsigned char*)HMAC_SECRET, strlen(HMAC_SECRET))) {
    return false;    // firma inválida o expirado
  }
  // extraer claim user_id
  *out_user_id = (int)jwt_get_grant_int(jwt, "user_id");
  jwt_free(jwt);
  return true;
}

// Stub de creación de token
char *create_token(int user_id) {
  jwt_t *jwt = NULL;
  jwt_new(&jwt);
  jwt_add_grant_int(jwt, "user_id", user_id);
  // expiración en 1 hora:
  jwt_add_grant_int(jwt, "exp", time(NULL) + 3600);
  // firma HMAC-SHA256:
  jwt_set_alg(jwt, JWT_ALG_HS256, (unsigned char*)HMAC_SECRET, strlen(HMAC_SECRET));
  char *token = jwt_encode_str(jwt);
  jwt_free(jwt);
  return token;
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
