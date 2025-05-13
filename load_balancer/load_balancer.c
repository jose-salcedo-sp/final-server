#include "load_balancer.h"

Chat *chats[MAX_CONCURRENT_CHATS] = {0}; // initialize
int active_chat_count = 0; // we start the server with 0 active chats

int subscribe_user_to_chat(pid_t pid, UUID chat_id) {
  int idx = -1;

  // Check if the chat already exists
  for (int i = 0; i < active_chat_count; i++) {
    if (strcmp(chats[i]->id, chat_id) == 0) {
      idx = i;
      break;
    }
  }

  // Create a new chat if not found
  if (idx == -1) {
    if (active_chat_count >= MAX_CONCURRENT_CHATS) {
      log_err("Max chat limit reached");
      return -1;
    }

    chats[active_chat_count] = calloc(1, sizeof(Chat));
    if (!chats[active_chat_count]) {
      log_err("Failed to allocate memory for new chat");
      return -1;
    }

    strncpy(chats[active_chat_count]->id, chat_id, UUIDv7_SIZE - 1);
    chats[active_chat_count]->id[UUIDv7_SIZE - 1] = '\0';
    idx = active_chat_count++;
  }

  Chat *chat = chats[idx];

  // Check for duplicate subscription
  for (int i = 0; i < chat->user_count; i++) {
    if (chat->users[i] == pid) {
      return idx; // already subscribed
    }
  }

  if (chat->user_count >= MAX_USERS_PER_CHAT) {
    log_warn("Chat %s full, can't add user %d", chat_id, pid);
    return -1;
  }

  chat->users[chat->user_count++] = pid;

  log_info("User %d subscribed to chat %s (index %d)", pid, chat_id, idx);
  return idx;
}

int unsubscribe_user_to_chat(pid_t pid, UUID chat_id) {
  for (int i = 0; i < active_chat_count; i++) {
    Chat *chat = chats[i];

    if (strcmp(chat->id, chat_id) == 0) {
      // Find and remove the user
      int found = 0;
      for (int j = 0; j < chat->user_count; j++) {
        if (chat->users[j] == pid) {
          found = 1;
          // Shift users to remove the pid
          for (int k = j; k < chat->user_count - 1; k++) {
            chat->users[k] = chat->users[k + 1];
          }
          chat->user_count--;
          break;
        }
      }

      if (!found) {
        log_warn("User %d not found in chat %s", pid, chat_id);
        return -1;
      }

      log_info("User %d unsubscribed from chat %s", pid, chat_id);

      // If chat is empty, remove it
      if (chat->user_count == 0) {
        log_info("Chat %s is now empty. Removing.", chat_id);
        free(chat);
        // Shift chats array
        for (int k = i; k < active_chat_count - 1; k++) {
          chats[k] = chats[k + 1];
        }
        chats[--active_chat_count] = NULL;
      }

      return 0;
    }
  }

  log_warn("Chat %s not found while unsubscribing user %d", chat_id, pid);
  return -1;
}

// { "action": 5, "chat_id": "anychatdoesntmatter" }

void handle_client_request(char *bytes, pid_t pid) {
  cJSON *req = cJSON_Parse(bytes);
  if (!req) {
    log_warn("Received malformed JSON from pid %d", pid);
    return;
  }

  cJSON *action_json = cJSON_GetObjectItemCaseSensitive(req, "action");
  ACTIONS action = -1;

  if (cJSON_IsNumber(action_json))
    action = action_json->valueint;

  switch (action) {
  case CHATJOIN: {
    cJSON *chat_id = cJSON_GetObjectItemCaseSensitive(req, "chat_id");

    if (cJSON_IsString(chat_id) && chat_id->valuestring != NULL) {
      log_info("User trying to join chat %s", chat_id->valuestring);
      subscribe_user_to_chat(pid, chat_id->valuestring);
    } else {
      log_warn("Invalid or missing chat_id in JOIN request");
    }
    break;
  }
  // handle other actions similarly
  default:
    log_warn("Action %d not implemented", action);
    break;
  }

  cJSON_Delete(req);
}

void handle_connection(int client_fd, char *backend_host,
                       char *backend_port_str, pid_t pid) {
  struct addrinfo hints = {0}, *addrs = NULL, *addr_iter = NULL;
  int backend_fd = -1;
  char buffer[BUFFER_SIZE];
  int bytes_read;

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(backend_host, backend_port_str, &hints, &addrs) != 0) {
    perror("getaddrinfo");
    exit(1);
  }

  for (addr_iter = addrs; addr_iter != NULL; addr_iter = addr_iter->ai_next) {
    backend_fd = socket(addr_iter->ai_family, addr_iter->ai_socktype,
                        addr_iter->ai_protocol);
    if (backend_fd == -1)
      continue;

    if (connect(backend_fd, addr_iter->ai_addr, addr_iter->ai_addrlen) == 0) {
      break;
    }

    close(backend_fd);
    backend_fd = -1;
  }

  freeaddrinfo(addrs);

  if (backend_fd == -1) {
    perror("Failed to connect to backend");
    exit(1);
  }

  struct pollfd fds[2];
  fds[0].fd = client_fd;
  fds[0].events = POLLIN;

  fds[1].fd = backend_fd;
  fds[1].events = POLLIN;

  while (1) {
    int ret = poll(fds, 2, -1); // block indefinitely

    if (ret < 0) {
      perror("poll");
      break;
    }

    // Client -> Backend
    if (fds[0].revents & POLLIN) {
      bytes_read = read(client_fd, buffer, BUFFER_SIZE);
      if (bytes_read <= 0)
        break;

      buffer[bytes_read] = '\0'; // null-terminate for string safety
      handle_client_request(buffer, pid);

      write(backend_fd, buffer, bytes_read);
    }

    // Backend -> Client
    if (fds[1].revents & POLLIN) {
      bytes_read = read(backend_fd, buffer, BUFFER_SIZE);
      if (bytes_read <= 0)
        break;
      write(client_fd, buffer, bytes_read);
    }
  }

  close(client_fd);
  close(backend_fd);
}

void reap_dead_children(int signum) {
  (void)signum;
  while (waitpid(-1, NULL, WNOHANG) > 0) {
  }
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <server_port> <backend_host> <backend_port>\n",
            argv[0]);
    return 1;
  }

  char *server_port_str = argv[1];
  char *backend_host = argv[2];
  char *backend_port_str = argv[3];

  struct addrinfo hints = {0}, *addrs = NULL, *addr_iter = NULL;
  int server_socket_fd, client_socket_fd, so_reuseaddr = 1;

  // Avoid zombie processes
  struct sigaction sa;
  sa.sa_handler = reap_dead_children;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sa, NULL);

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo(NULL, server_port_str, &hints, &addrs) != 0) {
    log_err("Could not bind to port");
    return 1;
  }

  for (addr_iter = addrs; addr_iter != NULL; addr_iter = addr_iter->ai_next) {
    server_socket_fd = socket(addr_iter->ai_family, addr_iter->ai_socktype,
                              addr_iter->ai_protocol);
    if (server_socket_fd == -1)
      continue;

    setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr,
               sizeof(so_reuseaddr));

    if (bind(server_socket_fd, addr_iter->ai_addr, addr_iter->ai_addrlen) == 0)
      break;

    close(server_socket_fd);
  }

  freeaddrinfo(addrs);

  if (addr_iter == NULL) {
    log_err("Failed to bind server socket");
    return 1;
  }

  listen(server_socket_fd, SOMAXCONN);
  log_info("Proxy listening on port %s", server_port_str);

  while (1) {
    client_socket_fd = accept(server_socket_fd, NULL, NULL);
    if (client_socket_fd == -1)
      continue;

    pid_t pid = fork();

    if (pid == 0) { // child process
      close(server_socket_fd);
      handle_connection(client_socket_fd, backend_host, backend_port_str,
                        getpid());
    } else if (pid > 0) { // parent process
      log_info("Forked child process %d", pid);
      close(client_socket_fd);
    } else {
      log_err("Fork failed");
    }
  }

  close(server_socket_fd);
  return 0;
}
