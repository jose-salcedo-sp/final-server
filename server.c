#include "dbg.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void handle_client(int client_fd) {
  char msg[] = "Hello world\n";
  send(client_fd, msg, strlen(msg), 0);
  close(client_fd);
  exit(0); // Child exits after handling the client
}

int main() {
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
    client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (client_fd == -1) {
      perror("accept failed");
      continue;
    }

    pid_t pid = fork();

    if (pid == 0) {     // Child process
      close(server_fd); // Child doesn't need the listening socket
      handle_client(client_fd);
    } else if (pid > 0) { // Parent process
      close(client_fd);   // Parent doesn't need this client socket
      log_info("Created child process %d for new connection", pid);

      // Clean up any finished child processes (prevent zombies)
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
