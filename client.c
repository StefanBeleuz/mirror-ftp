#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <dirent.h>
#include <string.h>

#define BUFF_SIZE 2048

int sockfdControl; // socket for control connection (commands, replies)
int sockfdData;	// socket for data connection (file transfer)

struct sockaddr_in serverControl; // serverAddr for control connection (commands, replies)
struct sockaddr_in serverData;	// serverAddr for data connection (file transfer)

int portControl; // port for control connection (commands, replies)
int portData; // port for data connection (file transfer)

char addr[16]; // server's ip address


/* get size of a file */
int getSize(const char *fileName) {
    struct stat st; 
     
    if(stat(fileName, &st)==0)
        return (st.st_size);
    else
        return -1;
}


/* get file's content */
int getContent(char* fileName, int* content, int contentSize) {
    int fd = open(fileName, O_RDONLY);
    if(-1 == fd) {
        perror("Error on openning file");
        return -1;
    }

    memset(content, 0, contentSize);
    
    if(-1 == read(fd, content, contentSize)) {
        perror("Error on reading from file");
        return -1;
    }

    close(fd);

    return 1;
}


int connectToServer() {
    /* create socket */
    if((sockfdControl = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error on socketControl");
        return -1;
    }

    /* fill server's structure */
    serverControl.sin_family = AF_INET;
    serverControl.sin_addr.s_addr = inet_addr(addr);
    serverControl.sin_port = htons(portControl);
    
    /* connect to server */
    if(connect(sockfdControl, (struct sockaddr*)&serverControl, sizeof(struct sockaddr)) == -1) {
        perror("Error on connectControl");
        return -1;
    }

    return 1;
}


int main(int argc, char *argv[]) {
    if(argc != 3) {
        printf("Use: %s <Server Address> <Port>\n", argv[0]);
        return -1;
    }

    memcpy(addr, argv[1], strlen(argv[1]) + 1);
    portControl = atoi(argv[2]);

    int user = 0, pass = 0, rnfr = 0, pasv = 0;

    if(connectToServer() == 1) {
        printf("Welcome!\nConnect to server using USER <username> followed by PASS <password>\n");
        while(1) {
            char command[64]; memset(command, 0, 64);
            char message[256]; memset(message, 0, 256);

            printf("Insert command: ");
            fgets(command, 64, stdin);
            command[strlen(command) - 1] = '\0';

            char _command[64]; memset(_command, 0, 64);
            char  _parameter[64];  memset(_parameter, 0, 64);
            sscanf(command, "%s %s ", _command, _parameter);
            printf("%s %s\n", _command, _parameter);

            if(strcmp(_command, "USER") == 0) {
                if(user == 0) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message);

                    if(strcmp(message, "Username found!") == 0) {
                        user = 1;
                    }
                }
                else {
                    printf("USER already entered!\n");
                }
            }

            else if(strcmp(_command, "PASS") == 0) {
                if(user == 1 && pass == 0) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }
                    
                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message);

                    if(strcmp(message, "Password found!") == 0) {
                        pass = 1;
                    }
                }
                else if(user == 0) {
                    printf("USER first!\n");
                }
                else {
                    printf("Already logged in!\n");
                }
            }

            else if(strcmp(_command, "PASV") == 0) {
                if(user == 1 && user == 1 && pasv == 0) {
                    int ok = 1;
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    /* get data port from server */
                    if(-1 == read(sockfdControl, &portData, sizeof(int))) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("Conecting to port %d...\n", portData);

                    /* create socket for data connection */
                    if((sockfdData = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                        perror("Error on socketData");
                        ok = -1;
                    }

                    /* fill "data" server's structure */
                    serverData.sin_family = AF_INET;
                    serverData.sin_addr.s_addr = inet_addr(addr);
                    serverData.sin_port = htons(portData);

                    /* connect to "data" server */
                    if(connect(sockfdData, (struct sockaddr *) &serverData, sizeof(struct sockaddr)) == -1) {
                        perror("Error on connectControl");
                        ok = -1;
                    }

                    if(ok == 1) { /* connected successfully */
                        pasv = 1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message);
                }
                else if(user == 1 && user == 1 && pasv == 1) {
                    printf("Data connecton already established!\n");
                }
                else if(user == 0 || pass == 0) {
                    printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "QUIT") == 0) {
                if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                    perror("Error on writing command to server");
                    return -1;
                }
                printf("GoodBye!\n");

                close(sockfdControl);
                close(sockfdData);
                break;
            }

            else if(strcmp(_command, "COPY") == 0) { /* 195.144.107.198 */
                if(user == 1 && pass == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }
                    printf("Copying site...\n"); fflush(stdout);

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message);
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "REFRESH") == 0) { /* 195.144.107.198 */
                if(user == 1 && pass == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }
                    printf("Refreshing site...\n"); fflush(stdout);

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message);
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "CWD") == 0) {
                if(user == 1 && pass == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message);
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "PWD") == 0) {
                if(user == 1 && pass == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("Working Directory: %s\n", message);
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "RETR") == 0) {
                if(user == 1 && pass == 1 && pasv == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message); fflush(stdout);

                    if(strcmp(message, "Failed to get file from server!") != 0) {
                        /* get file content's size */
                        int contentSize;
                        if(-1 == read(sockfdData, &contentSize, sizeof(int))) {
                            perror("Error on reading from sockfdData");
                            return -1;
                        }

                        /* get file content */
                        int* content = (int*)malloc(contentSize * sizeof(int));
                        if(-1 == read(sockfdData, content, contentSize)) {
                            perror("Error on reading from sockfdData");
                            return -1;
                        }

                        /* create and copy content localy */
                        int fd = open(_parameter, O_WRONLY | O_CREAT, 0777);
                        if(-1 == write(fd, content, contentSize)) {
                            perror("Error on reading from file");
                            return -1;
                        }

                        free(content);
                        close(fd);

                        read(sockfdControl, message, 256);
                        printf("%s\n", message); fflush(stdout);
                    }
                }
                else if(user == 1 && pass == 1 && pasv == 0) {
                    printf("Please set up data connection, using PASV!\n");
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "STOR") == 0) {
                if(user == 1 && pass == 1 && pasv == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    int contentSize = getSize(_parameter);
                    if(-1 == contentSize) {
                        printf("File doesn't exist!\n");
                        strcpy(message, "File doesn't exist!");

                        if(-1 == write(sockfdControl, message, strlen(message) + 1)) {
                            perror("Error on writing message to server");
                            return -1;
                        }
                    } 
                    else {
                        int* content = (int*)malloc(contentSize * sizeof(int));

                        if(-1 == getContent(_parameter, content, contentSize)) {
                            printf("Failed to get file's content!\n");
                            strcpy(message, "Failed to get file's content!");

                            if(-1 == write(sockfdControl, message, strlen(message) + 1)) {
                                perror("Error on writing message to server");
                                return -1;
                            }
                        }
                        else {
                            strcpy(message, "File's content got successfully!");

                            if(-1 == write(sockfdControl, message, strlen(message) + 1)) {
                                perror("Error on writing message to server");
                                return -1;
                            }

                            if(-1 == write(sockfdData, &contentSize, sizeof(int))) {
                                perror("Error on writing to sockfdData");
                                return -1;
                            }

                            if(-1 == write(sockfdData, content, contentSize)) {
                                perror("Error on writing to sockfdData");
                                return -1;
                            }

                            free(content);

                            if(-1 == read(sockfdControl, message, 256)) {
                                perror("Error on reading reply from server");
                                return -1;
                            }
                            printf("%s\n", message);
                        }
                    }
                }
                else if(user == 1 && pass == 1 && pasv == 0) {
                    printf("Please set up data connection, using PASV!\n");
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "RNFR") == 0) {
                if(user == 1 && pass == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message);
                    rnfr = 1;
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "RNTO") == 0) {
                if(user == 1 && pass == 1 && rnfr == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }

                    printf("%s\n", message);
                    rnfr = 0;
                }
                else if(rnfr == 0) {
                    printf("Please specity the old name of file to rename, using RNFR!\n");
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "DELE") == 0) {
                if(user == 1 && pass == 1) { 
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }

                    printf("%s\n", message);
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "RMD") == 0) { 
                if(user == 1 && pass == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    while(1) {
                        if(-1 == read(sockfdControl, message, 256)) {
                            perror("Error on reading reply from server");
                            return -1;
                        }
                        printf("%s\n", message);

                        if(strcmp(message, "Directory successfully removed!") == 0 || 
                            strcmp(message, "Failed to remove directory") == 0 ||
                            strcmp(message, "Wrong input!") == 0) break;
                    }
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "MKD") == 0) {
                if(user == 1 && pass == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message);
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "LIST") == 0) {
                if(user == 1 && pass == 1 && pasv == 1) {
                    if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                        perror("Error on writing command to server");
                        return -1;
                    }

                    if(-1 == read(sockfdControl, message, 256)) {
                        perror("Error on reading reply from server");
                        return -1;
                    }
                    printf("%s\n", message); fflush(stdout);

                    if(strcmp(message, "Content of directory listed successfully!") == 0) {
                        char* messageList = (char*)malloc(BUFF_SIZE);

                        if(-1 == read(sockfdData, messageList, BUFF_SIZE)) {
                            perror("Error on reading message from server");
                            return -1;
                        }
                        printf("%s", messageList); fflush(stdout);

                        free(messageList);
                    }
                }
                else if(user == 1 && pass == 1 && pasv == 0) {
                    printf("Please set up data connection, using PASV!\n");
                }
                else {
                     printf("Please log in first!\n");
                }
            }

            else if(strcmp(_command, "HELP") == 0) {
                if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                    perror("Error on writing command to server");
                    return -1;
                }

                char* messageHelp = (char*)malloc(BUFF_SIZE); 
                if(-1 == read(sockfdControl, messageHelp, BUFF_SIZE)) {
                    perror("Error on reading message from server");
                    return -1;
                }
                printf("%s", messageHelp); fflush(stdout);
                free(messageHelp);
            }

            else if(strcmp(_command, "NOOP") == 0) {
               if(-1 == write(sockfdControl, command, strlen(command) + 1)) {
                    perror("Error on writing command to server");
                    return -1;
                }

                if(-1 == read(sockfdControl, message, 256)) {
                    perror("Error on reading reply from server");
                    return -1;
                }
                printf("%s\n", message);
            }

            else printf("Unknown command!\n");
        }

        close(sockfdControl);
        close(sockfdData);
    }
    else {
        perror("Error to connect to server");
        return -1;
    }

    return 0;
}