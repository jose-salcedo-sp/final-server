#include "../lib/cjson/cJSON.h"
#include "../dbg.h"
#include <stdio.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>

#define  BUFSIZE   4096      /* maximum buffer size */
#define  PORT      6969     /* arbitrary port number */

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
    char buffer[BUFSIZE];
    int bytes_received;

    while(1) {
        bytes_received = recv(client_sock, buffer, BUFSIZE, 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                log_info("Client disconnected\n");
            } else {
                log_err("recv");
            }
            break;
        }

        /* Null-terminate the received data */
        buffer[bytes_received] = '\0';
        log_info("Received: %s", buffer);

        /* Send the same message back */
        if (send(client_sock, buffer, bytes_received, 0) == -1) {
            log_err("send");
            break;
        }
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
