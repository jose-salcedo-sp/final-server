#include "../dbg.h"
#include "../lib/queue/queue.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024

typedef struct Request {
  int client_fd;                  // To identify the requester
  struct sockaddr_in client_addr; // Store client's address info
  char data[BUFFER_SIZE];         // Request data (can expand this)
} Request;

void enqueue_request(int client_fd, struct sockaddr_in client_addr,
                     const char *data, queue *q) {
  Request *new_request = (Request *)malloc(sizeof(Request));
  if (!new_request) {
    perror("Failed to allocate memory for new request");
    return;
  }

  enqueue(q, new_request);
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
  log_info("Enqueued request from %s:%d\nQueue size: %zd", client_ip,
           ntohs(client_addr.sin_port), getSize(q));
}

void handle_client(queue *q) {
  Request req;
  dequeue(q, &req);

  char msg[] = "Hello world\n";

  send(req.client_fd, msg, strlen(msg), 0);
  close(req.client_fd);
  exit(0); // Child exits after handling the client
}

int main() {
  queue *request_queue = createQueue(sizeof(Request));
  int server_fd, client_fd;
  struct sockaddr_in address;
  int opt = 1;
  socklen_t addrlen = sizeof(address);

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
             sizeof(opt));

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 10) < 0) {
    perror("listen failed");
    exit(EXIT_FAILURE);
  }

  log_info("Server listening on port %d...\n", PORT);

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_fd == -1) {
      perror("accept failed");
      continue;
    }

    char buffer[BUFFER_SIZE] = {0};
    read(client_fd, buffer, BUFFER_SIZE);

    enqueue_request(client_fd, client_addr, buffer, request_queue);

    pid_t pid = fork();

    if (pid == 0) { // Child process
      close(server_fd);

      handle_client(request_queue);
    } else if (pid > 0) {
      close(client_fd);
      log_info("Created child process %d for new connection", pid);
      while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    } else {
      perror("fork failed");
      close(client_fd);
    }
  }

  close(server_fd); // Never actually reached
  return 0;
}
