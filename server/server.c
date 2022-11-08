#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <strings.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>

//#include "requestHandler.h"
//#include "httpHeaders.h"

#define MAXLINE 4096 /*max text line length*/
#define LISTENQ 1024 /*maximum number of client connections*/
int forceStopChildren = 0;


int beginRequest(int connfd);
int checkMessage(int connfd, char* method, char* URI, char* version, int errNO, int keepAlive);
int attemptRequest(int connfd, char* URI,char* version, int keepAlive);


void checkStop(int signum){

    //printf("testing\n");
        int status = 0;
        pid_t wpid;
        while(wpid = wait(&status) > 0);
        exit(0);

}
void checkStopChild(int signum){
    forceStopChildren = 1;
}
int main(int argc, char **argv){
    if(argc != 2){
        fprintf(stderr,"usage: %s <port>\n", argv[0]);
        exit(0);
    }


    int portno;
    portno = atoi(argv[1]);
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int sockfd, optval, n, connfd;
    socklen_t clientLength;
    pid_t childPID;
    signal(SIGINT, checkStop);

    


    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(portno);


    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));



    if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) <0) {
        perror("Problem in creating the socket\n");
        exit(-1);
    }



    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) {
        perror("ERROR on binding\n");
        exit(-2);
    }



    if(listen(sockfd , LISTENQ) < 0){
        perror("Maximum sessions reached!\n");  
    }
    printf("Waiting for connections\n");


    while(1){
        clientLength = sizeof(clientaddr);
        connfd = accept(sockfd, (struct sockaddr *) &clientaddr, &clientLength);
        
        printf("Connection accepted.\n");
        if((childPID = fork()) == 0){
           signal(SIGINT, checkStopChild);
           if(close(sockfd)<0){
            perror("ERROR CLOSING LISTENING SOCKET\n");
           }
           printf("Socket opened: %d\n", connfd);
           beginRequest(connfd);
           printf("Closing socket: %d\n", connfd);
           if(close(connfd)<0){
            perror("ERROR CLOSING CONNECTION SOCKET\n");
           }           
         exit(0);
        }
        if(close(connfd) < 0){
            perror("ERROR CLOSING CONNECTION SOCKET (PARENT)\n");
        }
        waitpid(-1, NULL, WNOHANG);

    }
    printf("parent exiting\n");
    
    return -1;
}
char * returnMethod(char* request, char * desiredHeader){
            char host_buf[MAXLINE];
        strcpy(host_buf, request);
        char *host = strstr(host_buf, desiredHeader);
        strtok(host, " ");
        char * hostname = strtok(NULL, "\r\n");
        return hostname;
}
int beginRequest(int connfd){
    int readSize;
    char message[MAXLINE];
    char lowerCaseMessage[MAXLINE];
    char method[MAXLINE];
    char URI[MAXLINE];
    char version[MAXLINE];
    char test[MAXLINE];
    struct timeval timeAlive;
    timeAlive.tv_sec = 10;
    timeAlive.tv_usec = 0;
    fd_set testingTimeout;
    struct timeval defaultTimeout;
    defaultTimeout.tv_sec = 120;
    defaultTimeout.tv_usec = 0;
    int stopTheLoop = 0;
    setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&defaultTimeout, sizeof(defaultTimeout));
    //int retval = select(connfd+1, &testingTimeout, NULL, NULL, &timeAlive);
    if(forceStopChildren == 1){
        defaultTimeout.tv_sec = 2;
        defaultTimeout.tv_usec = 0;
        setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&defaultTimeout, sizeof(defaultTimeout));
        stopTheLoop = 1;
    }
    
    int CLRFCount;
    char * finalMessage;
    finalMessage = malloc(MAXLINE);
    while(1){
        setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeAlive, sizeof(timeAlive));
        int testRead;
        while((readSize = recv(connfd, finalMessage, MAXLINE, 0)) > 0){
            CLRFCount = 0;
            strncpy(message, finalMessage, readSize);
            
            while(strstr(message, "\r\n\r\n") != NULL){
                CLRFCount += 1;
                if(CLRFCount == 1){
                    break;
                }  
            }
            if(CLRFCount == 1){
                break;
            }
            
        }
        if(readSize <= 0){
            break;
        }
        memset(&method, 0, MAXLINE);
        memset(&URI, 0, MAXLINE);
        memset(&version, 0, MAXLINE);
        memset(&test, 0, MAXLINE);
        memset(&lowerCaseMessage, 0, MAXLINE);
        FD_ZERO(&testingTimeout);
        FD_SET(connfd, &testingTimeout);
        free(finalMessage);
        int errNo;
        int check400; 
        int keepAlive;
        check400 = sscanf(message, "%s %s %s %s", method, URI, version, test);
        for(int i=0; message[i]; i++){
            lowerCaseMessage[i] = tolower(message[i]);
        }
        printf("Received request %s %s %s\n", method, URI, version);
        //printf("still in loop\n");
        
        if(strstr(lowerCaseMessage, "connection: keep-alive") != NULL && forceStopChildren == 0){
            keepAlive = 1;
            setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeAlive, sizeof(timeAlive));
        }
        else if(strstr(lowerCaseMessage, "connection: close") != NULL || forceStopChildren == 1){
            keepAlive = 0;
            forceStopChildren = 0;
        }
        else{
            if(strcmp(version, "HTTP/1.1")==0){
                keepAlive = 1;
                setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeAlive, sizeof(timeAlive));
            }
            else{
                keepAlive = 0;
            } 
        }

        if(strstr(message, "HTTP/") == NULL){
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("No method.\n");
        }
        else if(strstr(message, "\r\n\r\n") == NULL){
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("Improper URI\n");
        }
        else if(strstr(URI, "../") != NULL){
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("Improper URI\n");
        }
        else if(URI[0] != '/' || version[4] != '/'){
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("Improper URI\n");
        }
        else if(strstr(message, "Host:") == NULL && strcmp(version, "HTTP/1.1")==0){
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("No host header in HTTP/1.1 request\n");
        }
        else if(check400 > 4 || check400 < 3){
            if(checkMessage(connfd, method, URI, version, 400, keepAlive) != 0){
                printf("400 error\n");
            }
        }
        else if(strchr(test, ':')==NULL && check400==4){
            if(checkMessage(connfd, method, URI, version, 400, keepAlive) != 0){
                printf("400 error\n");
            }
        }
        else if((errNo = checkMessage(connfd, method, URI, version, 0, keepAlive)) != 0){
            printf("405 or 505 encountered.\n");
        }

        
        else{
            errNo = attemptRequest(connfd, URI, version, keepAlive);
            if(errNo != 0){
                printf("error received while transmitting file\n");
            }
            if(keepAlive == 0){
                printf("Client connection closed.\n");
                return -1;
            }

        }
        memset(&message, 0, MAXLINE);
        if(keepAlive == 0){
                printf("Client connection closed.\n");
                return -1;
        }
    }
    if(forceStopChildren == 1 && stopTheLoop == 0){
        beginRequest(connfd);
    }
    else if(readSize <= 0){
        printf("Client timed out!\n");
        return -1;
    }
    return 0;
}








int checkMessage(int connfd, char* method, char* URI, char* version, int errNO, int keepAlive){
    int writeLength = 0;
    char getErrorHeader[MAXLINE];
    if(errNO == 400){
        char getError[MAXLINE] = "<!DOCTYPE html>\n<html>\n<title>400 Bad Request</title>\n<body>The request could not be parsed or is malformed</body>\n</html>";
        if(keepAlive == 1){
            snprintf(getErrorHeader, MAXLINE, "HTTP/1.0 400 Bad Request\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n", strlen(getError));
        }else{
            snprintf(getErrorHeader, MAXLINE, "HTTP/1.0 400 Bad Request\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", strlen(getError));
        }
        
        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 400;

    }
    else if(errNO == 403){
        char getError[MAXLINE] = "<!DOCTYPE html>\n<html>\n<title>403 Forbidden</title>\n<body>The requested file can not be accessed due to a file permission issue</body>\n</html>";
        if(keepAlive == 1){
            snprintf(getErrorHeader, MAXLINE, "%s 403 Forbidden\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }else{
            snprintf(getErrorHeader, MAXLINE, "%s 403 Forbidden\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }
        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 403;

    }else if(errNO == 404){
        char getError[MAXLINE] = "<!DOCTYPE html>\n<html>\n<title>404 Not Found</title>\n<body>The requested file can not be found in the document tree</body>\n</html>";
        if(keepAlive == 1){
            snprintf(getErrorHeader, MAXLINE, "%s 404 Not Found\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }else{
            snprintf(getErrorHeader, MAXLINE, "%s 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }
        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 404;
    }
    else if(strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0){
        char getError[MAXLINE] ="<!DOCTYPE html>\n<html>\n<title>505 HTTP Version Not Supported</title>\n<body>An HTTP version other than 1.0 or 1.1 was requested.</body>\n</html>";
        if(keepAlive == 1){
            snprintf(getErrorHeader, MAXLINE, "HTTP/1.0 505 HTTP Version Not Supported\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n", strlen(getError));
        }else{
            snprintf(getErrorHeader, MAXLINE, "HTTP/1.0 505 HTTP Version Not Supported\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", strlen(getError));
        }
        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 505;
    }
    else if(strcmp(method, "GET") != 0){
        char getError[MAXLINE] = "<!DOCTYPE html>\n<html>\n<title>405 Method Not Allowed</title>\n<body>A method other than GET was requested.</body>\n</html>";
        if(keepAlive == 1){
            snprintf(getErrorHeader, MAXLINE, "%s 405 Method Not Allowed\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n",version, strlen(getError));
        }else{
            snprintf(getErrorHeader, MAXLINE, "%s 405 Method Not Allowed\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n",version, strlen(getError));
        }
        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 405;
    }
    else{
        return 0;
    }

}

char *get_filename_ext(char *filename) {
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}
int getFileType(char* URI, char* fileType){
    char * filename = get_filename_ext(URI);
    if(strcmp(filename, "html") == 0 || strcmp(filename, "htm") == 0){
        strcat(fileType, "text/html");
        return 0;
    }
    else if(strcmp(filename, "txt") == 0){
        strcat(fileType, "text/plain");
        return 0;
    }
        else if(strcmp(filename, "png") == 0){
            strcat(fileType, "image/png");
        return 0;
        
    }
        else if(strcmp(filename, "gif") == 0){
            strcat(fileType, "image/gif");
            return 0;
        
    }
        else if(strcmp(filename, "jpg") == 0){
            strcat(fileType, "image/jpeg");
            return 0;
        
    }
            else if(strcmp(filename, "css") == 0){
                strcat(fileType, "text/css");
                return 0;
        
    }
                else if(strcmp(filename, "js") == 0){
                    strcat(fileType, "application/javascript");
                    return 0;
        
    }else{
        strcat(fileType, "application/octet-stream");
        return -1;
    }
}

int attemptRequest(int connfd, char* URI,char* version, int keepAlive){
    
    FILE* fp;   
    char currDirectory[MAXLINE] = "www";
    struct stat statbuf;
    strcat(currDirectory, URI);
    stat(currDirectory, &statbuf);
    if(URI[strlen(URI)-1] == '/' || strcmp(URI, "") == 0 /*|| S_ISDIR(statbuf.st_mode) != 0*/){
        if(S_ISDIR(statbuf.st_mode) !=0 && URI[strlen(URI)-1] != '/'){
            strcat(currDirectory, "/");
        }
        strcat(currDirectory, "index.html");
        if(access(currDirectory, F_OK)!=0){
            currDirectory[strlen(currDirectory)-1] = '\0';
            if(access(currDirectory, F_OK)!=0){
            checkMessage(connfd, "GET", URI, version, 404, keepAlive);
            return -1;  
            }
        }
        if((fp = fopen(currDirectory, "rb"))==NULL){
            if(access(currDirectory, F_OK)==0){
                checkMessage(connfd, "GET", URI, version, 403, keepAlive);
                return -1;
            }
            checkMessage(connfd, "GET", URI, version, 404, keepAlive);
            return -1;
        }
    }
    else{
        if(S_ISDIR(statbuf.st_mode) != 0){
                checkMessage(connfd, "GET", URI, version, 404, keepAlive);
                return -1;          
        }
        if((fp = fopen(currDirectory, "rb")) == NULL){
            if(access(currDirectory, F_OK)==0){
                checkMessage(connfd, "GET", URI, version, 403, keepAlive);
                return -1;
            }
            checkMessage(connfd, "GET", URI, version, 404, keepAlive);
            return -1;
        }
    }
    
    // SEND FILE NOW!
    char contentHeader[MAXLINE];
    int length = 0;
    int filesize = 0;
    
    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    rewind(fp);
    //CRAFTING HEADER.
    char fileType[MAXLINE];
    bzero(fileType, MAXLINE);
    getFileType(currDirectory, fileType);
    length += snprintf(contentHeader, MAXLINE, "%s 200 OK\r\n", version);
    length += snprintf(contentHeader+length, MAXLINE-length, "Content-Type: %s\r\n", fileType);
    
    if(keepAlive == 1 && forceStopChildren == 0){
        length += snprintf(contentHeader+length, MAXLINE-length, "Connection: keep-alive\r\n");
    }else if(keepAlive == 0 || forceStopChildren == 1){
        length += snprintf(contentHeader+length, MAXLINE-length, "Connection: close\r\n");
    }
    length += snprintf(contentHeader+length, MAXLINE-length, "Content-Length: %u\r\n\r\n",  filesize);
    send(connfd, contentHeader, strlen(contentHeader), 0);
    //CRAFTING CONTENTS WITH FILE
    char * transferBuf;
    transferBuf = malloc(MAXLINE);  // use a malloc instead of a static array.



    int test;
    while(/*!feof(fp)*/filesize > 0){ // https://stackoverflow.com/questions/33783470/sending-picture-via-tcp
        test = fread(transferBuf, 1, MAXLINE, fp);
        if(test > 0){
            if((test = send(connfd, transferBuf, test, 0) ) < 0){
                perror("test");
            }
            filesize -= test;
        }
        else{
            printf("%s\n", transferBuf);
            break;
        }
    }
    /*
    FILE* testFile;
    testFile = fopen("test.jpg", "wb");
    fwrite(transferBuf, 1, sizeof(transferBuf), testFile);
    fclose(testFile);
    */

    //printf("%ld\n");
    size_t bytesRead = 0;
    free(transferBuf);
    //bzero(URI, MAXLINE);
    bzero(currDirectory, MAXLINE);
    fclose(fp);
    return 0;
}