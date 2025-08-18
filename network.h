#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>


#define SOCKET int
#define ISVALIDSOCKET(s) ((s) >= 0)
#define CLOSESOCKET(s) close(s)
#define GETSOCKETERRNO() (errno)

#include <math.h>
#include <stdio.h>

#define MAX_REQUEST_SIZE 1023
#define MAX_UDP_REQUEST_SIZE 1024

enum{
    REBOOT_PRIMARY,
    REBOOT_SECONDARY,
    REBOOT_BOTH
};

struct client_info_t {
    socklen_t address_length;
    struct sockaddr_in address;
    struct sockaddr_in udp_addr;
    SOCKET tcp_socket;
    SOCKET udp_socket;
    char tcp_request[MAX_REQUEST_SIZE+1];
    char udp_request[MAX_UDP_REQUEST_SIZE];
    int received;
    int message_size;
    double last_request_time;
    struct client_info_t* next;
};

SOCKET create_udp_socket(char* hostname, char* port){

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(hostname);
    server_addr.sin_port= htons(atoi(port));

    printf("Creating UDP Socket...\n");
    SOCKET server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket == -1){
        perror("Failed to create socket.");

        return -1;
    }

    printf("Binding UDP Socket...\n");
    int bind_result = bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(bind_result == -1){
        perror("Failed to bind...");

        return -1;
    }

    printf("Listening to UDP Socket...\n");

    return server_socket;
}

SOCKET create_socket(char* hostname, char* port){
   
    printf("Configuring Local Address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
    struct addrinfo *bind_address;
    if(getaddrinfo(hostname, port, &hints, &bind_address)){
        fprintf(stderr, "getaddrinfo() failed with error: %d", GETSOCKETERRNO());
        exit(1);
    }

    printf("Creating TCP Socket...\n");
    SOCKET socket_listen;
    socket_listen = socket(bind_address->ai_family, bind_address->ai_socktype, bind_address->ai_protocol);
    if(!ISVALIDSOCKET(socket_listen)){
        fprintf(stderr, "socket() failed with error: %d", GETSOCKETERRNO());
        exit(1);
    }

    char c = 1;
    setsockopt(socket_listen, SOL_SOCKET, SO_REUSEADDR, &c, sizeof(int));
    
    printf("Binding TCP Socket...\n");
    if(bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen)){
        fprintf(stderr, "bind() failed with error: %d", GETSOCKETERRNO());
        CLOSESOCKET(socket_listen);
        exit(1);
    }

    printf("Listening to TCP Socket...\n");
    if(listen(socket_listen, 10) > 0){
        fprintf(stderr, "listen() failed with error: %d", GETSOCKETERRNO());
        exit(1);
    }
    freeaddrinfo(bind_address);

    return socket_listen;

}

fd_set wait_on_clients(struct client_info_t* clients, SOCKET server){

    struct timeval select_wait;
    select_wait.tv_sec = 0;
    select_wait.tv_usec = 100000; 

    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(server, &reads);
    SOCKET max_socket = server;

    // Push all the clients to reads
    struct client_info_t* client = clients;
    while(client){
        FD_SET(client->tcp_socket, &reads);
        if(client->tcp_socket > max_socket){
            max_socket = client->tcp_socket;
        }
        client = client->next;
    }

    // Wait on sockets
    //printf("This select. \n");
    if(select(max_socket+1, &reads, 0, 0, &select_wait) < 0){
        fprintf(stderr, "this1 select() failed with error: %d", GETSOCKETERRNO());
        exit(1);
    }
    
    return reads;

}

fd_set wait_on_udp_clients(struct client_info_t* clients, SOCKET server){

    struct timeval select_wait;
    select_wait.tv_sec = 0;
    select_wait.tv_usec = 100000; 

    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(server, &reads);
    SOCKET max_socket = server;

    // Push all the clients to reads
    struct client_info_t* client = clients;
    while(client){
        FD_SET(client->udp_socket, &reads);
        if(client->udp_socket > max_socket){
            max_socket = client->udp_socket;
        }
        client = client->next;
    }

    // Wait on sockets
    //printf("This select. \n");
    if(select(max_socket+1, &reads, 0, 0, &select_wait) < 0){
        fprintf(stderr, "this select() failed with error: %d", GETSOCKETERRNO());
        exit(1);
    }
    
    return reads;

}

struct client_info_t* get_client(struct client_info_t** clients, SOCKET socket){
    
    // look for the client in the linked list
    struct client_info_t* client = *clients;
    while(client){

        if(client->tcp_socket == socket){
            return client;
        }
        client = client->next;
    }

    // client not found, make one
    struct client_info_t* new_client = (struct client_info_t*) malloc(sizeof(struct client_info_t));
    if(new_client == NULL){
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }

    // Set struct to zero, this is mainly for the char request buffer (since 0 is the NULL Terminator)
    memset(new_client, 0, sizeof(struct client_info_t));

    // Initialize local variables
    new_client->address_length = sizeof(new_client->address);
    new_client->received = 0;
    new_client->message_size = -1;

    // Push to clients list
    new_client->next = *clients;
    *clients = new_client;

    return new_client;


}

struct client_info_t* get_udp_client(struct client_info_t** clients, SOCKET socket){
    
    // look for the client in the linked list
    struct client_info_t* client = *clients;
    while(client){

        if(client->udp_socket == socket){
            return client;
        }
        client = client->next;
    }

    // client not found, make one
    struct client_info_t* new_client = (struct client_info_t*) malloc(sizeof(struct client_info_t));
    if(new_client == NULL){
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }

    // Set struct to zero, this is mainly for the char request buffer (since 0 is the NULL Terminator)
    memset(new_client, 0, sizeof(struct client_info_t));

    // Initialize local variables
    new_client->address_length = sizeof(new_client->address);
    new_client->received = 0;
    new_client->message_size = -1;

    // Push to clients list
    new_client->next = *clients;
    *clients = new_client;

    return new_client;


}

const char* get_client_address(struct client_info_t* client){
    static char address_buffer[100];
    getnameinfo((struct sockaddr*) &client->address, client->address_length, address_buffer, sizeof(address_buffer), 0, 0, NI_NUMERICHOST);
    return address_buffer;
}

const char* get_client_udp_address(struct client_info_t* client){
    static char address_buffer[100];
    getnameinfo((struct sockaddr*) &client->udp_addr, client->address_length, address_buffer, sizeof(address_buffer), 0, 0, NI_NUMERICHOST);
    return address_buffer;
}

void drop_client(struct client_info_t** clients, struct client_info_t* client){
    
    CLOSESOCKET(client->tcp_socket);

    struct client_info_t** p = clients;

    while(*p){
        if(*p == client){
            *p = client->next;
            free(client);
            return;
        }
        p = & ((*p)->next);
    }

    fprintf(stderr, "drop_client: client not found\n");
    exit(1);

}

void drop_udp_client(struct client_info_t** clients, struct client_info_t* client){
    
    //CLOSESOCKET(client->udp_socket);

    struct client_info_t** p = clients;

    while(*p){
        if(*p == client){
            *p = client->next;
            free(client);
            return;
        }
        p = & ((*p)->next);
    }

    fprintf(stderr, "drop_client: client not found\n");
    exit(1);

}
