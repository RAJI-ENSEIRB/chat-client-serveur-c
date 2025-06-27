#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include "common.h"

void echo_client(int sockfd) {
    char buff[MSG_LEN];
    int n;
    struct pollfd fds[2];

    // Configuration de poll()
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            perror("poll()");
            break;
        }

        if (fds[0].revents & POLLIN) {
            // Entrée utilisateur disponible
            memset(buff, 0, MSG_LEN);
            printf("Message: ");
            n = 0;
            while ((buff[n++] = getchar()) != '\n') {} // trailing '\n' will be sent

            // Vérifier si l'utilisateur veut quitter
            if (strncmp(buff, "/quit", 5) == 0) {
                printf("Déconnexion...\n");
                break;
            }

            // Envoi du message
            if (send(sockfd, buff, strlen(buff), 0) <= 0) {
                perror("send()");
                break;
            }
            printf("Message envoyé!\n");
        }

        if (fds[1].revents & POLLIN) {
            // Message reçu du serveur
            memset(buff, 0, MSG_LEN);
            if (recv(sockfd, buff, MSG_LEN, 0) <= 0) {
                printf("Serveur déconnecté\n");
                break;
            }
            printf("Reçu: %s", buff);
        }
    }
}

int handle_connect(const char *server_name, const char *server_port) {
    struct addrinfo hints, *result, *rp;
    int sfd;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(server_name, server_port, &hints, &result) != 0) {
        perror("getaddrinfo()");
        exit(EXIT_FAILURE);
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }
        close(sfd);
    }
    if (rp == NULL) {
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(result);
    return sfd;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd;
    sfd = handle_connect(argv[1], argv[2]);
    printf("Connecté au serveur %s:%s\n", argv[1], argv[2]);
    echo_client(sfd);
    close(sfd);
    return EXIT_SUCCESS;
} 