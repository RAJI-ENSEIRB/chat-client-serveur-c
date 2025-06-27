#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <poll.h>

#include "common.h"
#define MAX_CLIENTS 10

typedef struct Client {
    int fd;
    struct sockaddr_in addr;
    struct Client *next;
} Client;

Client *clients = NULL;

void add_client(int fd, struct sockaddr_in addr) {
    Client *new_client = malloc(sizeof(Client));
    new_client->fd = fd;
    new_client->addr = addr;
    new_client->next = clients;
    clients = new_client;
    printf("Nouveau client ajouté: %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

void remove_client(int fd) {
    Client **pp = &clients;
    while (*pp && (*pp)->fd != fd) {
        pp = &(*pp)->next;
    }
    if (*pp) {
        Client *tmp = *pp;
        *pp = (*pp)->next;
        printf("Client retiré: %s:%d\n", inet_ntoa(tmp->addr.sin_addr), ntohs(tmp->addr.sin_port));
        free(tmp);
    }
}

void echo_server(int server_fd) {
    char buff[MSG_LEN];
    struct pollfd fds[MAX_CLIENTS + 1];
    int nfds = 1;

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    while (1) {
        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            perror("poll()");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd == server_fd) {
                    // Nouvelle connexion
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd < 0) {
                        perror("accept()");
                        continue;
                    }

                    printf("Nouvelle connexion de %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                    add_client(client_fd, client_addr);

                    if (nfds < MAX_CLIENTS + 1) {
                        fds[nfds].fd = client_fd;
                        fds[nfds].events = POLLIN;
                        nfds++;
                    } else {
                        fprintf(stderr, "Trop de clients, connexion refusée\n");
                        close(client_fd);
                        remove_client(client_fd);
                    }
                } else {
                    // Données reçues d'un client existant
                    int client_fd = fds[i].fd;
                    memset(buff, 0, MSG_LEN);
                    int bytes_received = recv(client_fd, buff, MSG_LEN, 0);
                    if (bytes_received <= 0) {
                        // Client déconnecté
                        printf("Client déconnecté\n");
                        close(client_fd);
                        remove_client(client_fd);
                        for (int j = i; j < nfds - 1; j++) {
                            fds[j] = fds[j + 1];
                        }
                        nfds--;
                        i--;
                    } else {
                        printf("Reçu de %d: %s", client_fd, buff);
                        // Renvoyer le message au client
                        if (send(client_fd, buff, strlen(buff), 0) <= 0) {
                            perror("send()");
                        } else {
                            printf("Message envoyé à %d!\n", client_fd);
                        }
                    }
                }
            }
        }
    }
}

int handle_bind(const char *port) {
    struct addrinfo hints, *result, *rp;
    int sfd;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &result) != 0) {
        perror("getaddrinfo()");
        exit(EXIT_FAILURE);
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }
        if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sfd);
    }
    if (rp == NULL) {
        fprintf(stderr, "Could not bind\n");
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(result);
    return sfd;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd = handle_bind(argv[1]);
    if ((listen(sfd, SOMAXCONN)) != 0) {
        perror("listen()");
        exit(EXIT_FAILURE);
    }

    printf("Serveur en écoute sur le port %s\n", argv[1]);
    echo_server(sfd);

    // Nettoyage
    while (clients) {
        Client *tmp = clients;
        clients = clients->next;
        close(tmp->fd);
        free(tmp);
    }
    close(sfd);
    return EXIT_SUCCESS;
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd = handle_bind(argv[1]);
    if ((listen(sfd, SOMAXCONN)) != 0) {
        perror("listen()");
        exit(EXIT_FAILURE);
    }

    printf("Serveur en écoute sur le port %s\n", argv[1]);
    echo_server(sfd);

    // Nettoyage
    while (clients) {
        Client *tmp = clients;
        clients = clients->next;
        close(tmp->fd);
        free(tmp);
    }
    close(sfd);
    return EXIT_SUCCESS;
}