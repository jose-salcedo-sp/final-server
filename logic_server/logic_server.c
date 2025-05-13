#include "../lib/cjson/cJSON.h"
#include <stdio.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#define  BUFSIZE   4096      /* maximum buffer size */
#define  PORT      6969     /* arbitrary port number */

int                  sd, sd_actual;  /* socket descriptors */
int                  addrlen;        /* address length */
struct sockaddr_in   sind, pin;      /* server and client addresses */

/* Signal handler for graceful shutdown */
void abort_handler(int sig) {
   printf("Shutting down server...\n");
   close(sd);  
   close(sd_actual); 
   exit(0);
}

int main() {
    char buffer[BUFSIZE];     /* input/output buffer */
    int bytes_received;

    /* Set up signal handler for Ctrl+C */
    if(signal(SIGINT, abort_handler) == SIG_ERR) {
        perror("Could not set signal handler");
        return 1;
    }

    /* Create internet socket */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return 1;
    }

    /* Set up address structure */
    sind.sin_family = AF_INET;
    sind.sin_addr.s_addr = INADDR_ANY;
    sind.sin_port = htons(PORT);

    /* Bind socket to port */
    if (bind(sd, (struct sockaddr *)&sind, sizeof(sind)) == -1) {
        perror("bind");
        close(sd);
        return 1;
    }

    /* Start listening */
    if (listen(sd, 5) == -1) {
        perror("listen");
        close(sd);
        return 1;
    }

    printf("Echo server running on port %d...\n", PORT);

    /* Main server loop */
    while(1) {
        addrlen = sizeof(pin);
        
        /* Wait for client connection */
        if ((sd_actual = accept(sd, (struct sockaddr *)&pin, (socklen_t*)&addrlen)) == -1) {
            perror("accept");
            continue;
        }

        printf("Client connected.\n");

        /* Receive message from client */
        bytes_received = recv(sd_actual, buffer, BUFSIZE, 0);
        if (bytes_received == -1) {
            perror("recv");
            close(sd_actual);
            continue;
        }

        /* Null-terminate the received data */
        buffer[bytes_received] = '\0';
        printf("Received: %s\n", buffer);

        /* Send the same message back */
        if (send(sd_actual, buffer, bytes_received, 0) == -1) {
            perror("send");
        }

        /* Close connection */
        close(sd_actual);
        printf("Connection closed.\n");
    }

    /* Clean up (though we won't normally reach here) */
    close(sd);
    return 0;
}
