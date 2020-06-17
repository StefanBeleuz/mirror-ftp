#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <linux/limits.h>

char list[256][2][64];
int listSize;

#define BUFF_SIZE 2048

#define PORT_CONTROL 2020

int portData;

int sockfdControl; // socket for control connection (commands, replies)
int sockfdData; // socket for data connection (file transfer)

int clifdControl; // client fd for control connection (commands, replies)
int clifdData; // client sock for control connection (commands, replies)

struct sockaddr_in serverControl; // serverAddr for control connection (commands, replies)
struct sockaddr_in serverData; // serverAddr for data connection (file transfer)
struct sockaddr_in clientControl; // clientAddr for control connection (commands, replies)
struct sockaddr_in clientData; // clientAddr for data connection (file transfer)

int sockfdFTPServerControl; // FTP server's socket for control connection (commands, replies)
int sockfdFTPServerData; // FTP server's socket for data connection (file transfer)

struct sockaddr_in FTPServerControl; // FTP serverAddr for control connection (commands, replies)
struct sockaddr_in FTPServerData; // FTP serverAddr for data connection (file transfer)

int pozUser; // used for login

char cd[PATH_MAX] = "FTP"; // current working directory

char oldPath[PATH_MAX]; // used for renaming files


/* structure to parse command from client */
struct Command {
    char command[64];
    char parameter[64];
    char username[64];
    char password[64];
    char location[PATH_MAX];
};
typedef struct Command Command;


/* parse command from client */
Command* parseCommand(char* command) {
    Command* _command = (Command*)malloc(sizeof(Command));
    memset(_command, 0, sizeof(Command));

    char* p = command;

    sscanf(p, "%s %s %s %s %s", _command->command, _command->parameter, _command->username, _command->password, _command->location);

    return _command;
}


/* connect to server with username */
int USER(char* username) {
    int fd = open("users", O_RDONLY);
    if(-1 == fd) {
        perror("Error on reading from file");
        return -1;
    }
    lseek(fd, 0, SEEK_SET);

    /* search username in file and return it's position */
    pozUser = 0;
    int i = 0;
    char checkUsername[64];
    char ch;
    while(1) {
        int rcode = read(fd, &ch, 1);
        if(rcode == -1) {
            perror("Error on reading username from file");
            return -1;
        }

        if(rcode == 0) {
            checkUsername[i] = '\0';

            if(strcmp(checkUsername, username) == 0) {
                return pozUser;
            }
            break;
        }

        if(ch == '\n') {
            checkUsername[i] = '\0';

            if(strcmp(checkUsername, username) == 0) {
                return pozUser;
            }

            i = 0;
            pozUser++;

        }
        else {
            checkUsername[i++] = ch;
        }
    } 
    close(fd);

    return -1;
}


/* connect to server with password */
int PASS(char* password) {
    int fd = open("passwords", O_RDONLY);
    if(-1 == fd) {
        perror("Error on reading from file");
        return -1;
    }
    lseek(fd, 0, SEEK_SET);

    /* search password in file based on username position */
    int poz = 0, i = 0;
    char checkPassword[64];
    char ch;
    while(1) {
        int rcode = read(fd, &ch, 1);
        if(rcode == -1) {
            perror("Error on reading password from file");
            return -1;
        }

        if(rcode == 0) {
            checkPassword[i] = '\0';
            if(poz == pozUser && strcmp(checkPassword, password) == 0) {
                return poz;
            }
            break;
        }

        if(ch == '\n') {
            checkPassword[i] = '\0';
            if(poz == pozUser && strcmp(checkPassword, password) == 0) {
                return poz;
            }
            i = 0;
            poz++;

        }
        else {
            checkPassword[i++] = ch;
        }
    } 
    close(fd);

    return -1;
}


/* enter Passive Mode */
int PASV() {
    /* create socket for data connection */
    if((sockfdData = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error on socketData");
        return -1;
    }

    /* fill server's "data" struct */
    serverData.sin_family = AF_INET;
    serverData.sin_addr.s_addr = htonl(INADDR_ANY);
    serverData.sin_port = 0; /* get an unused port */

    /* bind server's structure to socket */
    if(bind(sockfdData, (struct sockaddr*)&serverData, sizeof(serverData)) == -1) {
        perror("Error on bindData");
        return -1;
    }

    /* listen for clients */
    if(listen(sockfdData, 5) == -1) {
        perror("Error on listenData");
        return -1;
    }

    /* send port to client */
	struct sockaddr_in getAddr;
    socklen_t len = sizeof(getAddr);
	getsockname(sockfdData, (struct sockaddr*)&getAddr, &len); /* get info from sockaddr strucre */
	portData = ntohs(getAddr.sin_port);

    if(-1 == write(clifdControl, &portData, sizeof(int))) {
        perror("Error on write");
        return -1;
    }

    /* wait and accept a new client */
    socklen_t lengthData = sizeof(clientData);
    if((clifdData = accept(sockfdData, (struct sockaddr*)&clientData, &lengthData)) == -1) {
        perror("Error on acceptControl");
    }

    return 1;
}


/* check if path is folder */
int is_folder(const char* path) {
    struct stat statbuf;

    if (stat(path, &statbuf) != 0) 
        return -1;

    return S_ISDIR(statbuf.st_mode);
}


/* change working directory */
int CWD(char* cdir) {
    if(strcmp(cdir, "..") == 0) { /* cwd to parent */
        int n = strlen(cd);
        for(int i = n - 1; i >= 0; i--) {
            if(cd[i] == '/') {
                cd[i] = '\0';
                break;
            }
        }
    }
    else if(strcmp(cdir, "/") == 0) { /* cwd to root */
        strcpy(cd, "FTP");
    }
    else {
        /* build new path */
        char cdAux[PATH_MAX];
        sprintf(cdAux, "%s/%s", cd, cdir);

        /* check if new path is a folder */
        if(is_folder(cdAux) != 1) { 
            return -1;
        }

        else {
            strcpy(cd, cdAux);
        }
    }

    return 1;
}


/* get size of a file */
int getSize(const char *fileName) {
    struct stat st; 
     
    if(stat(fileName, &st) == 0) {
        return (st.st_size);
    }

    return -1;
}


/* get a file from server to client */
int RETR(char* path, int* content, int contentSize) {
    int fd = open(path, O_RDONLY);
    if(-1 == fd) {
        perror("Error on openning file");
        return -1;
    }

    memset(content, 0, contentSize);

    /* fill lock struct */
    struct flock lock;
    lock.l_type = F_RDLCK;
    lock.l_whence = SEEK_CUR;
    lock.l_start = 0;
    lock.l_len = contentSize;
    /* lock file */
    if(-1 == fcntl(fd, F_SETLKW, &lock)) { 
        perror("Error on putting lock");
        return -1;
    }
    
    /* get file's content */
    if(-1 == read(fd, content, contentSize)) {
        perror("Error on reading from file");
        return -1;
    }

    /* unlock file */
    lock.l_type = F_UNLCK;
    if(-1 == fcntl(fd, F_SETLKW, &lock)) { 
        perror("Error on removing lock");
        return -1;
    }
    close(fd);

    return 1;
}


/* get file from client to server */
int STOR(char* fileName) {
    char path[PATH_MAX];
    sprintf(path, "%s/%s", cd, fileName);

    /* get file's content size */
    int contentSize;
    if(-1 == read(clifdData, &contentSize, sizeof(int))) {
        perror("Error on read");
        return -1;
    }

    /* get file's content */
    int* content = (int*)malloc(contentSize * sizeof(int));
    if(-1 == read(clifdData, content, contentSize)) {
        perror("Error on reading from sockfdData");
        return -1;
    }

    int fd = open(path, O_WRONLY | O_CREAT, 0777);
    if(-1 == write(fd, content, contentSize)) {
        perror("Error on writing to file");
        return -1;
    }
    free(content);
    close(fd);

    return 1;
}


/* rename file specifing old name */
int RNFR(char* path) {
    if(is_folder(path) == -1) {
        return -1;
    }
    else {
        strcpy(oldPath, path);
        return 1;
    }
}


/* rename file specifing new name */
int RNTO(char* path) {
    if(rename(oldPath, path) == 0) {
        return 1;
    }

    return -1;
}


/* remove file */
int DELE(char* fileName) {
    if (remove(fileName) == 0) {
      return 1;
    }

    return -1;
}


/* remove directory */
void RMD(char* path, int* status) {
    DIR* dir;
    struct dirent *de; 
    char pname[PATH_MAX]; 

    char message[256];

    /* check if is file or directory */
    if(is_folder(path) != 1) { 
        if(remove(path) != 0) { /* remove file */
            sprintf(message, "Error on removing file %s", path);
            write(clifdControl, message, 256);
            *status = -1;
        }
        return;
    }
    else {
        /* open current directory */
        if(NULL == (dir = opendir(path))) {
            sprintf(message, "Error on openning directory %s", path);
            write(clifdControl, message, 256);
            *status = -1;
            return;
        }

        /* get each entry of current directory */
        while(NULL != (de = readdir(dir))) { 
            if(strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {      
                /* build new path */         
                sprintf(pname, "%s/%s", path, de->d_name); 
                /* recursive call for new entry */
                RMD(pname, status);
            }
        }

        /* remove directory when is empty */
        if(rmdir(path) != 0) {
            sprintf(message, "Error on removing directory %s", path);
            write(clifdControl, message, 256);
            *status = -1;
            return;
        }

        closedir(dir);
    }
}


/* create new directory */
int MKD(char* dirName) {
    char path[PATH_MAX];
    sprintf(path, "%s/%s", cd, dirName);

    if(mkdir(path, 0777) == -1 && errno != EEXIST) {
		return -1;
	}

    return 1;
}


/* get list of files from directory */
int LIST(char* path, char* message) {
    DIR* dir;
    struct dirent *de; 
    
    /* check if is folder */
    if(is_folder(path) != 1) { 
        return -1;
    }
    else {
        /* open directory */
        if(NULL == (dir = opendir(path))) {
            perror("Error on opening directory");
            return -1;
        }

        char* pathAux = (char*)malloc(PATH_MAX);
        char* size = (char*)malloc(16);  

        /* get each entry from directory */
        while(NULL != (de = readdir(dir))) { 
            if(strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) { 
                sprintf(pathAux, "%s/%s", path, de->d_name);

                /* 
                    <size> <filename>
                    <DIR> <dirname> 
                */
                if(is_folder(pathAux) == 1) { 
                    strcat(message, "<DIR>");
                }
                else {  
                    sprintf(size, "%d", getSize(pathAux));

                    strcat(message, size);
                }
                strcat(message, "  "); strcat(message, de->d_name); strcat(message, "\n");
            }
        }  

        closedir(dir);

        free(pathAux);
        free(size);
    }
    return 1;
} 


/* print a list of all commands */
void HELP() {
    char* message = (char*)malloc(BUFF_SIZE); 
    strcpy(message, "");

    strcat(message, "USER <username> - First command to log in\n");
    strcat(message, "PASS <password> - Second command to log in\n");
    strcat(message, "PASV - Requests the server to listen on a data port\n");
    strcat(message, "CWD <pathname> - Change working directory\n");
    strcat(message, "PWD - Print working directory\n");
    strcat(message, "RETR <pathname> - Transfer a copy of the specified file from server to client\n");
    strcat(message, "STOR <pathname> - Transfer a copy of the specified file from client to server\n");
    strcat(message, "RNFR <pathname> - Specifies the old pathname of the file to be renamed. Must be immediately followed by RNTO command specifying the new file pathname.\n");
    strcat(message, "RNTO <pathname> - Specifies the new pathname of the file to be renamed\n");
    strcat(message, "DELE <pathname> - Delete the specified file from server\n");
    strcat(message, "RMD <pathname> - Remove the specified directory from server\n");
    strcat(message, "MKD <pathname> - Create the specified directory on server\n");
    strcat(message, "LIST [<pathname>] - Send a list of files in specified directory/current directory from server to client\n");
    strcat(message, "HELP - Show a list of commands\n");
    strcat(message, "NOOP - Server send an OK reply.\n");
    strcat(message, "QUIT - End current connection with server\n");

    if(-1 == write(clifdControl, message, BUFF_SIZE)) {
        perror("Error on read");
    }

    free(message);
}


/* send command to FTP server */
int FTPWriteCmd(char* command) {
	if(-1 == write(sockfdFTPServerControl, command, strlen(command))) {
		perror("Error on sending command to FTP Server");
		return -1;
	}
	return 1;
}


/* read reaply from FTP server */
int FTPReadReply(char* reply) {
    int poz = 0;
    while(1) {
		if(-1 == read(sockfdFTPServerControl, &reply[poz], sizeof(char))) {
			perror("Error on reading reply from FTP Server");
			return -1;
		}

		if(reply[poz] == '\n') {
			break; 
        }

        poz++;
	}
	reply[poz + 1] = '\0';
    printf("%s", reply);

    /* get reply code, first 3 numbers from reply message */
    int replyCode = 0;
    int p = 100;
    poz = 0;
	while(1) {
        if(reply[poz] == ' ') {
            break;
        }
        else {
            replyCode = replyCode + p * (reply[poz] - '0');
            p = p / 10;
            poz++;
        }
    }

    return replyCode;
}


/* connect to FTP server with username */
int FTPUser(char* username) { 
	char replyBuffer[BUFF_SIZE], cmdBuffer[BUFF_SIZE];

	sprintf(cmdBuffer, "USER %s\r\n", username);
	if(-1 == FTPWriteCmd(cmdBuffer)) {
		close(sockfdFTPServerControl);
		return -1;
	}

	return FTPReadReply(replyBuffer);
}


/* connect to FTP server with password */
int FTPPass(char* password) {
	char replyBuffer[BUFF_SIZE], cmdBuffer[BUFF_SIZE];

	sprintf(cmdBuffer, "PASS %s\r\n", password);
    if(-1 == FTPWriteCmd(cmdBuffer)) {
        close(sockfdFTPServerControl);
		return -1;
	}

	return FTPReadReply(replyBuffer);
}


/* set the transfer mode to binary(image) on FTP server */
int FTPTypeI() {
	char replyBuffer[BUFF_SIZE];

	if(-1 == FTPWriteCmd("TYPE I\r\n")) { 
		close(sockfdFTPServerControl);
		return -1;
	}
	
	return FTPReadReply(replyBuffer);
}


/* connect & login to FTP server */
int FTPServerLogin(char* addr, int port, char* username, char* password) {
	char replyBuffer[BUFF_SIZE];
	int replyCode;

    /* create socket for control connection */
	if((sockfdFTPServerControl = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error on socketControl");
        return -1;
    }

    /* fill server's "control" struct */
    FTPServerControl.sin_family = AF_INET;
    FTPServerControl.sin_addr.s_addr = inet_addr(addr);
    FTPServerControl.sin_port = htons(port);
    
    /* connect to FTP "control" server */
    if(connect(sockfdFTPServerControl, (struct sockaddr*)&FTPServerControl, sizeof(struct sockaddr)) == -1) {
        perror("Error on connectControl");
        return -1;
    }

	printf("Connected to FTP Server successfully!\n");

	replyCode = FTPReadReply(replyBuffer);
	if(replyCode != 220) { /* check if server sent welcome message */
		close(sockfdFTPServerControl);
		return -1;
	}

	replyCode = FTPUser(username);
	if(replyCode != 331 && replyCode != 230) { /* check if username is found */
		close(sockfdFTPServerControl);
		return -1;
	}

	if(replyCode == 331) { /* check if need password */
		replyCode = FTPPass(password);
		if(replyCode != 230) { /* check if loged in */
			close(sockfdFTPServerControl);
			return -1;
		}
	}

	replyCode = FTPTypeI();
	if(replyCode != 200) { /* check if command ok */
		close(sockfdFTPServerControl);
		return -1;
	}

	return 1;
}


/* enter Passive Mode on FTP server */
int FTPPasv() {
    char* addr = (char*)malloc(32); 
    int port;

    char replyBuffer[BUFF_SIZE];

	if(-1 == FTPWriteCmd("PASV\r\n")) {
		return -1;
	}
	int replyCode = FTPReadReply(replyBuffer);
	if(replyCode != 227) { /* check if entered passive mode (h1,h2,h3,h4,p1,p2) */
		return replyCode;
	}

    int addr1, addr2, addr3, addr4, port1, port2;
	sscanf(replyBuffer, "%*[^(](%d,%d,%d,%d,%d,%d)", &addr1, &addr2, &addr3, &addr4, &port1, &port2);
	sprintf(addr, "%d.%d.%d.%d", addr1, addr2, addr3, addr4);
	port = port1 * 256 + port2; /* get port from first and second 8-bit numbers */

    /* create socket for data connection */
	if((sockfdFTPServerData = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error on socketControl");
        return -1;
    }

    /* fill server's "data" struct */
    FTPServerData.sin_family = AF_INET;
    FTPServerData.sin_addr.s_addr = inet_addr(addr);
    FTPServerData.sin_port = htons(port);
    
    /* connect to FTP "data" server */
    if(connect(sockfdFTPServerData, (struct sockaddr*)&FTPServerData, sizeof(struct sockaddr)) == -1) {
        perror("Error on connectControl");
        return -1;
    }

	return replyCode;
}


/* get a list of files from cwd from FTP server */
int FTPList(char* dir) {
	char replyBuffer[BUFF_SIZE], cmdBuffer[BUFF_SIZE];

	if(FTPPasv() != 227) { /* check if entered passive mode */
		return -1;
	}

    sprintf(cmdBuffer, "LIST %s\r\n", dir);
	if(-1 == FTPWriteCmd(cmdBuffer)) {
		return 1;
	}

	int replyCode = FTPReadReply(replyBuffer);
	if(replyCode != 125 && replyCode != 150) { /* check if data connection is opened */
		return -1;
	}

    /* read content of list */
    if(-1 == read(sockfdFTPServerData, replyBuffer, BUFF_SIZE)) {
		perror("Error on read");
		return -1;
	}

	close(sockfdFTPServerData);

	char aux[BUFF_SIZE];
	int poz = 0;
	listSize = 0;
	int n = strlen(replyBuffer);

    /* 
        <size> <filename>
        <DIR> <dirname> 
    */
	for(int i = 0; i < n; i++) {
		if(replyBuffer[i] != '\r') { 
			aux[poz++] = replyBuffer[i];
		}
		else {
			aux[poz] = '\0';
			if(strcmp(aux, "ing.") != 0) {
				strcpy(list[listSize][0], aux);
				sscanf(list[listSize][0], "%*[^ ] %*[^ ] %s %s", list[listSize][0], list[listSize][1]); /* fill the structure */
				listSize++;
			}
			i++;
			poz = 0;
		}
	}

	if(FTPReadReply(replyBuffer) != 226) { /* check if transfer was completed */
		return -1;
	}

    return 1;
}


/* change working directory on FTP server */
int FTPCwd(char* dir) {
	char replyBuffer[BUFF_SIZE], cmdBuffer[BUFF_SIZE];

    sprintf(cmdBuffer, "CWD %s\r\n", dir);
	if(-1 == FTPWriteCmd(cmdBuffer)) {
		return -1;
	}

    return FTPReadReply(replyBuffer);
}


/* get a file from FTP server */
int FTPRetr(char* fileName, int size, char* newFileName, int refresh) {
    char cmdBuffer[BUFF_SIZE], replyBuffer[BUFF_SIZE];

	if(FTPPasv() != 227) { /* check if entered passive mode */
		return -1;
	}

    sprintf(cmdBuffer, "RETR %s\r\n", fileName);
	if(-1 == FTPWriteCmd(cmdBuffer)) {
		return -1;
	}
	int replyCode = FTPReadReply(replyBuffer);
	if(replyCode != 125 && replyCode != 150) { /* check if data connection is opened */
		return -1;
	}

    /* get file's content */
	int* content = (int*)malloc(size * sizeof(int));
	int i = 0, rsize, failed = 0;

	printf("Starting download...\n");

    while(i < size) {
		rsize = read(sockfdFTPServerData, ((char*)content + i), size);
		if(rsize == -1) {
			printf("Donwload was intrerupted!\n");
			failed = 1;
			break;
		}
        i = i + rsize;
	}

	if(failed == 0) {
		printf("Download completed!\n");
	}

	close(sockfdFTPServerData);

	int fd = open(newFileName, O_RDWR | O_CREAT, 0777);
	if(fd == -1) {
		perror("Error on opening file");
		return -1;
	}

    /* check if is a copy/refresh command */
    if(refresh == 1) {
        int* checkContent = (int*)malloc(size * sizeof(int));

        /* fill lock stucture */
        struct flock lock;
        lock.l_type = F_RDLCK;
        lock.l_whence = SEEK_CUR;
        lock.l_start = 0;
        lock.l_len = size;

        /* lock file */
        if(-1 == fcntl(fd, F_SETLKW, &lock)) { 
            perror("Error on putting lock");
            return -1;
        }

        /* get file's content */
        if(-1 == read(fd, (char*)checkContent, size)) {
            perror("Error on reading from file");
            return -1;
        }

        /* unlock file */
        lock.l_type   = F_UNLCK;
        if(-1 == fcntl(fd, F_SETLKW, &lock)) { 
            perror("Error on removing lock");
            return -1;
        }

        /* check if file was modified */
        if(strcmp((char*)content, (char*)checkContent) != 0) {
            lseek(fd, 0, SEEK_SET);
            if(-1 == write(fd, content, size)) {
                perror("Error on writing to file");
                return -1;
            }
        }
    }
    else {
        if(-1 == write(fd, content, size)) {
            perror("Error on writing to file");
            return -1;
        }
    }

	close(fd);
	free(content);

	if(FTPReadReply(replyBuffer) != 226) { /* check if transfer was completed */
		return -1;
	}

    return 0;
}


/* copy/refresh FTP server */
void FTPDownloadFolder(char* currentDir, char* localDir, int refresh, int* status) {
    if(FTPCwd(currentDir) != 250) { /* check if changed working directory */
        *status = -1;
		return;
	}
	
    /* create local path */
	char lDir[PATH_MAX];
	sprintf(lDir, "%s%s", localDir, currentDir);

    /* create local directory */
	if(mkdir(lDir, 0777) == -1 && errno != EEXIST) { 
        *status = -1;
		return;
	}

    /* get list of files from cwd */
	if(FTPList(currentDir) == -1) {
        *status = -1;
		return;
	}

	char listAux[256][2][64];
	int listSizeAux = listSize;

	for(int i = 0 ; i < listSizeAux; i++) {
		strcpy(listAux[i][0], list[i][0]);
		strcpy(listAux[i][1], list[i][1]);
	}

	for(int i = 0 ; i < listSizeAux; i++) {
		if(strcmp(listAux[i][0], "<DIR>") == 0) {
            /* create new working path */
			char cd[PATH_MAX];
			if(strcmp(currentDir, "/") != 0) {
				sprintf(cd, "%s/%s", currentDir, listAux[i][1]);
			}
			else {
			 	sprintf(cd, "/%s", listAux[i][1]);
			}

            /* recursive call for new path */
			FTPDownloadFolder(cd, localDir, refresh, status);
		}

		else {
            /* get new local path */
			char fileLoc[PATH_MAX];
			sprintf(fileLoc, "%s/%s", lDir, listAux[i][1]);

            /* get file's content localy */
			if(-1 == FTPRetr(listAux[i][1], atoi(listAux[i][0]), fileLoc, refresh)) {
                *status = -1;
                return;
            }
		}
	}
}


/* close FTP connection */
void FTPQuit() {
	FTPWriteCmd("QUIT\r\n");

    char replyBuffer[BUFF_SIZE];
    FTPReadReply(replyBuffer);

    close(sockfdFTPServerData);
    close(sockfdFTPServerControl);
}


int listenServer() {
    /* create socket */
    if((sockfdControl = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Error on socketControl");
        return -1;
    }

    /* fill server's struct */
    serverControl.sin_family = AF_INET;
    serverControl.sin_addr.s_addr = htonl(INADDR_ANY);
    serverControl.sin_port = htons(PORT_CONTROL);


    /* bind the structure to socket */
    if(bind(sockfdControl, (struct sockaddr*) &serverControl, sizeof(serverControl)) == -1) {
        perror("Error on bindControl");
        return -1;
    }

    /* listen for new clients */
    if(listen(sockfdControl, 5) == -1) {
        perror("Error on listenContol");
        return -1;
    }

    return 1;
} 


void acceptClient() {
    /* wait and accept a client */
    socklen_t lengthControl = sizeof(clientControl);
    if((clifdControl = accept(sockfdControl, (struct sockaddr *) &clientControl, &lengthControl)) == -1) {
        perror("Error on acceptControl");
    }
}


int main(int argc, char *argv[]) {
    if(-1 == listenServer()) {
        return -1;
    }

    while(1) {
        printf("Waiting for clients...\n"); fflush (stdout);

        acceptClient();

        /* create a new process for each new client */
        int pid;
        if(-1 == (pid = fork())) {
          perror("Error on fork");
          close(clifdControl);
        }

        /* child process */
        if(pid == 0) {
            while(1){
                printf ("Waiting to read...\n");

                char command[64]; memset(command, 0, 64);
                char message[256]; memset(message, 0, 256);

                /* read command from client */
                if(read(clifdControl, command, 64) <= 0) {
                    perror("Error on read");
                    close(clifdControl);
                    close(clifdData);
                    break;
                }
                printf("%s\n", command);

                Command* _command = parseCommand(command);

                if(strcmp(_command->command, "USER") == 0) {
                    pozUser = USER(_command->parameter); 
                    if(pozUser == -1) {
                        strcpy(message, "Username not found!");
                    }
                    else {
                        strcpy(message, "Username found!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                        perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "PASS") == 0) {
                    int poz = PASS(_command->parameter);
                    if(poz == -1) {
                        strcpy(message, "Password not found!");
                    }   
                    else {
                        strcpy(message, "Password found!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                        perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "PASV") == 0) {
                    int reply = PASV();
                    if(reply == -1) {
                        strcpy(message, "Failed to estabilish data connection!");
                    }
                    else {
                        strcpy(message, "Data connection estabilished successfully!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                        perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "QUIT") == 0) {
                    printf("Client has quit!\n");

                    close(clifdControl);
                    close(clifdData);

                    exit(0);
                }

                /* copy FTP server at specific location */
                else if(strcmp(_command->command, "COPY") == 0) {
                    int serverPort = 21; /* FTP Port */
                    char cdAux[PATH_MAX+2];

                    if(strcmp(_command->location, "") == 0) {
                        strcpy(cdAux, cd);
                    }
                    else {
                        sprintf(cdAux, "%s/%s", cd, _command->location);
                    }

                    if(-1 != FTPServerLogin(_command->parameter, serverPort, _command->username, _command->password)) {
                        int status = 0;
                        listSize = 0;

                        FTPDownloadFolder("/", cdAux, 0, &status);

                        if(status == -1) {
                            strcpy(message, "Failed to copy site!");
                        }
                        else {
                            strcpy(message, "Site copied successfully!");
                        }

                        FTPQuit();
                    }
                    else {
                        strcpy(message, "Failed to copy site!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                        perror("Error on write");
                    }
                }

                /* copy FTP server at specific location */
                else if(strcmp(_command->command, "REFRESH") == 0) {
                    int serverPort = 21; /* FTP Port */
                    char cdAux[PATH_MAX+2];

                    if(strcmp(_command->location, "") == 0) {
                        strcpy(cdAux, cd);
                    }
                    else {
                        sprintf(cdAux, "%s/%s", cd, _command->location);
                    }

                    if(-1 != FTPServerLogin(_command->parameter, serverPort, _command->username, _command->password)) {
                        int status = 0;
                        listSize = 0;

                        FTPDownloadFolder("/", cdAux, 1, &status);

                        if(status == -1) {
                            strcpy(message, "Failed to refresh site!");
                        }
                        else {
                            strcpy(message, "Site refreshed successfully!");
                        }

                        FTPQuit();
                    }
                    else {
                        strcpy(message, "Failed to refresh site!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                        perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "CWD") == 0) {
                    int reply = CWD(_command->parameter); 
                    if(reply == -1) {
                        strcpy(message, "Failed to change working directory!");
                    }
                    else {
                        strcpy(message, "Working directory changed successfully!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                        perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "PWD") == 0) {
                    if(-1 == write(clifdControl, cd, strlen(cd) + 1)) {
                        perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "RETR") == 0) {
                    char path[PATH_MAX]; strcpy(path, "");
                    sprintf(path, "%s/%s", cd, _command->parameter);

                    int contentSize = getSize(path);
                    int* content = (int*)malloc(contentSize * sizeof(int));

                    int reply = RETR(path, content, contentSize);  
                    if(reply == -1) {
                        strcpy(message, "Failed to get file from server!");
                        if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                        }
                    }
                    else {
                        strcpy(message, "File got from server successfully!");
                        if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                        }

                        if(-1 == write(clifdData, &contentSize, sizeof(int))) {
                            perror("Error on writing to clifdData");
                            return -1;
                        }

                        if(-1 == write(clifdData, content, contentSize)) {
                            perror("Error on writing to clifdData");
                            return -1;
                        }

                        strcpy(message, "File sent successfully!");
                        if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                        }
                    }
                    
                    free(content);
                }

                else if(strcmp(_command->command, "STOR") == 0) {
                    if(-1 == read(clifdControl, message, 256)) {
                        perror("Error on read");
                        close(clifdControl); close(clifdData);
                        exit(-1);
                    }
                    printf("%s\n",message);

                    if(strcmp(message, "File's content got successfully!") == 0) {
                        int reply = STOR(_command->parameter);  
                        if(reply == -1) {
                            strcpy(message, "Failed to store file to server!");
                        }
                        else {
                            strcpy(message, "File stored to server successfully!");
                        }

                        if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                        }
                    }
                }

                else if(strcmp(_command->command, "RNFR") == 0) {
                    char path[PATH_MAX+2];
                    sprintf(path, "%s/%s", cd, _command->parameter);

                    int reply = RNFR(path);
                    if(reply == -1) {
                        strcpy(message, "File to rename not found!");
                    }
                    else {
                        strcpy(message, "File to rename found!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "RNTO") == 0) {
                    char path[PATH_MAX+2];
                    sprintf(path, "%s/%s", cd, _command->parameter);

                    int reply = RNTO(path);
                    if(reply == -1) {
                        strcpy(message, "Failed to rename file!");
                    }
                    else {
                        strcpy(message, "File renamed successfully!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "DELE") == 0) {
                    char path[PATH_MAX+2];
                    sprintf(path, "%s/%s", cd, _command->parameter);

                    int reply = DELE(path); 
                    if(reply == -1) {
                        strcpy(message, "Failed to remove file from server!");
                    }
                    else {
                        strcpy(message, "File removed from server successfully!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "RMD") == 0) { 
                    char path[PATH_MAX+2];
                    sprintf(path, "%s/%s", cd, _command->parameter);

                    if(is_folder(path) != 1) {
                        strcpy(message, "Wrong input!");
                    }
                    else {
                        int status = 0;
                        RMD(path, &status);
                        if(status == 0) {
                            strcpy(message, "Directory successfully removed!");
                        }
                        else {
                            strcpy(message, "Failed to remove directory");
                        }
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "MKD") == 0) {
                    int reply = MKD(_command->parameter);
                    if(reply == -1) {
                        strcpy(message, "Failed to create directory to server!");
                    }
                    else {
                        strcpy(message, "Directory created to server successfully!");
                    }

                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                        perror("Error on write");
                    }
                }

                else if(strcmp(_command->command, "LIST") == 0) {
                    char path[PATH_MAX+2];
                    strcpy(path, "");

                    if(strcmp(_command->parameter, "") == 0) {
                        sprintf(path, "%s", cd);
                    }
                    else {
                        sprintf(path, "%s/%s", cd, _command->parameter);
                    }

                    char messageList[BUFF_SIZE]; memset(messageList, 0, BUFF_SIZE);
                    
                    int reply = LIST(path, messageList);
                    if(reply == -1) {
                        strcpy(message, "Failed to list content of directory!");
                        if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                        }
                    }
                    else {
                        strcpy(message, "Content of directory listed successfully!");
                        if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                            perror("Error on write");
                        }
                        if(-1 == write(clifdData, messageList, strlen(messageList) + 1)) {
                            perror("Error on write");
                        }
                    }
                }

                else if(strcmp(_command->command, "HELP") == 0) {
                    HELP();
                }

                else if(strcmp(_command->command, "NOOP") == 0) {
                    strcpy(message, "Server NOOPed back!");
                    if(-1 == write(clifdControl, message, strlen(message) + 1)) {
                        perror("Error on write");
                    }
                }
           }
        }
        /* parent process */
        else {
            close(clifdControl);
            close(clifdData);
            waitpid(-1, NULL, WNOHANG);
            continue;
        }   
    }

    return 0;
}
