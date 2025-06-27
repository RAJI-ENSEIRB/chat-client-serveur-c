#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include "msg_struct.h"

#define MAX_CLIENTS 10
#define MAX_CHANNELS 100
#define CHANNEL_NAME_LEN 50
#define PAYLOAD_SIZE 1024

// Structures existantes
typedef struct Client {
    int fd;
    struct sockaddr_in addr;
    char nickname[NICK_LEN];
    time_t connection_time;
    char current_channel[CHANNEL_NAME_LEN];
    struct Client *next;
} Client;

typedef struct Channel {
    char name[CHANNEL_NAME_LEN];
    int num_users;
    struct Channel *next;
} Channel;

// Nouvelle structure pour le jalon 4
typedef struct FileTransfer {
    char sender_nick[NICK_LEN];
    char receiver_nick[NICK_LEN];
    char filename[256];
    struct FileTransfer *next;
} FileTransfer;

// Variables globales
Client *clients = NULL;
Channel *channels = NULL;
FileTransfer *pending_transfers = NULL;

// Déclarations des fonctions (prototypes)
void send_response(int fd, const char *nick_sender, enum msg_type type, const char *infos, const char *payload);
void handle_nickname_new(int fd, struct message *msg);
void handle_who(int fd);
void handle_whois(int fd, struct message *msg);
void handle_broadcast_send(int fd, const char *payload);
void handle_unicast_send(int fd, struct message *msg, const char *payload);
void handle_channel_message(int fd, struct message *msg, const char *payload);

// Nouvelles déclarations pour le jalon 4
void handle_file_request(int fd, struct message *msg, const char *payload);
void handle_file_accept(int fd, struct message *msg, const char *payload);
void handle_file_reject(int fd, struct message *msg);



// Implémentation des fonctions
void send_response(int fd, const char *nick_sender, enum msg_type type, 
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
    if (send(fd, &msg, sizeof(msg), 0) < 0) {
        perror("send message structure");
        return;
    }

    // Envoyer le payload si présent
    if (payload && msg.pld_len > 0) {
        if (send(fd, payload, msg.pld_len, 0) < 0) {
            perror("send payload");
        }
    }
}

int is_nickname_valid(const char *nickname) {
    if (strlen(nickname) == 0 || strlen(nickname) >= NICK_LEN)
        return 0;
    
    for (int i = 0; nickname[i]; i++) {
        char c = nickname[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9')))
            return 0;
    }
    return 1;
}

void add_client(int fd, struct sockaddr_in addr) {
    Client *new_client = malloc(sizeof(Client));
    if (!new_client) {
        perror("malloc");
        return;
    }
    
    new_client->fd = fd;
    new_client->addr = addr;
    new_client->nickname[0] = '\0';
    new_client->connection_time = time(NULL);
    new_client->current_channel[0] = '\0';
    new_client->next = clients;
    clients = new_client;

    send_response(fd, "Server", ECHO_SEND, "", "Please login with /nick <your pseudo>");
    printf("New client connected: %s:%d\n", 
           inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

void remove_client(int fd) {
    Client **pp = &clients;
    while (*pp && (*pp)->fd != fd) {
        pp = &(*pp)->next;
    }
    if (*pp) {
        Client *tmp = *pp;
        *pp = (*pp)->next;
        printf("Client removed: %s:%d\n", 
               inet_ntoa(tmp->addr.sin_addr), ntohs(tmp->addr.sin_port));
        free(tmp);
    }
}

void broadcast_to_channel(const char *channel_name, const char *sender, 
                         const char *message, enum msg_type type) {
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->current_channel[0] && 
            strcmp(curr->current_channel, channel_name) == 0) {
            send_response(curr->fd, sender, type, channel_name, message);
        }
    }
}

Channel* find_channel(const char *name) {
    Channel *curr = channels;
    while (curr) {
        if (strcmp(curr->name, name) == 0)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

void leave_current_channel(Client *client) {
    if (client->current_channel[0] != '\0') {
        Channel *channel = find_channel(client->current_channel);
        if (channel) {
            channel->num_users--;
            
            char notice[256];
            snprintf(notice, sizeof(notice), "%s has quit %s", 
                    client->nickname, channel->name);
            broadcast_to_channel(channel->name, "Server", notice, MULTICAST_QUIT);
            
            if (channel->num_users == 0) {
                send_response(client->fd, "Server", MULTICAST_QUIT, 
                            channel->name, "You were the last user in this channel");
                
                Channel **pp = &channels;
                while (*pp && strcmp((*pp)->name, channel->name) != 0) {
                    pp = &(*pp)->next;
                }
                if (*pp) {
                    Channel *tmp = *pp;
                    *pp = (*pp)->next;
                    free(tmp);
                }
            }
            client->current_channel[0] = '\0';
        }
    }
}

void handle_nickname_new(int fd, struct message *msg) {
    if (!is_nickname_valid(msg->infos)) {
        send_response(fd, "Server", NICKNAME_NEW, "", "Invalid nickname format");
        return;
    }

    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (strcmp(curr->nickname, msg->infos) == 0 && curr->fd != fd) {
            send_response(fd, "Server", NICKNAME_NEW, "", "Nickname already taken");
            return;
        }
    }

    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            strncpy(curr->nickname, msg->infos, NICK_LEN - 1);
            curr->nickname[NICK_LEN - 1] = '\0';
            
            // Construction sécurisée du message de bienvenue
            char response[INFOS_LEN];
            const char *prefix = "Welcome on the chat ";
            size_t prefix_len = strlen(prefix);
            size_t max_nick_len = INFOS_LEN - prefix_len - 1;
            
            strncpy(response, prefix, INFOS_LEN - 1);
            strncat(response, msg->infos, max_nick_len);
            response[INFOS_LEN - 1] = '\0';

            send_response(fd, "Server", NICKNAME_NEW, "", response);
            printf("Client %d changed nickname to %s\n", fd, msg->infos);
            return;
        }
    }
}

void handle_who(int fd) {
    char user_list[PAYLOAD_SIZE] = "Online users are:\n";
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->nickname[0]) {
            char user[NICK_LEN + 4];
            snprintf(user, sizeof(user), "- %s\n", curr->nickname);
            strcat(user_list, user);
        }
    }
    send_response(fd, "Server", NICKNAME_LIST, "", user_list);
}

void handle_whois(int fd, struct message *msg) {
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (strcmp(curr->nickname, msg->infos) == 0) {
            char time_str[30];
            strftime(time_str, sizeof(time_str), "%Y/%m/%d@%H:%M", 
                    localtime(&curr->connection_time));
            
            char info[PAYLOAD_SIZE];
            snprintf(info, PAYLOAD_SIZE, "%s connected since %s with IP address %s and port number %d",
                    curr->nickname, time_str, 
                    inet_ntoa(curr->addr.sin_addr), 
                    ntohs(curr->addr.sin_port));
            
            send_response(fd, "Server", NICKNAME_INFOS, "", info);
            return;
        }
    }
    send_response(fd, "Server", NICKNAME_INFOS, "", "User not found");
}

void handle_broadcast_send(int fd, const char *payload) {
    Client *sender = NULL;
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            sender = curr;
            break;
        }
    }

    if (!sender || !sender->nickname[0]) {
        send_response(fd, "Server", BROADCAST_SEND, "", "You must set a nickname first");
        return;
    }

    // Envoyer à tous les autres clients
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd != fd && curr->nickname[0]) {
            send_response(curr->fd, sender->nickname, BROADCAST_SEND, "", payload);
        }
    }
}

void handle_unicast_send(int fd, struct message *msg, const char *payload) {
    Client *sender = NULL;
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            sender = curr;
            break;
        }
    }

    if (!sender || !sender->nickname[0]) {
        send_response(fd, "Server", UNICAST_SEND, "", "You must set a nickname first");
        return;
    }

    // Chercher le destinataire et envoyer le message
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (strcmp(curr->nickname, msg->infos) == 0) {
            send_response(curr->fd, sender->nickname, UNICAST_SEND, "", payload);
            return;
        }
    }

    // Construction sécurisée du message d'erreur
    char error[INFOS_LEN];
    const char *prefix = "User ";
    const char *suffix = " does not exist";
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    size_t max_nick_len = INFOS_LEN - prefix_len - suffix_len - 1;

    strncpy(error, prefix, INFOS_LEN - 1);
    strncat(error, msg->infos, max_nick_len);
    strncat(error, suffix, INFOS_LEN - strlen(error) - 1);
    error[INFOS_LEN - 1] = '\0';

    send_response(fd, "Server", UNICAST_SEND, "", error);
}

void handle_create_channel(int fd, struct message *msg) {
    Client *client = NULL;
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            client = curr;
            break;
        }
    }

    if (!client || !client->nickname[0]) {
        send_response(fd, "Server", MULTICAST_CREATE, "", "You must set a nickname first");
        return;
    }

    if (!is_nickname_valid(msg->infos)) {
        send_response(fd, "Server", MULTICAST_CREATE, "", "Invalid channel name");
        return;
    }

    if (find_channel(msg->infos)) {
        send_response(fd, "Server", MULTICAST_CREATE, "", "Channel already exists");
        return;
    }

    // Créer le nouveau salon
    Channel *new_channel = malloc(sizeof(Channel));
    if (!new_channel) {
        perror("malloc");
        return;
    }

    strncpy(new_channel->name, msg->infos, CHANNEL_NAME_LEN - 1);
    new_channel->name[CHANNEL_NAME_LEN - 1] = '\0';
    new_channel->num_users = 0;
    new_channel->next = channels;
    channels = new_channel;

    // Faire rejoindre le salon au créateur
    leave_current_channel(client);
    strncpy(client->current_channel, msg->infos, CHANNEL_NAME_LEN - 1);
    client->current_channel[CHANNEL_NAME_LEN - 1] = '\0';
    new_channel->num_users++;

    send_response(fd, "Server", MULTICAST_CREATE, "", "Channel created successfully");
    char join_msg[PAYLOAD_SIZE];
    snprintf(join_msg, PAYLOAD_SIZE, "You have joined %s", msg->infos);
    send_response(fd, "Server", MULTICAST_JOIN, msg->infos, join_msg);
}

void handle_channel_list(int fd) {
    char list[PAYLOAD_SIZE] = "Available channels:\n";
    for (Channel *curr = channels; curr != NULL; curr = curr->next) {
        char channel_info[128];
        snprintf(channel_info, sizeof(channel_info), "- %s (%d users)\n", 
                curr->name, curr->num_users);
        strcat(list, channel_info);
    }
    send_response(fd, "Server", MULTICAST_LIST, "", list);
}

void handle_join_channel(int fd, struct message *msg) {
    Client *client = NULL;
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            client = curr;
            break;
        }
    }

    if (!client || !client->nickname[0]) {
        send_response(fd, "Server", MULTICAST_JOIN, "", "You must set a nickname first");
        return;
    }

    Channel *channel = find_channel(msg->infos);
    if (!channel) {
        send_response(fd, "Server", MULTICAST_JOIN, "", "Channel does not exist");
        return;
    }

    // Quitter le salon actuel si nécessaire
    leave_current_channel(client);

    // Rejoindre le nouveau salon
    strncpy(client->current_channel, msg->infos, CHANNEL_NAME_LEN - 1);
    client->current_channel[CHANNEL_NAME_LEN - 1] = '\0';
    channel->num_users++;

    // Notifier tout le monde
    char notice[PAYLOAD_SIZE];
    snprintf(notice, sizeof(notice), "%s has joined the channel", client->nickname);
    broadcast_to_channel(channel->name, "Server", notice, MULTICAST_JOIN);

    char join_msg[PAYLOAD_SIZE];
    snprintf(join_msg, PAYLOAD_SIZE, "You have joined %s", msg->infos);
    send_response(fd, "Server", MULTICAST_JOIN, msg->infos, join_msg);
}

void handle_channel_message(int fd, struct message *msg, const char *payload) {
    Client *client = NULL;
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            client = curr;
            break;
        }
    }

    if (!client || !client->nickname[0]) {
        send_response(fd, "Server", MULTICAST_SEND, "", "You must set a nickname first");
        return;
    }

    if (!client->current_channel[0]) {
        send_response(fd, "Server", MULTICAST_SEND, "", "You must join a channel first");
        return;
    }

    // Utiliser msg->infos pour vérifier si un canal spécifique est demandé
    if (msg && msg->infos[0] != '\0') {
        Channel *requested_channel = find_channel(msg->infos);
        if (!requested_channel || strcmp(msg->infos, client->current_channel) != 0) {
            send_response(fd, "Server", MULTICAST_SEND, "", "Invalid channel or not your current channel");
            return;
        }
    }

    // Vérifier si le canal existe toujours
    if (!find_channel(client->current_channel)) {
        send_response(fd, "Server", MULTICAST_SEND, "", "Your channel no longer exists");
        client->current_channel[0] = '\0';
        return;
    }

    broadcast_to_channel(client->current_channel, client->nickname, payload, MULTICAST_SEND);
}
void handle_quit_channel(int fd, struct message *msg) {
    Client *client = NULL;
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            client = curr;
            break;
        }
    }

    if (!client || !client->nickname[0]) {
        send_response(fd, "Server", MULTICAST_QUIT, "", "You must set a nickname first");
        return;
    }

    if (strcmp(client->current_channel, msg->infos) != 0) {
        send_response(fd, "Server", MULTICAST_QUIT, "", "You are not in this channel");
        return;
    }

    leave_current_channel(client);
}

void handle_file_request(int fd, struct message *msg, const char *payload) {
    Client *sender = NULL;
    Client *receiver = NULL;

    // Trouver l'émetteur et le récepteur
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            sender = curr;
        }
        if (strcmp(curr->nickname, msg->infos) == 0) {
            receiver = curr;
        }
    }

    if (!sender || !sender->nickname[0]) {
        send_response(fd, "Server", FILE_REQUEST, "", "You must set a nickname first");
        return;
    }

    if (!receiver) {
        send_response(fd, "Server", FILE_REQUEST, "", "Recipient not found");
        return;
    }

    printf("File request from %s to %s: %s\n", sender->nickname, receiver->nickname, payload);

    // Transmettre la demande au récepteur
    char request_msg[512];
    snprintf(request_msg, sizeof(request_msg), "%s", payload);
    send_response(receiver->fd, sender->nickname, FILE_REQUEST, "", request_msg);
}

void handle_file_accept(int fd, struct message *msg, const char *payload) {
    Client *receiver = NULL;
    Client *sender = NULL;

    // Trouver le récepteur (celui qui accepte) et l'émetteur
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            receiver = curr;
        }
        if (strcmp(curr->nickname, msg->infos) == 0) {
            sender = curr;
        }
    }

    if (!receiver || !sender) {
        printf("Sender or receiver not found in file accept\n");
        return;
    }

    printf("File accept from %s to %s with address %s\n", 
           receiver->nickname, sender->nickname, payload);

    // Envoyer les informations de connexion à l'émetteur
    send_response(sender->fd, receiver->nickname, FILE_ACCEPT, receiver->nickname, payload);
}

void handle_file_reject(int fd, struct message *msg) {
    Client *receiver = NULL;
    Client *sender = NULL;

    // Trouver le récepteur (celui qui refuse) et l'émetteur
    for (Client *curr = clients; curr != NULL; curr = curr->next) {
        if (curr->fd == fd) {
            receiver = curr;
        }
        if (strcmp(curr->nickname, msg->infos) == 0) {
            sender = curr;
        }
    }

    if (!receiver || !sender) {
        printf("Sender or receiver not found in file reject\n");
        return;
    }

    printf("File reject from %s to %s\n", receiver->nickname, sender->nickname);

    // Notifier l'émetteur
    send_response(sender->fd, receiver->nickname, FILE_REJECT, receiver->nickname, 
                 "File transfer was rejected");
}

void handle_client_message(int fd, struct message *msg) {
    char payload[PAYLOAD_SIZE];
    memset(payload, 0, PAYLOAD_SIZE);
    
    if (msg->pld_len > 0) {
        if (recv(fd, payload, msg->pld_len, 0) < 0) {
            perror("recv payload");
            return;
        }
        payload[msg->pld_len] = '\0';
    }

    switch (msg->type) {
        case NICKNAME_NEW:
            handle_nickname_new(fd, msg);
            break;
        case NICKNAME_LIST:
            handle_who(fd);
            break;
        case NICKNAME_INFOS:
            handle_whois(fd, msg);
            break;
        case BROADCAST_SEND:
            handle_broadcast_send(fd, payload);
            break;
        case UNICAST_SEND:
            handle_unicast_send(fd, msg, payload);
            break;
        case MULTICAST_CREATE:
            handle_create_channel(fd, msg);
            break;
        case MULTICAST_LIST:
            handle_channel_list(fd);
            break;
        case MULTICAST_JOIN:
            handle_join_channel(fd, msg);
            break;
        case MULTICAST_SEND:
            handle_channel_message(fd, msg, payload);
            break;
        case MULTICAST_QUIT:
            handle_quit_channel(fd, msg);
            break;
        case ECHO_SEND:
            send_response(fd, "Server", ECHO_SEND, "", payload);
            break;
        case FILE_REQUEST:
            printf("Received file request\n");
            handle_file_request(fd, msg, payload);
            break;

        case FILE_ACCEPT:
            printf("Received file accept\n");
            handle_file_accept(fd, msg, payload);
            break;

        case FILE_REJECT:
            printf("Received file reject\n");
            handle_file_reject(fd, msg);
            break;

        case FILE_ACK:
            // Transmettre l'accusé de réception à l'émetteur
            for (Client *curr = clients; curr != NULL; curr = curr->next) {
                if (strcmp(curr->nickname, msg->infos) == 0) {
                    send_response(curr->fd, msg->nick_sender, FILE_ACK, msg->infos, payload);
                    break;
                }
            }
            break;
        default:
            fprintf(stderr, "Unknown message type\n");
    }
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        
        exit(EXIT_FAILURE);
    }

    if (listen(sfd, SOMAXCONN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %s\n", argv[1]);

    struct pollfd fds[MAX_CLIENTS + 1];
    int nfds = 1;
    fds[0].fd = sfd;
    fds[0].events = POLLIN;

    while (1) {
        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd == sfd) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(sfd, (struct sockaddr *)&client_addr, &client_len);
                    
                    if (client_fd < 0) {
                        perror("accept");
                        continue;
                    }

                    if (nfds < MAX_CLIENTS + 1) {
                        add_client(client_fd, client_addr);
                        fds[nfds].fd = client_fd;
                        fds[nfds].events = POLLIN;
                        nfds++;
                    } else {
                        send_response(client_fd, "Server", ECHO_SEND, "", "Server is full");
                        close(client_fd);
                    }
                } else {
                    struct message msg;
                    int received = recv(fds[i].fd, &msg, sizeof(struct message), 0);
                    
                    if (received <= 0) {
                        close(fds[i].fd);
                        remove_client(fds[i].fd);
                        for (int j = i; j < nfds - 1; j++) {
                            fds[j] = fds[j + 1];
                        }
                        nfds--;
                        i--;
                    } else {
                        handle_client_message(fds[i].fd, &msg);
                    }
                }
            }
        }
    }

    close(sfd);
    return 0;
}