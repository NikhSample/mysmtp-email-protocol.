#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// to ensure max email size is also read
#define BUFFER_SIZE 4096
// maximum cmd length (including data lines)
#define MAX_LEN 256

int sockfd = -1;

// Signal handler for SIGINT
void handle_sigint(int sig) {
    printf("\nExiting...\n");
    if (sockfd != -1) close(sockfd);
    exit(0);
}

void send_mail(int sockfd) {
    char buffer[BUFFER_SIZE], line[MAX_LEN];
    
    if (send(sockfd, "DATA\n", 6, 0) < 0) {
        perror("Error: send failed");
        return;
    }
    
    memset(buffer, 0, BUFFER_SIZE);
    int n, flag = 0;
    
    if ((n = recv(sockfd, buffer, BUFFER_SIZE - 1, 0)) <= 0) {
        perror("Error: recv failed");
        return;
    }
    
    buffer[n] = '\0';
    printf("%s", buffer);
    
    // get mail data from terminal
    while (!flag) {
        if (fgets(line, MAX_LEN, stdin) == NULL) break;
        
        line[strlen(line) - 1] = '\0';
        if (strcmp(line, ".") == 0) flag = 1;

        write(sockfd, line, strlen(line));
    }
    
    // get reply for the sent mail
    memset(buffer, 0, BUFFER_SIZE);
    if ((n = recv(sockfd, buffer, BUFFER_SIZE - 1, 0)) <= 0) {
        perror("Error: recv failed");
        return;
    }
    buffer[n] = '\0';
    printf("%s", buffer);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE], cmd[MAX_LEN];
    
    signal(SIGINT, handle_sigint);
    
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(1);
    }
    
    // create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error: socket failed");
        exit(1);
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("Error: invalid ip");
        close(sockfd);
        exit(1);
    }
    
    // connect to server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error: connect failed | server not running");
        close(sockfd);
        exit(1);
    }
    
    int flag = 0, n;
    printf("Connected to My_SMTP server.\n");
    
    while (1) {
        printf("> ");
        if (fgets(cmd, MAX_LEN, stdin) == NULL) break;
        
        cmd[strlen(cmd) - 1] = '\0';
        if (strlen(cmd) == 0) continue;
        
        strcat(cmd, "\n");
        
        if (strncmp(cmd, "DATA", 4) == 0) {
            send_mail(sockfd);
            continue;
        }
        if(strncmp(cmd, "QUIT", 4) == 0) flag = 1;
        
        // send cmd to server
        if (send(sockfd, cmd, strlen(cmd), 0) < 0) {
            perror("Error: send failed");
            exit(1);
        }
        
        // read reply from server
        memset(buffer, 0, BUFFER_SIZE);
        if((n = recv(sockfd, buffer, BUFFER_SIZE - 1, 0)) < 0) {
            perror("Error: recv failed");
            exit(1);
        }
        buffer[n] = '\0';
        printf("%s", buffer);
        
        if(strncmp(buffer, "200 Goodbye", 11) == 0 || flag) break;
    }
    
    // close and quit
    close(sockfd);
    return 0;
}