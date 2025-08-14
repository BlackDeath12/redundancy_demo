#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>

#define PORT 2020
#define MAX_DESTINATION_SIZE 16
#define MAX_BUFFER 1024

#define CMD_USAGE "Usage: ./server [Arguments (Optinal)] [local addr] [peer addr] or ./server -p [local addr]\n"

int main(int argc, char** argv){

    signal(SIGPIPE, SIG_IGN);

    char* local_addr;
    bool primary_computer = false;
    bool destination_required = false;
    char* destination_addr;

    int message_counter = 0;

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
        local_addr = argv[argIdx];
        argIdx++;

        if(!primary_computer){

            if(argc < 4 && argIdx == 3){
                printf(CMD_USAGE);
                return 1;
            }

            destination_addr = argv[argIdx];
        }
    }
    else{
        printf(CMD_USAGE);
        return 1;
    }

    primary_computer ? printf("Running as primary computer...\n") : printf("Running as secondary computer...\n");

    time_t startTime;
    time(&startTime);

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    char buffer[MAX_BUFFER] = {0};

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(local_addr);
    server_addr.sin_port = htons(PORT);

    server_fd = socket(server_addr.sin_family, SOCK_STREAM, 0);
    if (server_fd < 0){
        printf("Failed to create server socket!\n");
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        exit(1);
    }

    client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0){
        printf("Failed to create client socket!\n");
        fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        exit(1);
    }

    int optval = 1;
    int option_result = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if(option_result < 0){
        printf("Error setting socket options!\n");
    }

    int result = bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0){
        printf("Failed to bind socket!: %d\n", result);
        close(server_fd);
        return 1;
    }

    char reply_buffer[] = "alive";
    if(primary_computer){
        
        if(listen(server_fd, 4) < 0){
            printf("Error listening socket\n");
            close(server_fd);
            return 1;
        }

        printf("Listening for messages on %s:%d...\n", local_addr, PORT);

        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd < 0){
            printf("Error creating client!\n");
        }

        printf("accepted client\n");
    }
    else{

        client_addr.sin_addr.s_addr = inet_addr(destination_addr);
        client_addr.sin_port = htons(PORT);
        client_addr.sin_family = AF_INET;

        int result = connect(client_fd, (struct sockaddr*)&client_addr, sizeof(client_addr));
        if(result < 0){
            printf("Error connecting to client!\n");
            fprintf(stderr, "client connection failed: %s\n", strerror(errno));
            exit(1);
            return 1;
        }

        printf("Connected to %s:%d\n", destination_addr, ntohs(client_addr.sin_port));

        char initial_reply[] = {"alive"};
        ssize_t sent_bytes = send(client_fd, initial_reply, sizeof(initial_reply), 0);
        if (sent_bytes < 0){
            printf("Error sending buffer to client!\n");
            fprintf(stderr, "buffer send failed: %s\n", strerror(errno));
            exit(1);
        }
    }

    time_t currentTime;
    while(1){
        time(&currentTime);
        if(currentTime - startTime < 1){
            continue;
        }
        else{
            startTime = currentTime;
        }

        printf("Listening...\n");
        int bytes_received = recv(client_fd, buffer, MAX_BUFFER, 0);
        if(bytes_received <= 0){
            printf("Error receiving last message!\n");
            fprintf(stderr, "message error: %s\n", strerror(errno));

            printf("Switching to backup\n");
            system("./script.sh");
            break;
        }
        else{
            message_counter++;
            printf("Total messages: %d, just recevied from ", message_counter);
            for(int i = 0; i < bytes_received; i++){
                printf("%c", buffer[i]);
            }
            printf("\n");

            ssize_t sent_bytes = send(client_fd, reply_buffer, sizeof(reply_buffer), 0);
            if (sent_bytes < 0){
                printf("Error sending buffer to client!\n");
            }
        }
    }

    close(client_fd);
    close(server_fd);

    return 0;
}