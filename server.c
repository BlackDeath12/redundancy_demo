#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>
#include "network.h"
#include <signal.h>

#define TCP_PORT 2020
#define UDP_PORT 2020
#define PEER_COMP_ADDR "10.0.2.11"
#define MAX_DESTINATION_SIZE 16
#define MAX_BUFFER 1024
#define ALIVE_MESSAGE "alive"

#define PEER_TIMEOUT 5
#define ALIVE_MSG_TIME 1
#define RECONNECT_TIME 1

#define CMD_USAGE "Usage: ./server [Arguments (Optinal)] [local addr] [peer addr]\n"

//Helper Functions
bool time_elapsed();
bool attempt_connection();

int main(int argc, char** argv){

    signal(SIGPIPE, SIG_IGN);

    char* local_addr;
    bool primary_computer = false;

    char* peer_comp_addr = PEER_COMP_ADDR;
    bool peer_connected = true;
    bool runScript = false;

    int message_counter = 0;

    int sim_case = -1;

    if(argc > 1){

        int argIdx = 1;
        if(argv[1][0] == '-'){
            int len = sizeof(argv[1]);
            for(int i = 0; i < len - 1; i++){
                switch(argv[1][i + 1])
                {
                case 'p':
                    primary_computer = true;
                    break;
                case '-':
                    printf("only one letter per argument\n");
                    return 1;
                default:
                    break;
                }
            }
            argIdx++;
        } 
        local_addr = argv[argIdx++];

        if(argc > argIdx){
            peer_comp_addr = argv[argIdx];
        }
    }
    else{
        printf(CMD_USAGE);
        return 1;
    }

    SOCKET server_sock, server_udp_sock;
    char port[6];
    sprintf(port, "%d", TCP_PORT);
    char udp_port[6];
    sprintf(udp_port, "%d", UDP_PORT);

    server_sock = create_socket(local_addr, port);
    server_udp_sock = create_udp_socket(local_addr, udp_port);

    primary_computer ? printf("\nRunning as primary computer...\n") : printf("\nRunning as secondary computer...\n");
    printf("Server started!\n\n");

    time_t startTime, last_peer_message, reconnect_timer,
    alive_timer;
    time(&startTime);
    time(&reconnect_timer);
    time(&last_peer_message);
    time(&alive_timer);

    struct client_info_t* clients = NULL;
    struct client_info_t* udp_clients = NULL;
    char alive_buffer[] = ALIVE_MESSAGE;

    if(!primary_computer) attempt_connection(&clients, peer_comp_addr, TCP_PORT, alive_buffer);

    while(true){
        fd_set fd, fd_udp;
        fd = wait_on_clients(clients, server_sock);
        fd_udp = wait_on_udp_clients(udp_clients, server_udp_sock);

        if(FD_ISSET(server_sock, &fd)){

            struct client_info_t* client = get_client(&clients, -1);

            // create client socket
            client->tcp_socket = accept(server_sock, (struct sockaddr*) &client->address, &client->address_length);
            if(!ISVALIDSOCKET(client->tcp_socket)){
                fprintf(stderr, "accept() failed with error: %d", GETSOCKETERRNO());
            }

            if(strcmp(get_client_address(client), local_addr)){
                printf("New Connection from %s\n", get_client_address(client));

            }
        }

        if(FD_ISSET(server_udp_sock, &fd_udp)){
            struct client_info_t* client = get_udp_client(&udp_clients, -1);
            
            memset(client->udp_request, 0, sizeof(client->udp_request));

            ssize_t received_bytes = recvfrom(server_udp_sock, client->udp_request, MAX_UDP_REQUEST_SIZE, 0, (struct sockaddr*)&client->udp_addr, &client->address_length);
            
            client->received = received_bytes;
            printf("Received datagram from: %s\n", get_client_udp_address(client));
            
        }

        struct client_info_t* client = clients;
        struct client_info_t* udp_client = udp_clients;
        while(client){
            
            struct client_info_t* next_client = client->next;

            // Check if this client has pending request
            if(FD_ISSET(client->tcp_socket, &fd)){

                // Request too big
                if(MAX_REQUEST_SIZE <= client->received){

                    drop_client(&clients, client);
                    client = next_client;
                    continue;
                }
                
                // read new bytes in
                int bytes_received = recv(client->tcp_socket, client->tcp_request + client->received, MAX_REQUEST_SIZE - client->received, 0);
                //printf("Recieved bytes: %d From: %s\n", bytes_received, get_client_address(client));
                if(bytes_received < 0){
                    printf("Error receiving last message!\n");
                }

                if(strcmp(get_client_address(client), peer_comp_addr) == 0 && strcmp(client->tcp_request, ALIVE_MESSAGE) == 0 && bytes_received > 0){
                    time_t currentTime;
                    time(&currentTime);
                    time(&last_peer_message);
                    peer_connected = true;
                    if(time_elapsed(&alive_timer, ALIVE_MSG_TIME)){
                        printf("Peer computer is alive! Elapsed time: %lds\n", time(NULL) - startTime);
                    }
                    send(client->tcp_socket, alive_buffer, sizeof(alive_buffer), 0);
                }
                else{
                    drop_client(&clients, client);
                }
                

            }

            client = next_client;
        }
        while (udp_client){

            struct client_info_t* next_client = udp_client->next;

            printf("Received UDP string: %s\n", udp_client->udp_request);

            if(strcmp(udp_client->udp_request, "reboot_primary") == 0){
                sim_case = REBOOT_PRIMARY;
            }
            else if(strcmp(udp_client->udp_request, "reboot_secondary") == 0){
                sim_case = REBOOT_SECONDARY;
            }
            else if(strcmp(udp_client->udp_request, "reboot_both") == 0){
                sim_case = REBOOT_BOTH;
            }

            drop_udp_client(&udp_clients, udp_client);
            
            udp_client = next_client;
        }

        if(time_elapsed(&last_peer_message, PEER_TIMEOUT)){
            printf("Peer disconnected!\n");
            peer_connected = false;
        }

        if(!peer_connected){
            if(!runScript){
                runScript = true;
                system("/home/spacecloud/redundancy_demo/script.sh");
            }

            if(!primary_computer && time_elapsed(&reconnect_timer, RECONNECT_TIME)){
                attempt_connection(&clients, peer_comp_addr, TCP_PORT, alive_buffer);
            }
        }
        else{
            if(runScript){
                runScript = false;
            }
        }

        switch (sim_case)
        {
        case REBOOT_PRIMARY:
            if(primary_computer){
                system("/home/spacecloud/redundancy_demo/reboot.sh");
            }
            sim_case = -1;

            break;
        case REBOOT_SECONDARY:
            if(!primary_computer){
                system("/home/spacecloud/redundancy_demo/reboot.sh");
            }
            sim_case = -1;

            break;
        case REBOOT_BOTH:
            system("/home/spacecloud/redundancy_demo/reboot.sh");
            sim_case = -1;

            break;
        default:

            sim_case = -1;
            break;
        }

    }

    close(server_udp_sock);
    close(server_sock);

    return 0;
}

bool time_elapsed(time_t* last_time, time_t total_time){
    time_t current_time;
    time(&current_time);

    time_t diff = current_time - *last_time;

    if(diff > total_time){
        *last_time = current_time;
        return true;
    }

    return false;
}

bool attempt_connection(struct client_info_t** clients, char* address, int port, char* buffer){
    struct client_info_t* client = get_client(clients, -1);
    client->tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    client->address.sin_addr.s_addr = inet_addr(address);
    client->address.sin_port = htons(port);
    client->address.sin_family = AF_INET;

    int result = connect(client->tcp_socket, (struct sockaddr*)&client->address, sizeof(client->address));
    if(result < 0){
        printf("Couldn't connect to server!\n");
        drop_client(clients, client);
    }
    else{
        send(client->tcp_socket, buffer, sizeof(buffer), 0);
    }
}