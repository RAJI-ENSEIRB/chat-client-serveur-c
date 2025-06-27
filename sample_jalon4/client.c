#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>  // Pour ctime()
#include "msg_struct.h"
#include "common.h"

#define BUFFER_SIZE 1024
#define FILE_CHUNK_SIZE 8192
#define INBOX_DIR ".re216/inbox"

// Variables globales
static int sockfd;
static char current_nickname[NICK_LEN] = {0};

// Structure pour le transfert de fichiers
typedef struct {
    char filename[256];
    char sender[NICK_LEN];
    int listening_socket;
    char *file_path;
} FileTransfer;

static FileTransfer current_transfer = {0};

// Déclarations des fonctions (prototypes)
int handle_connect(const char *server_name, const char *server_port);
void handle_file_accept(const char *receiver, const char *address_port);
void send_message_to_server(int sockfd, enum msg_type type, const char *nick_sender, 
                          const char *infos, const char *payload);
void handle_file_request(const char *sender, const char *filename);
void send_file(const char *recipient, const char *filepath);
void receive_file(int sock);
int test_file(const char *filepath);
void handle_server_message(int sockfd);
void echo_client(int sockfd);
void ensure_inbox_directory(void);

// Implémentation des fonctions

// vérifie si le répertoire de réception des fichiers (INBOX_DIR) existe
void ensure_inbox_directory(void) {
    struct stat st = {0};
    if (stat(INBOX_DIR, &st) == -1) {
        char command[512];
        snprintf(command, sizeof(command), "mkdir -p %s", INBOX_DIR);
        system(command);
    }
}

//Établit une connexion avec le serveur.
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
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
    }

    return sfd;
}

//Vérifie l'existence d'un fichier, ses permissions et ses informations 
int test_file(const char *filepath) {
    struct stat st;
    
    if (stat(filepath, &st) == -1) {
        printf("Erreur stat: %s\n", strerror(errno));
        return -1;
    }

    printf("Informations fichier:\n");
    printf("- Taille: %ld bytes\n", st.st_size);
    printf("- Permissions: %o\n", st.st_mode & 0777);
    printf("- Dernier accès: %s", ctime(&st.st_atime));
    printf("- Dernière modification: %s", ctime(&st.st_mtime));

    return 0;
}

// Envoie un message structuré au serveur
void send_message_to_server(int sockfd, enum msg_type type, const char *nick_sender, 
                          const char *infos, const char *payload) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    
    msg.type = type;
    if (nick_sender) {
        strncpy(msg.nick_sender, nick_sender, NICK_LEN - 1);
    }
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

//Gère une demande de transfert de fichier. Demande à l'utilisateur s'il accepte la réception du fichier
void handle_file_request(const char *sender, const char *filename) {
    printf("%s wants you to accept the transfer of the file named \"%s\". Do you accept? [Y/N]\n", 
           sender, filename);
    
    char response;
    while ((response = getchar()) != 'Y' && response != 'N') {
        if (response != '\n') {
            printf("Please enter Y or N: ");
        }
    }
    
    if (response == 'Y') {
        char address_str[50] = {0};
        int listening_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (listening_socket < 0) {
            perror("Socket creation failed");
            return;
        }

        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = 0;  // Port automatique

        if (bind(listening_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Bind failed");
            close(listening_socket);
            return;
        }

        if (listen(listening_socket, 1) < 0) {
            perror("Listen failed");
            close(listening_socket);
            return;
        }

        socklen_t len = sizeof(server_addr);
        if (getsockname(listening_socket, (struct sockaddr*)&server_addr, &len) < 0) {
            perror("getsockname failed");
            close(listening_socket);
            return;
        }

        snprintf(address_str, sizeof(address_str), "127.0.0.1:%d", ntohs(server_addr.sin_port));
        
        strncpy(current_transfer.filename, filename, sizeof(current_transfer.filename) - 1);
        strncpy(current_transfer.sender, sender, sizeof(current_transfer.sender) - 1);
        current_transfer.listening_socket = listening_socket;
        
        ensure_inbox_directory();
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", INBOX_DIR, filename);
        current_transfer.file_path = strdup(file_path);

        send_message_to_server(sockfd, FILE_ACCEPT, current_nickname, sender, address_str);
    } else {
        send_message_to_server(sockfd, FILE_REJECT, current_nickname, sender, NULL);
    }
    
    while (getchar() != '\n');  // Vider le buffer
}

//Prépare un fichier pour l'envoi à un destinataire. Vérifie son existence et sa lisibilité, puis ouvre le fichier pour envoyer une demande de transfert au serveur.
void send_file(const char *recipient, const char *filepath) {
    printf("Tentative d'envoi du fichier:\n");
    printf("- Chemin: %s\n", filepath);

    if (access(filepath, F_OK) == -1) {
        printf("Le fichier n'existe pas: %s\n", strerror(errno));
        return;
    }

    if (access(filepath, R_OK) == -1) {
        printf("Pas de permission de lecture: %s\n", strerror(errno));
        return;
    }

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        printf("Erreur ouverture fichier: %s\n", strerror(errno));
        return;
    }

    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    rewind(file);

    printf("- Taille: %ld bytes\n", filesize);

    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    printf("- Nom du fichier: %s\n", filename);

    char test_buf[16] = {0};
    size_t read = fread(test_buf, 1, sizeof(test_buf) - 1, file);
    printf("- Premier contenu (%zu bytes): %s\n", read, test_buf);
    rewind(file);

    strncpy(current_transfer.filename, filename, sizeof(current_transfer.filename) - 1);
    current_transfer.file_path = strdup(filepath);

    send_message_to_server(sockfd, FILE_REQUEST, current_nickname, recipient, filename);
    printf("Demande de transfert envoyée\n");

    fclose(file);
}


//Reçoit un fichier via un socket donné. Ouvre le fichier pour l'écriture et sauvegarde les données reçues
void receive_file(int sock) {
    struct message msg;
    if (recv(sock, &msg, sizeof(msg), 0) <= 0) {
        perror("Failed to receive message header");
        return;
    }

    if (msg.type != FILE_SEND) {
        printf("Unexpected message type received\n");
        return;
    }

    FILE *file = fopen(current_transfer.file_path, "wb");
    if (!file) {
        printf("Cannot open file for writing\n");
        return;
    }

    printf("Receiving file from %s...\n", msg.nick_sender);

    char buffer[FILE_CHUNK_SIZE];
    size_t total_received = 0;
    ssize_t bytes_received;

    while (total_received < msg.pld_len && 
           (bytes_received = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        size_t written = fwrite(buffer, 1, bytes_received, file);
        if (written != bytes_received) {
            printf("Error writing to file\n");
            break;
        }
        total_received += bytes_received;
    }

    fclose(file);
    printf("File saved as %s\n", current_transfer.file_path);

    send_message_to_server(sockfd, FILE_ACK, current_nickname, 
                          msg.nick_sender, current_transfer.filename);

    free(current_transfer.file_path);
    memset(&current_transfer, 0, sizeof(current_transfer));
}
// Gère les messages du serveur. Reçoit un message structuré, vérifie le type, et exécute différentes actions en fonction du type
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
        case MULTICAST_CREATE:
            printf("[Server] %s\n", payload);
            break;
        case MULTICAST_LIST:
            printf("%s", payload);
            break;
        case MULTICAST_JOIN:
            printf("[%s] %s\n", msg.infos, payload);
            break;
        case MULTICAST_QUIT:
            printf("[%s] %s\n", msg.infos, payload);
            break;
        case MULTICAST_SEND:
            printf("[%s][%s] %s\n", msg.infos, msg.nick_sender, payload);
            break;
        case FILE_REQUEST:
            handle_file_request(msg.nick_sender, payload);
            break;
        case FILE_ACCEPT:
            printf("[Server] %s accepted file transfer\n", msg.infos);
            handle_file_accept(msg.infos, payload);
            break;
        case FILE_REJECT:
            printf("[Server] %s rejected file transfer\n", msg.infos);
            if (current_transfer.file_path) {
                free(current_transfer.file_path);
                memset(&current_transfer, 0, sizeof(current_transfer));
            }
            break;
        case FILE_ACK:
            printf("[Server] %s has received the file %s\n", msg.nick_sender, msg.infos);
            break;
        default:
            printf("Message de type inconnu reçu\n");
            break;
    }
}
//Interprète les commandes de l'utilisateur (ex. changement de pseudo, envoi de messages) et gère les transferts de fichiers entrants.
void echo_client(int sockfd) {
    char buff[BUFFER_SIZE];
    struct pollfd fds[3];  // Pour STDIN, socket serveur, et socket de fichier

    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    printf("Connecté au serveur. Tapez /help pour la liste des commandes.\n");
    ensure_inbox_directory();

    while (1) {
        int nfds = 2;  // Par défaut, on surveille STDIN et le socket serveur
        if (current_transfer.listening_socket > 0) {
            fds[2].fd = current_transfer.listening_socket;
            fds[2].events = POLLIN;
            nfds = 3;
        }

        int ret = poll(fds, nfds, -1);
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
                strncpy(current_nickname, buff + 6, NICK_LEN - 1);
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
            } else if (strncmp(buff, "/create ", 8) == 0) {
                send_message_to_server(sockfd, MULTICAST_CREATE, "", buff + 8, NULL);
            } else if (strcmp(buff, "/channel_list") == 0) {
                send_message_to_server(sockfd, MULTICAST_LIST, "", NULL, NULL);
            } else if (strncmp(buff, "/join ", 6) == 0) {
                send_message_to_server(sockfd, MULTICAST_JOIN, "", buff + 6, NULL);
            } else if (strncmp(buff, "/quit ", 6) == 0) {
                send_message_to_server(sockfd, MULTICAST_QUIT, "", buff + 6, NULL);
            } else if (strncmp(buff, "/send ", 6) == 0) {
                char *recipient = strtok(buff + 6, " ");
                char *filepath = strtok(NULL, "");
                if (recipient && filepath) {
                    // Supprimer les espaces au début du filepath
                    while (*filepath == ' ') filepath++;
                    // Supprimer les guillemets si présents
                    if (filepath[0] == '"') {
                        filepath++;
                        size_t len = strlen(filepath);
                        if (len > 0 && filepath[len-1] == '"') {
                            filepath[len-1] = '\0';
                        }
                    }
                    if (test_file(filepath) == 0) {
                        send_file(recipient, filepath);
                    }
                } else {
                    printf("Usage: /send <username> <filepath>\n");
                }
            } else if (strcmp(buff, "/help") == 0) {
                printf("Commandes disponibles:\n");
                printf("/nick <pseudo> : définir son pseudo\n");
                printf("/who : liste des utilisateurs connectés\n");
                printf("/whois <pseudo> : informations sur un utilisateur\n");
                printf("/msgall <message> : envoyer un message à tous\n");
                printf("/msg <pseudo> <message> : envoyer un message privé\n");
                printf("/create <channel> : créer un salon\n");
                printf("/channel_list : liste des salons\n");
                printf("/join <channel> : rejoindre un salon\n");
                printf("/quit <channel> : quitter un salon\n");
                printf("/send <pseudo> <filepath> : envoyer un fichier\n");
                printf("/quit : quitter le chat\n");
            } else {
                // Message pour le salon actuel
                send_message_to_server(sockfd, MULTICAST_SEND, "", NULL, buff);
            }
        }

        if (fds[1].revents & POLLIN) {
            handle_server_message(sockfd);
        }

        // Gérer les connexions entrantes pour le transfert de fichiers
        if (nfds > 2 && (fds[2].revents & POLLIN)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(current_transfer.listening_socket, 
                                   (struct sockaddr*)&client_addr, &client_len);
            if (client_sock < 0) {
                perror("accept");
                continue;
            }

            close(current_transfer.listening_socket);
            current_transfer.listening_socket = -1;

            receive_file(client_sock);
            close(client_sock);
        }
    }
}
//Gère la connexion au destinataire pour le transfert de fichiers une fois que l'autre côté a accepté
void handle_file_accept(const char *receiver, const char *address_port) {
    char ip[16];
    int port;
    if (sscanf(address_port, "%[^:]:%d", ip, &port) != 2) {
        printf("Invalid address format received\n");
        return;
    }

    // Se connecter au récepteur
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        printf("Invalid IP address\n");
        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return;
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return;
    }

    printf("Connected to receiver. Sending file...\n");

    // Envoyer le fichier
    FILE *file = fopen(current_transfer.file_path, "rb");
    if (!file) {
        printf("Cannot open file for sending\n");
        close(sock);
        return;
    }

    // Envoyer d'abord la structure message
    struct message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = FILE_SEND;
    strncpy(msg.nick_sender, current_nickname, NICK_LEN - 1);
    strncpy(msg.infos, current_transfer.filename, INFOS_LEN - 1);

    fseek(file, 0, SEEK_END);
    msg.pld_len = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (send(sock, &msg, sizeof(msg), 0) < 0) {
        perror("Failed to send message header");
        fclose(file);
        close(sock);
        return;
    }

    char buffer[FILE_CHUNK_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            perror("Failed to send file chunk");
            break;
        }
    }

    fclose(file);
    close(sock);
    printf("File sent successfully\n");
    free(current_transfer.file_path);
    memset(&current_transfer, 0, sizeof(current_transfer));
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    sockfd = handle_connect(argv[1], argv[2]);
    printf("Connecté au serveur %s:%s\n", argv[1], argv[2]);

    echo_client(sockfd);

    if (current_transfer.listening_socket > 0) {
        close(current_transfer.listening_socket);
    }
    if (current_transfer.file_path) {
        free(current_transfer.file_path);
    }
    close(sockfd);
    return EXIT_SUCCESS;
}