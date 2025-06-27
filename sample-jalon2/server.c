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
#include <time.h>
#include "msg_struct.h"

#define MAX_CLIENTS 10
#define PAYLOAD_SIZE 1024

typedef struct Client {
    int fd;
    struct sockaddr_in addr;
    char nickname[NICK_LEN];
    time_t connection_time;
    struct Client *next;
} Client;

Client *clients = NULL;

void send_response(int fd, const char *nick_sender, enum msg_type type, const char *infos, const char *payload) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    
    msg.pld_len = payload ? strlen(payload) : 0;
    strncpy(msg.nick_sender, nick_sender, NICK_LEN - 1);
    msg.type = type;
    strncpy(msg.infos, infos, INFOS_LEN - 1);

    if (send(fd, &msg, sizeof(struct message), 0) < 0) {
        perror("send message");
        return;
    }

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
    new_client->next = clients;
    clients = new_client;

    send_response(fd, "Server", ECHO_SEND, "", "Please login with /nick <your pseudo>");
    printf("New client connected: %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

void remove_client(int fd) {
    Client **pp = &clients;
    while (*pp && (*pp)->fd != fd) {
        pp = &(*pp)->next;
    }
    if (*pp) {
        Client *tmp = *pp;
        *pp = (*pp)->next;
        printf("Client removed: %s:%d\n", inet_ntoa(tmp->addr.sin_addr), ntohs(tmp->addr.sin_port));
        free(tmp);
    }
}

void handle_nickname_new(int fd, struct message *msg) {
    if (!is_nickname_valid(msg->infos)) {
        send_response(fd, "Server", NICKNAME_NEW, "", "Invalid nickname format");
        return;
    }

    Client *curr = clients;
    while (curr) {
        if (strcmp(curr->nickname, msg->infos) == 0 && curr->fd != fd) {
            send_response(fd, "Server", NICKNAME_NEW, "", "Nickname already taken");
            return;
        }
        curr = curr->next;
    }

    curr = clients;
    while (curr) {
        if (curr->fd == fd) {
            strncpy(curr->nickname, msg->infos, NICK_LEN - 1);
            curr->nickname[NICK_LEN - 1] = '\0';
            
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
        curr = curr->next;
    }
}

void handle_who(int fd) {
    char user_list[PAYLOAD_SIZE] = "Online users are:\n";
    for (Client *curr = clients; curr; curr = curr->next) {
        if (curr->nickname[0]) {
            char user[NICK_LEN + 4];
            snprintf(user, sizeof(user), "- %s\n", curr->nickname);
            strcat(user_list, user);
        }
    }
    send_response(fd, "Server", NICKNAME_LIST, "", user_list);
}

void handle_whois(int fd, struct message *msg) {
    Client *target = NULL;
    for (Client *curr = clients; curr; curr = curr->next) {
        if (strcmp(curr->nickname, msg->infos) == 0) {
            target = curr;
            break;
        }
    }

    if (target) {
        char time_str[30];
        strftime(time_str, sizeof(time_str), "%Y/%m/%d@%H:%M", 
                localtime(&target->connection_time));
        
        char info[PAYLOAD_SIZE];
        snprintf(info, PAYLOAD_SIZE, "%s connected since %s with IP address %s and port number %d",
                target->nickname, time_str, 
                inet_ntoa(target->addr.sin_addr), 
                ntohs(target->addr.sin_port));
        
        send_response(fd, "Server", NICKNAME_INFOS, "", info);
    } else {
        send_response(fd, "Server", NICKNAME_INFOS, "", "User not found");
    }
}

void handle_broadcast_send(int fd, const char *payload) {
    Client *sender = NULL;
    for (Client *curr = clients; curr; curr = curr->next) {
        if (curr->fd == fd) {
            sender = curr;
            break;
        }
    }

    if (!sender || !sender->nickname[0]) {
        send_response(fd, "Server", ECHO_SEND, "", "You must set a nickname first");
        return;
    }

    for (Client *curr = clients; curr; curr = curr->next) {
        if (curr->fd != fd && curr->nickname[0]) {
            send_response(curr->fd, sender->nickname, BROADCAST_SEND, "", payload);
        }
    }
}

void handle_unicast_send(int fd, struct message *msg, const char *payload) {
    Client *sender = NULL;
    for (Client *curr = clients; curr; curr = curr->next) {
        if (curr->fd == fd) {
            sender = curr;
            break;
        }
    }

    if (!sender || !sender->nickname[0]) {
        send_response(fd, "Server", ECHO_SEND, "", "You must set a nickname first");
        return;
    }

    Client *recipient = NULL;
    for (Client *curr = clients; curr; curr = curr->next) {
        if (strcmp(curr->nickname, msg->infos) == 0) {
            recipient = curr;
            break;
        }
    }

    if (recipient) {
        send_response(recipient->fd, sender->nickname, UNICAST_SEND, "", payload);
    } else {
        char error[INFOS_LEN];
        const char *prefix = "User ";
        const char *suffix = " does not exist";
        size_t prefix_len = strlen(prefix);
        size_t suffix_len = strlen(suffix);
        size_t max_nick_len = INFOS_LEN - prefix_len - suffix_len - 1;

        strncpy(error, prefix, INFOS_LEN - 1);
        strncat(error, msg->infos, max_nick_len);
        strncat(error, suffix, suffix_len);
        error[INFOS_LEN - 1] = '\0';

        send_response(fd, "Server", UNICAST_SEND, "", error);
    }
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
        case ECHO_SEND:
            send_response(fd, "Server", ECHO_SEND, "", payload);
            break;
        default:
            fprintf(stderr, "Unknown message type\n");
    }
}

void echo_server(int server_fd) {
    struct pollfd fds[MAX_CLIENTS + 1];
    int nfds = 1;

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    while (1) {
        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd == server_fd) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    
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
    echo_server(sfd);

    close(sfd);
    return 0;
}

*/

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
#define PAYLOAD_SIZE 1024

typedef struct Client {
    int fd;
    struct sockaddr_in addr;
    char nickname[NICK_LEN];
    time_t connection_time;
    struct Client *next;
} Client;

Client *clients = NULL;

void send_response(int fd, const char *nick_sender, enum msg_type type, const char *infos, const char *payload) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message));
    
    msg.pld_len = payload ? strlen(payload) : 0;
    strncpy(msg.nick_sender, nick_sender, NICK_LEN - 1);
    msg.type = type;
    strncpy(msg.infos, infos, INFOS_LEN - 1);

    if (send(fd, &msg, sizeof(struct message), 0) < 0) {
        perror("send message");
        return;
    }

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
    new_client->next = clients;
    clients = new_client;

    send_response(fd, "Server", ECHO_SEND, "", "Please login with /nick <your pseudo>");
    printf("New client connected: %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
}

void remove_client(int fd) {
    Client **pp = &clients;
    while (*pp && (*pp)->fd != fd) {
        pp = &(*pp)->next;
    }
    if (*pp) {
        Client *tmp = *pp;
        *pp = (*pp)->next;
        printf("Client removed: %s:%d\n", inet_ntoa(tmp->addr.sin_addr), ntohs(tmp->addr.sin_port));
        free(tmp);
    }
}

void handle_nickname_new(int fd, struct message *msg) {
    if (!is_nickname_valid(msg->infos)) {
        send_response(fd, "Server", NICKNAME_NEW, "", "Invalid nickname format");
        return;
    }

    Client *curr = clients;
    while (curr) {
        if (strcmp(curr->nickname, msg->infos) == 0 && curr->fd != fd) {
            send_response(fd, "Server", NICKNAME_NEW, "", "Nickname already taken");
            return;
        }
        curr = curr->next;
    }

    curr = clients;
    while (curr) {
        if (curr->fd == fd) {
            strncpy(curr->nickname, msg->infos, NICK_LEN - 1);
            curr->nickname[NICK_LEN - 1] = '\0';
            
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
        curr = curr->next;
    }
}

void handle_who(int fd) {
    char user_list[PAYLOAD_SIZE] = "Online users are:\n";
    for (Client *curr = clients; curr; curr = curr->next) {
        if (curr->nickname[0]) {
            char user[NICK_LEN + 4];
            snprintf(user, sizeof(user), "- %s\n", curr->nickname);
            strcat(user_list, user);
        }
    }
    send_response(fd, "Server", NICKNAME_LIST, "", user_list);
}

void handle_whois(int fd, struct message *msg) {
    Client *target = NULL;
    for (Client *curr = clients; curr; curr = curr->next) {
        if (strcmp(curr->nickname, msg->infos) == 0) {
            target = curr;
            break;
        }
    }

    if (target) {
        char time_str[30];
        strftime(time_str, sizeof(time_str), "%Y/%m/%d@%H:%M", 
                localtime(&target->connection_time));
        
        char info[PAYLOAD_SIZE];
        snprintf(info, PAYLOAD_SIZE, "%s connected since %s with IP address %s and port number %d",
                target->nickname, time_str, 
                inet_ntoa(target->addr.sin_addr), 
                ntohs(target->addr.sin_port));
        
        send_response(fd, "Server", NICKNAME_INFOS, "", info);
    } else {
        send_response(fd, "Server", NICKNAME_INFOS, "", "User not found");
    }
}

void handle_broadcast_send(int fd, const char *payload) {
    Client *sender = NULL;
    for (Client *curr = clients; curr; curr = curr->next) {
        if (curr->fd == fd) {
            sender = curr;
            break;
        }
    }

    if (!sender || !sender->nickname[0]) {
        send_response(fd, "Server", ECHO_SEND, "", "You must set a nickname first");
        return;
    }

    for (Client *curr = clients; curr; curr = curr->next) {
        if (curr->fd != fd && curr->nickname[0]) {
            send_response(curr->fd, sender->nickname, BROADCAST_SEND, "", payload);
        }
    }
}

void handle_unicast_send(int fd, struct message *msg, const char *payload) {
    Client *sender = NULL;
    for (Client *curr = clients; curr; curr = curr->next) {
        if (curr->fd == fd) {
            sender = curr;
            break;
        }
    }

    if (!sender || !sender->nickname[0]) {
        send_response(fd, "Server", ECHO_SEND, "", "You must set a nickname first");
        return;
    }

    Client *recipient = NULL;
    for (Client *curr = clients; curr; curr = curr->next) {
        if (strcmp(curr->nickname, msg->infos) == 0) {
            recipient = curr;
            break;
        }
    }

    if (recipient) {
        send_response(recipient->fd, sender->nickname, UNICAST_SEND, "", payload);
    } else {
        char error[INFOS_LEN];
        const char *prefix = "User ";
        const char *suffix = " does not exist";
        size_t prefix_len = strlen(prefix);
        size_t suffix_len = strlen(suffix);
        size_t max_nick_len = INFOS_LEN - prefix_len - suffix_len - 1;

        strncpy(error, prefix, INFOS_LEN - 1);
        strncat(error, msg->infos, max_nick_len);
        strncat(error, suffix, suffix_len);
        error[INFOS_LEN - 1] = '\0';

        send_response(fd, "Server", UNICAST_SEND, "", error);
    }
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
        case ECHO_SEND:
            send_response(fd, "Server", ECHO_SEND, "", payload);
            break;
        default:
            fprintf(stderr, "Unknown message type\n");
    }
}

void echo_server(int server_fd) {
    struct pollfd fds[MAX_CLIENTS + 1];
    int nfds = 1;

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    while (1) {
        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                if (fds[i].fd == server_fd) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    
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
    echo_server(sfd);

    close(sfd);
    return 0;
}

