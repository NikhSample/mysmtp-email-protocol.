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

// server port
#define PORT 2525

// assumed max tasks and clients as 100
#define MAX_CLIENTS 100

#define BUFFER_SIZE 1024
// assumed max email size to be 4096
#define MAX_EMAIL_SIZE 4096
#define MAX_LEN 256

// timeout for client inactivity (used 10min but can be changed)
#define CLIENT_TIMEOUT 600  

// sem ops
#define P(s) { struct sembuf op = {0, -1, 0}; semop(s, &op, 1); }
#define V(s) { struct sembuf op = {0, 1, 0}; semop(s, &op, 1); }

// Response codes
#define OK "200 OK\n"
#define ERR_SYNTAX "400 ERR\n"
#define ERR_NOT_FOUND "401 NOT FOUND\n"
#define ERR_FORBIDDEN "403 FORBIDDEN\n"
#define ERR_SERVER "500 SERVER ERROR\n"

// states for SMTP server
#define INIT 0
#define HELO 1
#define FROM 2
#define TO 3
#define DATA 4

// id for sem and sm
int sem_id;

// server socket
int serverfd;

// child pids and their tasks (parent handles these)
pid_t child_pids[MAX_CLIENTS];
int child_task[MAX_CLIENTS]; 
int client_cnt = 0;

/*
    SIGNAL HANDLERS
*/

// zombie killer
void handle_SIGCHLD(){
    int curr_err = errno;
    pid_t pid;
    int status;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < client_cnt; i++) {
            if (child_pids[i] == pid) {
                printf("\t\tClient with PID: %d terminated\n", pid);

                if (child_task[i] >= 0) {
                    child_task[i] = -1;
                }
                
                for (int j = i; j < client_cnt - 1; j++) {
                    child_pids[j] = child_pids[j + 1];
                    child_task[j] = child_task[j + 1];
                }
                client_cnt--;
                break;
            }
        }
    }
    
    errno = curr_err;
}

// SIGINT handler
void handle_SIGINT(int sig) {
    printf("\n Interrupt detected. Exiting...\n");
    semctl(sem_id, 0, IPC_RMID);
    close(serverfd);
    exit(0);
}

/*
    HELPER FUNCTIONS
*/

// creates mailbox directory
void chk_dir() {
    struct stat st = {0};
    if (stat("mailbox", &st) == -1) mkdir("mailbox", 0700);
}

// checks and validates domain
int chk_domain(char* domain, char* buffer, int clientsockfd){
    char *at_pos = strchr(buffer, '@');
    if(at_pos == NULL){ 
        send(clientsockfd, ERR_SYNTAX, strlen(ERR_SYNTAX), 0);
        return 0;
    }
    char *dom = at_pos + 1; 
    if(strncmp(dom, domain, strlen(domain)) != 0 || strlen(dom) != strlen(domain)){ 
        send(clientsockfd, ERR_FORBIDDEN, strlen(ERR_FORBIDDEN), 0);
        return 0;
    }
    return 1;
}

// store mail 
int store_mail(char *from, char *to, char *msg) {

    chk_dir();

    // mutex lock to ensure no two processes write to the same file
    // can use file wise locking too if speed is a concern
    P(sem_id);
    
    char file[100];
    snprintf(file, sizeof(file), "mailbox/%s.txt", to);
    
    FILE *fp = fopen(file, "a");
    if(!fp) {
        V(sem_id);
        return 0;
    }
    
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char date[20];
    strftime(date, sizeof(date), "%d-%m-%Y", t);

    fprintf(fp, "***SOM***\n");
    fprintf(fp, "From: %s\n", from);
    fprintf(fp, "Date: %s\n", date);
    fprintf(fp, "%s\n", msg);
    fprintf(fp, "***EOM***\n");

    V(sem_id);
    
    fclose(fp);
    return 1;
}

// list all mails for a recipient
char* list_mail(char *recipient) {

    char file[100];
    snprintf(file, sizeof(file), "mailbox/%s.txt", recipient);
    
    FILE *fp = fopen(file, "r");
    if (!fp) return NULL;
    
    static char mails[MAX_EMAIL_SIZE];
    memset(mails, 0, sizeof(mails));
    
    char line[256], sender[100], date[20];
    // flag validates if email is stored correctly
    int cnt = 0, flag = 0;
    
    strcpy(mails, OK);
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "***SOM***", 9) == 0) {
            cnt++;
            flag = 1;
        }    
        else if (flag && strncmp(line, "From: ", 6) == 0) {
            strncpy(sender, line + 6, sizeof(sender) - 1);
            sender[strcspn(sender, "\n")] = 0;
        }
        else if (flag && strncmp(line, "Date: ", 6) == 0) {
            strncpy(date, line + 6, sizeof(date) - 1);
            date[strcspn(date, "\n")] = 0;
            
            char info[200];
            snprintf(info, sizeof(info), "%d: Email from %s (%s)\n", cnt, sender, date);
            strcat(mails, info);
        }
        else if (flag && strncmp(line, "***EOM***", 9) == 0) flag = 0;
    }
    
    fclose(fp);
    
    if (cnt == 0) strcat(mails, "No emails found.\n");
    
    return mails;
}

// get mail by id of recipient
char* get_mail(char *recipient, int id) {

    if(id < 1) return NULL;

    char file[100];
    snprintf(file, sizeof(file), "mailbox/%s.txt", recipient);
    
    FILE *fp = fopen(file, "r");
    if(!fp) return NULL;
    
    static char mail[MAX_EMAIL_SIZE];
    memset(mail, 0, sizeof(mail));
    
    char line[256];
    int scnt = 0, ecnt = 0, found = 0;
    
    strcpy(mail, OK);

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "***SOM***", 9) == 0) {
            scnt++;
            if (scnt == id) found = 1;
        }
        else if (strncmp(line, "***EOM***", 9) == 0) {
            ecnt++;
            if(found){
                found = 0;
                break;
            }    
        }
        else if (found) {
            strcat(mail, line);
        }
    }
    
    fclose(fp);

    // if email is corrupted
    if(ecnt != scnt){
        memset(mail, 0, sizeof(mail));
        strcpy(mail, ERR_SERVER);
    }
    
    if (scnt < id) return NULL;
    
    return mail;
}

/*
    CLIENT HANDLER
*/

// handle client requests
void handle_client(int clientsockfd, int client_id) {
    char buffer[BUFFER_SIZE], cmd[MAX_LEN], arg[MAX_LEN];
    char  domain[MAX_LEN] = "", sender[MAX_LEN] = "", recipient[MAX_LEN] = "", data[MAX_EMAIL_SIZE] = "";
    int state = INIT;
    
    int n, id, maxi = 0;
    time_t last_active = time(NULL);
    
    // Set socket to non-blocking I/O
    int flags = fcntl(clientsockfd, F_GETFL, 0);
    fcntl(clientsockfd, F_SETFL, flags | O_NONBLOCK);
    
    // printf("Child process %d handling client %d\n", getpid(), client_id);
    
    while (1) {
        // Check for timeout
        time_t curr_time = time(NULL);
        if (curr_time - last_active > CLIENT_TIMEOUT) {
            printf("Client %d timed out after %d seconds of inactivity\n", client_id, CLIENT_TIMEOUT);
            close(clientsockfd);
            exit(0);
        }
        
        // Read from socket
        memset(buffer, 0, BUFFER_SIZE);
        n = recv(clientsockfd, buffer, BUFFER_SIZE - 1, 0);
        
        if (n > 0) {
            last_active = time(NULL);
            buffer[n] = '\0';
            
            buffer[strcspn(buffer, "\n")] = 0;
            
            // printf("Received: %s\n", buffer);
            
            // read email data
            if (state == DATA) {
                
                if (strcmp(buffer, ".") == 0) {
                    state = HELO;

                    if(maxi == 1) {
                        send(clientsockfd, ERR_FORBIDDEN, strlen(ERR_FORBIDDEN), 0);
                    }
                    else if (store_mail(sender, recipient, data)) {
                        send(clientsockfd, "200 Message stored successfully.\n", 33, 0);
                        printf("DATA recieved, message stored.\n");
                    } else {
                        send(clientsockfd, ERR_SERVER, strlen(ERR_SERVER), 0);
                    }
                    
                    memset(data, 0, sizeof(data));
                    maxi = 0;
                } else if(!maxi) {
                    int data_len = strlen(data), buffer_len = strlen(buffer);

                    // if email size exceeds MAX_EMAIL_SIZE
                    if (data_len + buffer_len + 1 >= sizeof(data)) {maxi = 1;
                        printf("Email size exceeded %d\n", maxi);
                    }
                    else {
                        strcat(data, buffer);
                        strcat(data, "\n");
                    }
                }
                continue;
            }
    
            memset(cmd, 0, MAX_LEN); memset(arg, 0, MAX_LEN);
            
            // get cmd and arg from buffer
            if (sscanf(buffer, "%s %[^\n]", cmd, arg) < 1) {
                send(clientsockfd, ERR_SYNTAX, strlen(ERR_SYNTAX), 0);
                continue;
            }
            
            // init connection
            if (strcmp(cmd, "HELO") == 0) {
                state = HELO;

                if(strlen(arg) == 0){
                    send(clientsockfd, ERR_SYNTAX, strlen(ERR_SYNTAX), 0);
                    continue;
                }

                strcpy(domain, arg);
                printf("HELO received from %s\n", arg);

                send(clientsockfd, OK, strlen(OK), 0);
            }
            // mail from
            else if(strncmp(buffer, "MAIL FROM:", 10) == 0){
                if(state == INIT || state == TO || state == DATA){
                    send(clientsockfd, ERR_FORBIDDEN, strlen(ERR_FORBIDDEN), 0);
                    continue;
                }
            
                if(!chk_domain(domain, buffer, clientsockfd)) continue;

                strcpy(sender, buffer + 11);
                state = FROM;
                printf("%s\n", buffer);
                
                send(clientsockfd, OK, strlen(OK), 0);
            }
            // rcpt to
            else if(strncmp(buffer, "RCPT TO:", 8) == 0){
                if(state != FROM){
                    send(clientsockfd, ERR_FORBIDDEN, strlen(ERR_FORBIDDEN), 0);
                    continue;
                }

                if(!chk_domain(domain, buffer, clientsockfd)) continue;

                strcpy(recipient, buffer + 9);
                state = TO;
                printf("%s\n", buffer);

                send(clientsockfd, OK, strlen(OK), 0);
            }
            // data
            else if (strcmp(cmd, "DATA") == 0) {
                if (state != TO) {
                    send(clientsockfd, ERR_FORBIDDEN, strlen(ERR_FORBIDDEN), 0);
                    continue;
                }
                
                state = DATA;
                send(clientsockfd, "Enter your message (end with a single dot '.'):\n", 48, 0);
            }
            // list mails
            else if (strcmp(cmd, "LIST") == 0) {
                if (state != HELO) {
                    send(clientsockfd, ERR_FORBIDDEN, strlen(ERR_FORBIDDEN), 0);
                    continue;
                }

                if(!chk_domain(domain, buffer, clientsockfd)) continue;

                char *list = list_mail(arg);
                if (list) {
                    send(clientsockfd, list, strlen(list), 0);
                    printf("%s\n", buffer);
                    printf("Emails retrieved; list sent.\n");
                } else {
                    send(clientsockfd, ERR_NOT_FOUND, strlen(ERR_NOT_FOUND), 0);
                }
            }
            // get mail
            else if (strcmp(cmd, "GET_MAIL") == 0) {
                if (state != HELO) {
                    send(clientsockfd, ERR_FORBIDDEN, strlen(ERR_FORBIDDEN), 0);
                    continue;
                }

                char recipient[MAX_LEN];
                
                if (sscanf(arg, "%s %d", recipient, &id) != 2) {
                    send(clientsockfd, ERR_SYNTAX, strlen(ERR_SYNTAX), 0);
                    continue;
                }

                if(!chk_domain(domain, recipient, clientsockfd)) continue;
                
                char *mail = get_mail(recipient, id);
                if (mail) {
                    send(clientsockfd, mail, strlen(mail), 0);
                    // if server error(due to corrupted email)
                    if(mail[0] == '5') continue;
                    
                    printf("%s\n", buffer);
                    printf("Email with id %d sent.\n", id); 
                } else {
                    send(clientsockfd, ERR_NOT_FOUND, strlen(ERR_NOT_FOUND), 0);
                }
            }
            // quit
            else if (strcmp(cmd, "QUIT") == 0) {
                send(clientsockfd, "200 Goodbye\n", 13, 0);
                printf("Client disconnected.\n");

                close(clientsockfd);
                exit(0);
            }
            // invalid command
            else {
                send(clientsockfd, ERR_SYNTAX, strlen(ERR_SYNTAX), 0);
            }
        }
        else if(n == 0) {
            printf("Client disconnected.\n");
            close(clientsockfd);
            exit(0);
        }
        else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("Error reading from socket");
                close(clientsockfd);
                exit(1);
            }
        }
        
        usleep(10000);
    }
}

/*
    MAIN FUNCTION
*/

int main(int argc, char *argv[]) {
    int port = PORT;  
    if (argc > 1) port = atoi(argv[1]);
    
    // signal handler for zombie killer
    struct sigaction sa;
    sa.sa_handler = handle_SIGCHLD;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        perror("Error: signal handler");
        return 1;
    }
    
    // signal handler for SIGINT
    signal(SIGINT, handle_SIGINT);
    
    key_t key = ftok("/", 66);
    
    // semaphore
    sem_id = semget(key, 1, IPC_CREAT | 0666);

    if (sem_id == -1) {
        perror("Error: semget");
        exit(1);
    }
    
    // init sem
    semctl(sem_id, 0, SETVAL, 1);
    
    // create socket
    struct sockaddr_in serv_addr;
    int len = sizeof(serv_addr);
    
    if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Error: socket failed");
        semctl(sem_id, 0, IPC_RMID);
        exit(1);
    }
    
    // make socket reusable
    int opt = 1;
    if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Error: setsockopt failed");
        semctl(sem_id, 0, IPC_RMID);
        exit(1);
    }
    
    // bind socket
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    
    if (bind(serverfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error: bind failed");
        semctl(sem_id, 0, IPC_RMID);
        exit(1);
    }
    
    // listen for connections(assuming max 30)
    listen(serverfd, 30);

    chk_dir();
    
    printf("Listening on port %d...\n", port);
    
    while (1) {
        struct sockaddr_in clien_addr;
        socklen_t len = sizeof(clien_addr);
        int clientsockfd = accept(serverfd, (struct sockaddr *)&clien_addr, &len);
        
        if (clientsockfd < 0) {
            perror("Error: accept failed");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clien_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Client connected: %s\n", client_ip);
        
        if (client_cnt >= MAX_CLIENTS) {
            // Server full
            send(clientsockfd, ERR_SERVER, strlen(ERR_SERVER), 0);
            close(clientsockfd);
            continue;
        }
        
        pid_t pid = fork();
        
        if (pid == 0) {
            close(serverfd); 
            handle_client(clientsockfd, client_cnt);
            exit(0);
        } 
        else if (pid > 0) {
            close(clientsockfd);  
            child_pids[client_cnt] = pid;
            child_task[client_cnt] = -1;
            client_cnt++;
        }
        else {
            perror("Error: fork failed");
            close(clientsockfd);
        } 
    }

    // not needed tho
    close(serverfd);
    semctl(sem_id, 0, IPC_RMID);
    return 0;
}