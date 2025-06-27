/*

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include "msg_struct.h"
#include "common.h"

#define BUFFER_SIZE 1024

void send_message_to_server(int sockfd, enum msg_type type, const char *nick_sender, 
                          const char *infos, const char *payload) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    
    msg.type = type;
    strncpy(msg.nick_sender, nick_sender, NICK_LEN - 1);
    if (infos) {
        strncpy(msg.infos, infos, INFOS_LEN - 1);
    }
    msg.pld_len = payload ? strlen(payload) : 0;

    // Envoyer la structure message
    if (send(sockfd, &msg, sizeof(msg), 0) < 0) {
        perror("Erreur d'envoi de la structure message");
        return;
    }

    // Envoyer le payload si présent
    if (payload && msg.pld_len > 0) {
        if (send(sockfd, payload, msg.pld_len, 0) < 0) {
            perror("Erreur d'envoi du payload");
            return;
        }
    }
}

void handle_server_message(int sockfd) {
    struct message msg;
    memset(&msg, 0, sizeof(msg));

    // Recevoir la structure message
    ssize_t received = recv(sockfd, &msg, sizeof(struct message), 0);
    if (received <= 0) {
        printf("Serveur déconnecté\n");
        exit(EXIT_FAILURE);
    }

    // Recevoir le payload si présent
    char payload[BUFFER_SIZE] = {0};
    if (msg.pld_len > 0) {
        received = recv(sockfd, payload, msg.pld_len, 0);
        if (received <= 0) {
            printf("Erreur de réception du payload\n");
            return;
        }
        payload[received] = '\0';
    }

    // Afficher le message selon son type
    switch (msg.type) {
        case NICKNAME_NEW:
        case NICKNAME_LIST:
        case NICKNAME_INFOS:
        case ECHO_SEND:
            printf("%s\n", payload);
            break;
        case UNICAST_SEND:
            printf("[%s]: %s\n", msg.nick_sender, payload);
            break;
        case BROADCAST_SEND:
            printf("[%s][All]: %s\n", msg.nick_sender, payload);
            break;
        default:
            printf("Message de type inconnu reçu\n");
    }
}

void echo_client(int sockfd) {
    char buff[BUFFER_SIZE];
    struct pollfd fds[2];

    // Configuration de poll()
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    printf("Connecté au serveur. Tapez /help pour la liste des commandes.\n");

    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            perror("poll()");
            break;
        }

        if (fds[0].revents & POLLIN) {
            // Lecture de l'entrée utilisateur
            memset(buff, 0, BUFFER_SIZE);
            if (fgets(buff, BUFFER_SIZE, stdin) == NULL) {
                break;
            }
            buff[strcspn(buff, "\n")] = 0;

            if (strlen(buff) == 0) {
                continue;
            }

            // Traitement des commandes
            if (strcmp(buff, "/quit") == 0) {
                printf("Déconnexion...\n");
                break;
            } else if (strncmp(buff, "/nick ", 6) == 0) {
                send_message_to_server(sockfd, NICKNAME_NEW, "", buff + 6, NULL);
            } else if (strcmp(buff, "/who") == 0) {
                send_message_to_server(sockfd, NICKNAME_LIST, "", NULL, NULL);
            } else if (strncmp(buff, "/whois ", 7) == 0) {
                send_message_to_server(sockfd, NICKNAME_INFOS, "", buff + 7, NULL);
            } else if (strncmp(buff, "/msgall ", 8) == 0) {
                send_message_to_server(sockfd, BROADCAST_SEND, "", NULL, buff + 8);
            } else if (strncmp(buff, "/msg ", 5) == 0) {
                char *recipient = strtok(buff + 5, " ");
                char *message = strtok(NULL, "");
                if (recipient && message) {
                    send_message_to_server(sockfd, UNICAST_SEND, "", recipient, message + 1);
                } else {
                    printf("Usage: /msg <pseudo> <message>\n");
                }
            } else if (strcmp(buff, "/help") == 0) {
                printf("Commandes disponibles:\n");
                printf("/nick <pseudo> : définir son pseudo\n");
                printf("/who : liste des utilisateurs connectés\n");
                printf("/whois <pseudo> : informations sur un utilisateur\n");
                printf("/msgall <message> : envoyer un message à tous\n");
                printf("/msg <pseudo> <message> : envoyer un message privé\n");
                printf("/quit : quitter le chat\n");
            } else {
                printf("Commande inconnue. Tapez /help pour la liste des commandes.\n");
            }
        }

        if (fds[1].revents & POLLIN) {
            handle_server_message(sockfd);
        }
    }
}

int handle_connect(const char *server_name, const char *server_port) {
    struct addrinfo hints, *result, *rp;
    int sfd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(server_name, server_port, &hints, &result);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }
        close(sfd);
    }

    freeaddrinfo(result);

    if (rp == NULL) {
        fprintf(stderr, "Impossible de se connecter\n");
        exit(EXIT_FAILURE);
    }

    return sfd;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd = handle_connect(argv[1], argv[2]);
    printf("Connecté au serveur %s:%s\n", argv[1], argv[2]);

    echo_client(sfd);
    close(sfd);
    return EXIT_SUCCESS;
}*/


#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include "msg_struct.h"
#include "common.h"

#define BUFFER_SIZE 1024

void send_message_to_server(int sockfd, enum msg_type type, const char *nick_sender, 
                          const char *infos, const char *payload) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    
    msg.type = type;
    strncpy(msg.nick_sender, nick_sender, NICK_LEN - 1);
    if (infos) {
        strncpy(msg.infos, infos, INFOS_LEN - 1);
    }
    msg.pld_len = payload ? strlen(payload) : 0;

    
    if (send(sockfd, &msg, sizeof(msg), 0) < 0) {
        perror("Erreur d'envoi de la structure message");
        return;
    }

    
    if (payload && msg.pld_len > 0) {
        if (send(sockfd, payload, msg.pld_len, 0) < 0) {
            perror("Erreur d'envoi du payload");
            return;
        }
    }
}

void handle_server_message(int sockfd) {
    struct message msg;
    memset(&msg, 0, sizeof(msg));

    
    ssize_t received = recv(sockfd, &msg, sizeof(struct message), 0);
    if (received <= 0) {
        printf("Serveur déconnecté\n");
        exit(EXIT_FAILURE);
    }

    
    char payload[BUFFER_SIZE] = {0};
    if (msg.pld_len > 0) {
        received = recv(sockfd, payload, msg.pld_len, 0);
        if (received <= 0) {
            printf("Erreur de réception du payload\n");
            return;
        }
        payload[received] = '\0';
    }

    
    switch (msg.type) {
        case NICKNAME_NEW:
        case NICKNAME_LIST:
        case NICKNAME_INFOS:
        case ECHO_SEND:
            printf("%s\n", payload);
            break;
        case UNICAST_SEND:
            printf("[%s]: %s\n", msg.nick_sender, payload);
            break;
        case BROADCAST_SEND:
            printf("[%s][All]: %s\n", msg.nick_sender, payload);
            break;
        default:
            printf("Message de type inconnu reçu\n");
    }
}

void echo_client(int sockfd) {
    char buff[BUFFER_SIZE];
    struct pollfd fds[2];

   
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    printf("Connecté au serveur. Tapez /help pour la liste des commandes.\n");

    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            perror("poll()");
            break;
        }

        if (fds[0].revents & POLLIN) {
           
            memset(buff, 0, BUFFER_SIZE);
            if (fgets(buff, BUFFER_SIZE, stdin) == NULL) {
                break;
            }
            buff[strcspn(buff, "\n")] = 0;

            if (strlen(buff) == 0) {
                continue;
            }

            
            if (strcmp(buff, "/quit") == 0) {
                printf("Déconnexion...\n");
                break;
            } else if (strncmp(buff, "/nick ", 6) == 0) {
                send_message_to_server(sockfd, NICKNAME_NEW, "", buff + 6, NULL);
            } else if (strcmp(buff, "/who") == 0) {
                send_message_to_server(sockfd, NICKNAME_LIST, "", NULL, NULL);
            } else if (strncmp(buff, "/whois ", 7) == 0) {
                send_message_to_server(sockfd, NICKNAME_INFOS, "", buff + 7, NULL);
            } else if (strncmp(buff, "/msgall ", 8) == 0) {
                send_message_to_server(sockfd, BROADCAST_SEND, "", NULL, buff + 8);
            } else if (strncmp(buff, "/msg ", 5) == 0) {
                char *recipient = strtok(buff + 5, " ");
                char *message = strtok(NULL, "");
                if (recipient && message) {
                    send_message_to_server(sockfd, UNICAST_SEND, "", recipient, message + 1);
                } else {
                    printf("Usage: /msg <pseudo> <message>\n");
                }
            } else if (strcmp(buff, "/help") == 0) {
                printf("Commandes disponibles:\n");
                printf("/nick <pseudo> : définir son pseudo\n");
                printf("/who : liste des utilisateurs connectés\n");
                printf("/whois <pseudo> : informations sur un utilisateur\n");
                printf("/msgall <message> : envoyer un message à tous\n");
                printf("/msg <pseudo> <message> : envoyer un message privé\n");
                printf("/quit : quitter le chat\n");
            } else {
                printf("Commande inconnue. Tapez /help pour la liste des commandes.\n");
            }
        }

        if (fds[1].revents & POLLIN) {
            handle_server_message(sockfd);
        }
    }
}

int handle_connect(const char *server_name, const char *server_port) {
    struct addrinfo hints, *result, *rp;
    int sfd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(server_name, server_port, &hints, &result);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }
        close(sfd);
    }

    freeaddrinfo(result);

    if (rp == NULL) {
        fprintf(stderr, "Impossible de se connecter\n");
        exit(EXIT_FAILURE);
    }

    return sfd;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd = handle_connect(argv[1], argv[2]);
    printf("Connecté au serveur %s:%s\n", argv[1], argv[2]);

    echo_client(sfd);
    close(sfd);
    return EXIT_SUCCESS;
}