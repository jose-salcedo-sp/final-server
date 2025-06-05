#include "logic_server.h"
int sd;
int udp_sd;

const char *db_ips[LB_COUNT] = { "10.7.11.159"};
const char *client_ips[LB_COUNT] = { "10.7.11.159"};
int db_ports_udp[LB_COUNT]= {5001};
int client_ports_udp[LB_COUNT]= {5000};
int db_ports_tcp[LB_COUNT]= {3001};
int client_ports_tcp[LB_COUNT]= {3000}; 
const char *string_tcp_addr = MAKE_ADDR(IP, TCP_PORT);
const char *string_udp_addr = MAKE_ADDR(IP, UDP_PORT);
static const char *HMAC_SECRET = "mi_secreto_super_fuerte";
static CurrentRequest current_request = {0};

const ErrorResponse ERROR_INVALID_JSON = {400, "Invalid JSON"};
const ErrorResponse ERROR_MISSING_ACTION = {400, "Missing or invalid action"};
const ErrorResponse ERROR_MISSING_FIELDS = {400, "Missing required fields for this action"};
const ErrorResponse ERROR_MISSING_TOKEN = {401, "Unauthorized: token missing"};
const ErrorResponse ERROR_INVALID_TOKEN = {401, "Unauthorized: invalid token"};
const ErrorResponse ERROR_UNKNOWN_ACTION = {404, "Unknown action"};
const ErrorResponse ERROR_DB_UNAVAILABLE = {503, "Service unavailable: all DB load balancers are unreachable"};
const ErrorResponse ERROR_DB_CONNECTION = {500, "DB connection error"};
const ErrorResponse ERROR_INVALID_DB_RESPONSE = {500, "Invalid DB response"};
const ErrorResponse ERROR_INVALID_CREDENTIALS = {401, "Invalid credentials"};

const ActionValidation validation_rules[] = {
    {VALIDATE_USER, {"key", "password", NULL}},
    {CREATE_USER, {"username", "email", "password", NULL}},
    {GET_USER_INFO, {"key", NULL}},
    {CREATE_CHAT, {"is_group", "chat_name", "participant_ids", NULL}},
    {ADD_TO_GROUP_CHAT, {"chat_id", "participant_ids", NULL}},
    {SEND_MESSAGE, {"chat_id", "content", "message_type", NULL}},
    {GET_CHATS, {"last_update_timestamp", NULL}},
    {GET_CHAT_MESSAGES, {"chat_id", "last_update_timestamp", NULL}},
    {GET_CHAT_INFO, {"chat_id", NULL}},
    {REMOVE_FROM_CHAT, {"chat_id", "participant_ids", NULL}},
    {EXIT_CHAT, {"chat_id", NULL}},
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

// Function to decrypt using Cesar cipher
void cesar_decrypt(char *text) {
    if (!text) return;
    
    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        
        // Only process printable ASCII characters (32-126)
        if (c >= 32 && c <= 126) {
            // Shift backward in the printable ASCII range
            int shifted = c - CESAR_SHIFT;
            if (shifted < 32) {
                shifted += 95; // 95 = range of printable characters (126-32+1)
            }
            text[i] = (char)shifted;
        }
        // Other characters remain unchanged
    }
}

// Function to encrypt using Cesar cipher
void cesar_encrypt(char *text) {
    if (!text) return;
    
    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        
        // Only process printable ASCII characters (32-126)
        if (c >= 32 && c <= 126) {
            // Shift forward in the printable ASCII range
            int shifted = c + CESAR_SHIFT;
            if (shifted > 126) {
                shifted -= 95; // 95 = range of printable characters
            }
            text[i] = (char)shifted;
        }
        // Other characters remain unchanged
    }
}

// Helper function to encrypt and send response
void send_encrypted_response(int sock, const char *response) {
    char *encrypted = strdup(response);
    cesar_encrypt(encrypted);
    send(sock, encrypted, strlen(encrypted), 0);
    free(encrypted);
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
    
    // Validar campos requeridos
    if (!validate_request(action, json)) {
        cJSON_Delete(json);
        *handled_locally = true;
        return create_error_response(ERROR_MISSING_FIELDS);
    }

// Actions that don't require token
	if (action == PING || action == VALIDATE_USER || action == CREATE_USER) {
        switch (action) {
            case PING:
                log_info("Handling PING locally");
                *handled_locally = true;
                cJSON_Delete(json);
                return strdup("{\"response\":\"pong\"}");

            case VALIDATE_USER: {
                log_info("Handling VALIDATE_USER");
                
                // Clear any previous request
                if (current_request.request_json) {
                    cJSON_Delete(current_request.request_json);
                }
                
                // Store current request
                current_request.action = VALIDATE_USER;
                current_request.request_json = cJSON_Duplicate(json, 1);

                // Create DB query (without password)
                cJSON *db_query = cJSON_CreateObject();
                cJSON_AddNumberToObject(db_query, "action", VALIDATE_USER);
                cJSON *user_key = cJSON_GetObjectItem(json, "key");
                if (user_key) {
                    cJSON_AddStringToObject(db_query, "key", user_key->valuestring);
                }

                *handled_locally = false;
                char *out = cJSON_PrintUnformatted(db_query);
                cJSON_Delete(json);
                cJSON_Delete(db_query);
                return out;
            }

            case CREATE_USER: {
                *handled_locally = false;
                char *out = cJSON_PrintUnformatted(json);
                cJSON_Delete(json);
                return out;
            }

            default:
                break;
        }
    }
    
	else {
        // Acciones que SÍ requieren token
        cJSON *token_json = cJSON_GetObjectItemCaseSensitive(json, "token");
        if (!cJSON_IsString(token_json) || token_json->valuestring == NULL) {
            log_warn("Missing or invalid token");
            cJSON_Delete(json);
            *handled_locally = true;
            return create_error_response(ERROR_MISSING_TOKEN);
        }

        // Validar token y extraer user_id
        int user_id;
        if (!validate_token(token_json->valuestring, &user_id)) {
            log_warn("Invalid or expired token");
            cJSON_Delete(json);
            *handled_locally = true;
            return create_error_response(ERROR_INVALID_TOKEN);
        }

        log_info("Token validated. User ID: %d", user_id);
        
        // Remover token del JSON e inyectar user_id según la acción
        cJSON_DeleteItemFromObject(json, "token");
        
        switch (action) {
            case GET_USER_INFO:
                // GET_USER_INFO no necesita user_id según la documentación
                // Solo requiere "key": "username_or_email"
                // No inyectamos nada
                log_info("GET_USER_INFO: no injection needed");
                break;
                
            case CREATE_CHAT:
                // Para CREATE_CHAT, inyectar como "created_by"
                cJSON_ReplaceItemInObject(json, "created_by", cJSON_CreateNumber(user_id));
                log_info("CREATE_CHAT: injected created_by=%d", user_id);
                break;
                
            case ADD_TO_GROUP_CHAT:
                // Para ADD_TO_GROUP_CHAT, inyectar como "added_by"
                cJSON_ReplaceItemInObject(json, "added_by", cJSON_CreateNumber(user_id));
                log_info("ADD_TO_GROUP_CHAT: injected added_by=%d", user_id);
                break;
                
            case SEND_MESSAGE:
                // Para SEND_MESSAGE, inyectar como "sender_id"
                cJSON_ReplaceItemInObject(json, "sender_id", cJSON_CreateNumber(user_id));
                log_info("SEND_MESSAGE: injected sender_id=%d", user_id);
                break;
                
            case GET_CHATS:
                // Para GET_CHATS, inyectar como "user_id"
                cJSON_ReplaceItemInObject(json, "user_id", cJSON_CreateNumber(user_id));
                log_info("GET_CHATS: injected user_id=%d", user_id);// Handle NULL timestamp case
				cJSON *timestampChats = cJSON_GetObjectItem(json, "last_update_timestamp");
				if (timestampChats && cJSON_IsString(timestampChats)) {
					if (strcasecmp(timestampChats->valuestring, "NULL") == 0 || 
						strcasecmp(timestampChats->valuestring, "null") == 0) {
						cJSON_ReplaceItemInObject(json, "last_update_timestamp", cJSON_CreateNull());
						log_info("Converted NULL timestamp string to actual NULL");
					}
				}
                break;
                
            case GET_CHAT_MESSAGES:
                // GET_CHAT_MESSAGES no necesita user_id según la documentación
                // Solo requiere "chat_id" y opcionalmente "last_update_timestamp"
                // Pero podríamos inyectar user_id para validación de permisos en el backend
                cJSON_AddNumberToObject(json, "user_id", user_id);
                log_info("GET_CHAT_MESSAGES: injected user_id=%d for permission validation", user_id);
				cJSON *timestampMessages = cJSON_GetObjectItem(json, "last_update_timestamp");
				if (timestampMessages && cJSON_IsString(timestampMessages)) {
					if (strcasecmp(timestampMessages->valuestring, "NULL") == 0 || 
						strcasecmp(timestampMessages->valuestring, "null") == 0) {
						cJSON_ReplaceItemInObject(json, "last_update_timestamp", cJSON_CreateNull());
						log_info("Converted NULL timestamp string to actual NULL");
					}
				}
                break;
                
            case GET_CHAT_INFO:
                break;
			case REMOVE_FROM_CHAT:
                cJSON_AddNumberToObject(json, "removed_by", user_id);
                log_info("GET_CHAT_MESSAGES: injected user_id=%d for permission validation", user_id);
                break;
			case EXIT_CHAT:
                cJSON_AddNumberToObject(json, "user_id", user_id);
                log_info("GET_CHAT_MESSAGES: injected user_id=%d for permission validation", user_id);
                break;
            default:
                // Para acciones no especificadas, inyectar como "user_id" por defecto
                cJSON_AddNumberToObject(json, "user_id", user_id);
                log_info("Unknown action %d: injected user_id=%d", action, user_id);
                break;
        }

        // Reenviar al backend
        *handled_locally = false;
        char *forward_json = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
        return forward_json;
    }

    // No debería llegar aquí
    cJSON_Delete(json);
    *handled_locally = true;
    return create_error_response(ERROR_UNKNOWN_ACTION);
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

void handle_db_response(int client_sock, char* buffer, int bytes_received, struct pollfd *fds) {
    cJSON *db_json = cJSON_Parse(buffer);
    if (!db_json) {
        log_warn("Failed to parse DB response");
        char *error_response = create_error_response(ERROR_DB_CONNECTION);
		send_encrypted_response(client_sock, error_response);
        free(error_response);
        return;
    }

    // Get response code to check if request was successful
    cJSON *response_code = cJSON_GetObjectItem(db_json, "response_code");
    bool is_success = response_code && cJSON_IsNumber(response_code) && response_code->valueint == 200;

    // Check if we have a current request
    if (!current_request.request_json) {
        log_warn("No current request found for DB response");
        char *error_response = create_error_response(ERROR_DB_CONNECTION);
		send_encrypted_response(client_sock, error_response);
        free(error_response);
        cJSON_Delete(db_json);
        return;
    }

    // Handle VALIDATE_USER action with different states
    if (current_request.action == VALIDATE_USER) {
        switch (current_request.auth_state) {
            case AUTH_STATE_INITIAL: {
                // First step - validate credentials
                cJSON *client_password = cJSON_GetObjectItem(current_request.request_json, "password");
                cJSON *db_password = cJSON_GetObjectItem(db_json, "password_hash");

                // Validate credentials
                if (!is_success || !db_password || !cJSON_IsString(db_password) || 
                    !client_password || !cJSON_IsString(client_password)) {
                    log_warn("Authentication failed");
                    char *error_response = create_error_response(ERROR_INVALID_CREDENTIALS);
					send_encrypted_response(client_sock, error_response);
                    free(error_response);
                    break;
                }

                if (strcmp(client_password->valuestring, db_password->valuestring) != 0) {
                    log_warn("Password mismatch");
                    char *error_response = create_error_response(ERROR_INVALID_CREDENTIALS);
					send_encrypted_response(client_sock, error_response);
                    free(error_response);
                    break;
                }

                // Store username for next request
                cJSON *user_key = cJSON_GetObjectItem(current_request.request_json, "key");
                if (user_key && cJSON_IsString(user_key)) {
                    if (current_request.key) free(current_request.key);
                    current_request.key = strdup(user_key->valuestring);
                }

                // Close current DB connection
                if (current_request.current_db_sock >= 0) {
                    close(current_request.current_db_sock);
                }

                // Create new DB connection for GET_USER_INFO
                int new_db_sock = connect_to_db_balancers(db_ips, db_ports_tcp, LB_COUNT);
                if (new_db_sock < 0) {
                    log_err("Failed to connect to DB for user info");
                    char *error_response = create_error_response(ERROR_DB_UNAVAILABLE);
					send_encrypted_response(client_sock, error_response);
                    free(error_response);
                    break;
                }

                // Update current DB socket and pollfd
                current_request.current_db_sock = new_db_sock;
                fds[1].fd = new_db_sock;

                // Prepare GET_USER_INFO request
                cJSON *user_info_request = cJSON_CreateObject();
                cJSON_AddNumberToObject(user_info_request, "action", GET_USER_INFO);
                cJSON_AddStringToObject(user_info_request, "key", current_request.key);

                char *request_str = cJSON_PrintUnformatted(user_info_request);
                write(new_db_sock, request_str, strlen(request_str));
                
                free(request_str);
                cJSON_Delete(user_info_request);

                // Move to next state
                current_request.auth_state = AUTH_STATE_VALIDATED;
                break;
            }

            case AUTH_STATE_VALIDATED: {
                // Second step - get user info
                if (!is_success) {
                    log_warn("Failed to get user info");
                    char *error_response = create_error_response(ERROR_INVALID_CREDENTIALS);
					send_encrypted_response(client_sock, error_response);
                    free(error_response);
                    break;
                }

                cJSON *user_id_json = cJSON_GetObjectItem(db_json, "user_id");
                if (!user_id_json || !cJSON_IsNumber(user_id_json)) {
                    log_warn("User info response missing user_id");
                    char *error_response = create_error_response(ERROR_INVALID_CREDENTIALS);
					send_encrypted_response(client_sock, error_response);
                    free(error_response);
                    break;
                }

                int user_id = user_id_json->valueint;
                char *token = create_token(user_id);
                
                // Prepare final response
                cJSON *response = cJSON_CreateObject();
                cJSON_AddNumberToObject(response, "response_code", 200);
                cJSON_AddStringToObject(response, "response_text", "Authentication successful");
                cJSON_AddStringToObject(response, "token", token);
                cJSON_AddNumberToObject(response, "user_id", user_id);

                char *response_str = cJSON_PrintUnformatted(response);
				send_encrypted_response(client_sock, response_str);
                
                free(token);
                free(response_str);
                cJSON_Delete(response);

                // Clean up
                if (current_request.key) {
                    free(current_request.key);
                    current_request.key = NULL;
                }
                if (current_request.request_json) {
                    cJSON_Delete(current_request.request_json);
                    current_request.request_json = NULL;
                }
                current_request.action = 0;
                current_request.auth_state = AUTH_STATE_INITIAL;
                break;
            }

            default:
                break;
        }
    } else {
        // For all other actions, just forward the response as-is
        char *modified = cJSON_PrintUnformatted(db_json);
		send_encrypted_response(client_sock, modified);
        free(modified);
    }
    
    cJSON_Delete(db_json);
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_received;

    // Initialize current request
    current_request.current_db_sock = connect_to_db_balancers(db_ips, db_ports_tcp, LB_COUNT);
    if (current_request.current_db_sock < 0) {
        log_err("Failed to connect to DB");
        char *error_response = create_error_response(ERROR_DB_UNAVAILABLE);
		send_encrypted_response(client_sock, error_response);
        free(error_response);
        close(client_sock);
        exit(1);
    }

    // Configurar timeout de 15 segundos para el socket de DB
    struct timeval tv;
    tv.tv_sec = 15;  // Timeout en segundos
    tv.tv_usec = 0;
    setsockopt(current_request.current_db_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(current_request.current_db_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct pollfd fds[2];
    fds[0].fd = client_sock;
    fds[0].events = POLLIN;
    fds[1].fd = current_request.current_db_sock;
    fds[1].events = POLLIN;

    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            log_err("poll error");
            break;
        }

        // Datos del cliente
        if (fds[0].revents & POLLIN) {
            bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                log_info("Client disconnected");
                break;
            }
            buffer[bytes_received] = '\0';
			
			cesar_decrypt(buffer);

            log_info("Received JSON from client: %s", buffer);

            bool handled_locally = false;
            char *response = process_client_request(buffer, current_request.current_db_sock, &handled_locally);
        
            if (handled_locally) {
                // Respuesta manejada localmente
				send_encrypted_response(client_sock, response);
                log_info("Sent local response to client: %s", response);
                free(response);
            } else {
                // Reenviar al backend
                write(current_request.current_db_sock, response, strlen(response));
                log_info("Forwarded to DB: %s", response);
                free(response);
            }
        }

        // Datos del backend
        if (fds[1].revents & POLLIN) {
            bytes_received = recv(fds[1].fd, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    log_warn("DB response timeout");
                    char *error_response = create_error_response(ERROR_DB_UNAVAILABLE);
					send_encrypted_response(client_sock, error_response);
                    free(error_response);
                } else {
                    log_warn("DB connection lost");
                }
                break;
            }
            buffer[bytes_received] = '\0';

            log_info("Received response from DB: %s", buffer);

            // Pass the pollfd structure to handle_db_response
            handle_db_response(client_sock, buffer, bytes_received, fds);
        }
    }

    // Clean up
    if (current_request.current_db_sock >= 0) {
        close(current_request.current_db_sock);
    }
    if (current_request.key) {
        free(current_request.key);
    }
    if (current_request.request_json) {
        cJSON_Delete(current_request.request_json);
    }
    memset(&current_request, 0, sizeof(current_request));

    close(client_sock);
    log_info("Client handler process exiting");
    exit(0);
}

// Validación de token JWT
bool validate_token(const char *jwt_str, int *out_user_id) {
    jwt_t *jwt = NULL;
    if (jwt_decode(&jwt, jwt_str, (unsigned char*)HMAC_SECRET, strlen(HMAC_SECRET))) {
        log_warn("JWT decode failed");
        return false;    // firma inválida o formato incorrecto
    }
    
    // Verificar expiración
    time_t exp = jwt_get_grant_int(jwt, "exp");
    if (exp > 0 && time(NULL) > exp) {
        log_warn("JWT expired");
        jwt_free(jwt);
        return false;
    }
    
    // Extraer claim user_id
    *out_user_id = (int)jwt_get_grant_int(jwt, "user_id");
    jwt_free(jwt);
    
    if (*out_user_id <= 0) {
        log_warn("Invalid user_id in JWT");
        return false;
    }
    
    return true;
}

// Creación de token JWT
char *create_token(int user_id) {
    jwt_t *jwt = NULL;
    jwt_new(&jwt);
    jwt_add_grant_int(jwt, "user_id", user_id);
    // Expiración en 1 hora:
    jwt_add_grant_int(jwt, "exp", time(NULL) + 3600);
    // Firma HMAC-SHA256:
    jwt_set_alg(jwt, JWT_ALG_HS256, (unsigned char*)HMAC_SECRET, strlen(HMAC_SECRET));
    char *token = jwt_encode_str(jwt);
    jwt_free(jwt);
    return token;
}

int main() {
    struct sockaddr_in sind, pin;
    int addrlen = sizeof(pin);
    pid_t pid;

    log_info("Starting Logic Server...");

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

    // Permitir reutilización del puerto
    int opt = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_warn("setsockopt SO_REUSEADDR failed");
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

    // Fork para el daemon UDP
    pid = fork();
    if (pid == 0) {
        log_info("Starting UDP LB daemon");
        udp_lb_daemon();
        exit(0);
    } else if (pid < 0) {
        log_err("fork for UDP daemon");
        close(sd);
        return 1;
    }

    log_info("Logic server running on TCP port %d...", TCP_PORT);

    while(1) {
        int client_sock = accept(sd, (struct sockaddr *)&pin, (socklen_t*)&addrlen);
        if (client_sock == -1) {
            if (errno == EINTR) continue; // Interrupted by signal
            log_err("accept");
            continue;
        }

        log_success("New client connected from %s:%d", 
                   inet_ntoa(pin.sin_addr), ntohs(pin.sin_port));

        pid = fork();
        if (pid == -1) {
            log_err("fork");
            close(client_sock);
            continue;
        }

        if (pid == 0) {
            // Proceso hijo - manejar cliente
            close(sd);
            handle_client(client_sock);
        } else {
            // Proceso padre - cerrar socket del cliente
            close(client_sock);
        }
    }

    close(sd);
    log_info("Logic server shutting down");
    return 0;
}
