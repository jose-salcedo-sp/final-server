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
    
    // Validar campos requeridos
    if (!validate_request(action, json)) {
        cJSON_Delete(json);
        *handled_locally = true;
        return create_error_response(ERROR_MISSING_FIELDS);
    }

    // Acciones que NO requieren token
    if(action == PING || action == VALIDATE_USER || action == CREATE_USER) {
        switch (action) {
            case PING:
                log_info("Handling PING locally");
                *handled_locally = true;
                cJSON_Delete(json);
                return strdup("{\"response\":\"pong\"}");

            case VALIDATE_USER: {
                log_info("Handling VALIDATE_USER");
                *handled_locally = true;
                
                // 1. Extraer credenciales del cliente
                cJSON *username = cJSON_GetObjectItem(json, "key");
                cJSON *password = cJSON_GetObjectItem(json, "password");
                
                // 2. Crear consulta para obtener datos de autenticación
                cJSON *db_query = cJSON_CreateObject();
                cJSON_AddNumberToObject(db_query, "action", GET_USER_AUTH_DATA);
                cJSON_AddStringToObject(db_query, "username", username->valuestring);
                
                // 3. Enviar consulta al backend
                char *db_request = cJSON_PrintUnformatted(db_query);
                write(backend_fd, db_request, strlen(db_request));
                free(db_request);
                cJSON_Delete(db_query);
                
                // 4. Recibir respuesta del backend
                char db_response[BUFFER_SIZE];
                int len = read(backend_fd, db_response, BUFFER_SIZE - 1);
                if (len <= 0) {
                    cJSON_Delete(json);
                    return create_error_response(ERROR_DB_CONNECTION);
                }
                db_response[len] = '\0';
                
                // 5. Parsear respuesta de la DB
                cJSON *db_json = cJSON_Parse(db_response);
                if (!db_json) {
                    cJSON_Delete(json);
                    return create_error_response(ERROR_INVALID_DB_RESPONSE);
                }
                
                // Verificar si el usuario existe
                cJSON *db_password = cJSON_GetObjectItem(db_json, "password");
                cJSON *db_user_id = cJSON_GetObjectItem(db_json, "id");
                if (!db_password || !cJSON_IsString(db_password) || !db_user_id || !cJSON_IsNumber(db_user_id)) {
                    cJSON_Delete(db_json);
                    cJSON_Delete(json);
                    return create_error_response(ERROR_INVALID_CREDENTIALS);
                }
                
                // 6. Validar contraseña
                if (strcmp(password->valuestring, db_password->valuestring) == 0) {
                    // Contraseña correcta - generar token
                    int user_id = db_user_id->valueint;
                    char *token = create_token(user_id);
                    
                    // Crear respuesta con token y user_id
                    cJSON *response = cJSON_CreateObject();
                    cJSON_AddStringToObject(response, "status", "ok");
                    cJSON_AddStringToObject(response, "token", token);
                    cJSON_AddNumberToObject(response, "user_id", user_id);
                    
                    char *response_str = cJSON_PrintUnformatted(response);
                    
                    // Limpiar memoria
                    cJSON_Delete(db_json);
                    cJSON_Delete(json);
                    cJSON_Delete(response);
                    free(token);
                    
                    log_info("User %s authenticated successfully with ID %d", username->valuestring, user_id);
                    return response_str;
                } else {
                    // Contraseña incorrecta
                    log_warn("Invalid password for user %s", username->valuestring);
                    cJSON_Delete(db_json);
                    cJSON_Delete(json);
                    return create_error_response(ERROR_INVALID_CREDENTIALS);
                }
            }

            case CREATE_USER:
                log_info("Forwarding CREATE_USER to DB");
                *handled_locally = false;
                {
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
                // Para GET_USER_INFO, inyectar como "user_id" (si no viene ya)
                if (!cJSON_HasObjectItem(json, "user_id")) {
                    cJSON_AddNumberToObject(json, "user_id", user_id);
                }
                log_info("GET_USER_INFO: injected user_id=%d", user_id);
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
                log_info("GET_CHATS: injected user_id=%d", user_id);
                break;
                
            case GET_CHAT_MESSAGES:
                // Para GET_CHAT_MESSAGES, inyectar como "user_id"
                cJSON_ReplaceItemInObject(json, "user_id", cJSON_CreateNumber(user_id));
                log_info("GET_CHAT_MESSAGES: injected user_id=%d", user_id);
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

void handle_db_response(int client_sock, char* buffer, int bytes_received) {
    // Si es CREATE_USER y fue exitoso, inyectar token
    cJSON *json = cJSON_Parse(buffer);
    if (json) {
        cJSON *action = cJSON_GetObjectItem(json, "action");
        cJSON *status = cJSON_GetObjectItem(json, "status");
        
        if (action && action->valueint == CREATE_USER && status &&
            strcmp(status->valuestring, "ok") == 0) {
            
            // Obtener el user_id de la respuesta del DB
            cJSON *user_id_json = cJSON_GetObjectItem(json, "user_id");
            if (user_id_json && cJSON_IsNumber(user_id_json)) {
                int user_id = user_id_json->valueint;
                char *jwt = create_token(user_id);
                cJSON_AddStringToObject(json, "token", jwt);
                
                log_info("CREATE_USER successful: generated token for user_id=%d", user_id);
                
                char *modified = cJSON_PrintUnformatted(json);
                send(client_sock, modified, strlen(modified), 0);
                free(modified);
                free(jwt);
                cJSON_Delete(json);
                return;
            } else {
                log_warn("CREATE_USER response missing user_id");
            }
        }
        cJSON_Delete(json);
    }

    // Si no es CREATE_USER exitoso o no se pudo parsear, reenviar tal como viene
    send(client_sock, buffer, bytes_received, 0);
}

void handle_client(int client_sock) {
    char buffer[BUFFER_SIZE];
    int bytes_received;

    int db_sock = connect_to_db_balancers(db_ips, db_ports_tcp, LB_COUNT);
    if (db_sock < 0) {
        log_err("Failed to connect to DB");
        char *error_response = create_error_response(ERROR_DB_UNAVAILABLE);
        send(client_sock, error_response, strlen(error_response), 0);
        free(error_response);
        close(client_sock);
        exit(1);
    }

    // Configurar timeout de 15 segundos para el socket de DB
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

            log_info("Received JSON from client: %s", buffer);

            bool handled_locally = false;
            char *response = process_client_request(buffer, db_sock, &handled_locally);
        
            if (handled_locally) {
                // Respuesta manejada localmente
                send(client_sock, response, strlen(response), 0);
                log_info("Sent local response to client: %s", response);
                free(response);
            } else {
                // Reenviar al backend
                write(db_sock, response, strlen(response));
                log_info("Forwarded to DB: %s", response);
                free(response);
            }
        }

        // Datos del backend
        if (fds[1].revents & POLLIN) {
            bytes_received = recv(db_sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    log_warn("DB response timeout");
                    char *error_response = create_error_response(ERROR_DB_UNAVAILABLE);
                    send(client_sock, error_response, strlen(error_response), 0);
                    free(error_response);
                } else {
                    log_warn("DB connection lost");
                }
                break;
            }
            buffer[bytes_received] = '\0';

            log_info("Received response from DB: %s", buffer);

            // Manejar respuesta del DB (especialmente CREATE_USER)
            handle_db_response(client_sock, buffer, bytes_received);
        }
    }

    close(client_sock);
    close(db_sock);
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