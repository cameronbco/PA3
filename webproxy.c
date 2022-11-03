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
#include <openssl/md5.h>
#include <pthread.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <assert.h>

//#include "requestHandler.h"
//#include "httpHeaders.h"

#define MAXLINE 4096 /*max text line length*/
#define LISTENQ 1024 /*maximum number of client connections*/
int forceStopChildren = 0;


int beginRequest(int connfd, int timeout);
int checkMessage(int connfd, char* method, char* URI, char* version, int errNO, int keepAlive);
int attemptRequest(int connfd, char* URI,char* version,  char * request, int keepalive, int requestSize, int URILength, int timeout);


typedef struct
{
  bool done;
  pthread_mutex_t mutex;
} shared_data;

static shared_data* data = NULL;

void initialise_shared()
{
    // place our shared data in shared memory
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_SHARED | MAP_ANONYMOUS;
    data = mmap(NULL, sizeof(shared_data), prot, flags, -1, 0);
    assert(data);

    data->done = false;

    // initialise mutex so it works properly in shared memory
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&data->mutex, &attr);
}








void checkStop(int signum){

    //printf("testing\n");
        int status = 0;
        pid_t wpid;
        while(wpid = wait(&status) > 0);
        munmap(data, sizeof(data));
        exit(0);

}
void checkStopChild(int signum){
    forceStopChildren = 1;
}
int main(int argc, char **argv){
    if(argc != 3){
        fprintf(stderr,"usage: %s <port> <timeout>\n", argv[0]);
        exit(0);
    }


    int portno;
    portno = atoi(argv[1]);
    int timeout = atoi(argv[2]);
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int sockfd, optval, n, connfd;
    socklen_t clientLength;
    pid_t childPID;
    signal(SIGINT, checkStop);

    mkdir("cache", 0700);


    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(portno);


    initialise_shared();

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
           beginRequest(connfd, timeout);
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
    munmap(data, sizeof(data)); 
    return -1;
}
char * returnHeader(char* request, char * desiredHeader){
            char host_buf[MAXLINE];
        strcpy(host_buf, request);
        char *host = strstr(host_buf, desiredHeader);
        strtok(host, " ");
        char * hostname = strtok(NULL, "\r\n");
        return hostname;
}
int beginRequest(int connfd, int timeout){
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
        else if(version[4] != '/'){
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
            errNo = attemptRequest(connfd,URI, version, message, keepAlive, sizeof(message), sizeof(URI), timeout);
            if(errNo != 0){
                printf("error received while transmitting file\n");
            }
            if(keepAlive == 0){
                printf("Client connection closed.\n");
                free(finalMessage);
                return -1;
            }

        }
        memset(&message, 0, MAXLINE);
        if(keepAlive == 0){
                printf("Client connection closed.\n");
                free(finalMessage);
                return -1;
        }
    }
    if(forceStopChildren == 1 && stopTheLoop == 0){
        beginRequest(connfd, timeout);
    }
    else if(readSize <= 0){
        printf("Client timed out!\n");
        free(finalMessage);
        return -1;
    }
    free(finalMessage);
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


unsigned long hash(char *str)
{
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

int attemptRequest(int connfd, char* URI,char* version,char* request,  int keepalive, int requestSize, int URILength, int timeout){
    //printf("%s\n", request);
    struct timeval timeAlive;
    timeAlive.tv_sec = 10;
    timeAlive.tv_usec = 0;
    char * destHostname = returnHeader(request, "Host: ");
    FILE * blacklist;
    blacklist = fopen("blacklist", "r");
    char line[253];
    size_t len = 0;
    size_t sizeRead;
    int errNo;
    int hashRes;
    hashRes = hash(URI);
    while(fgets(line, sizeof(line), blacklist)){
        len = strlen(line);
        sscanf(line, "%[^\r\n]", line);
        sscanf(line, "%[^\n]", line);
        if(strcmp(line, destHostname)==0){
            if((errNo = checkMessage(connfd, "GET", URI, version, 403, keepalive)) != 0){
                return -1;
            }
            
        }
    }
    char hostbuffer[256];
    char hostbuffer2[256];
    strcpy(hostbuffer, destHostname);
    char *IP;
    struct hostent *host_entry;
    int hostname;

    int inCache = 0;
    FILE * fp;
    char * currDirectory;

    /*
    if(URI[strlen(URI)-1] == '/' || strcmp(URI, "") == 0 ){
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
    */





  
    // To retrieve hostname
  
    // To retrieve host information
    char * prePort = strstr(destHostname, ":");
    int portno;
    if(prePort != NULL){
        portno = atoi(prePort+1);
        strncpy(hostbuffer2, hostbuffer, (strlen(hostbuffer) - strlen(prePort)));
        hostbuffer2[strlen(hostbuffer) - strlen(prePort)] = '\0';
        host_entry = gethostbyname(hostbuffer2);
    }
    else{
        strncpy(hostbuffer2, hostbuffer, strlen(hostbuffer));
        hostbuffer2[strlen(hostbuffer)] = '\0';
        host_entry = gethostbyname(hostbuffer2);
    }
    // To convert an Internet network
    // address into ASCII string
    IP = inet_ntoa(*((struct in_addr*)
                           host_entry->h_addr_list[0]));
    printf("Host IP: %s\n", IP);
    // To convert an Internet network
    // address into ASCII string
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int sockfd2, optval, n;
    socklen_t clientLength;
    pid_t childPID;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr(IP);
    serveraddr.sin_port = htons(portno);
    optval = 1;
    setsockopt(sockfd2, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));
    if ((sockfd2 = socket (AF_INET, SOCK_STREAM, 0)) <0) {
        perror("Problem in creating the socket\n");
        exit(-1);
    }
    if (connect(sockfd2, (struct sockaddr *) &serveraddr, sizeof(serveraddr))<0) {
        perror("Problem in connecting to the server");
        exit(3);
    }
    int test;
    char * request2 = strstr(URI, "//")+2;
    char * request3 = strstr(request2, "/");
    char * request4 = malloc(MAXLINE);
    printf("%s\n", request3);
    snprintf(request4, MAXLINE, "GET %s HTTP/1.0\r\n\r\n", request3);
    printf("%s", request4);
    if((test = send(sockfd2, request4, MAXLINE, 0) ) < 0){
                perror("test");
    }





    char * finalMessage = malloc(MAXLINE);
    char * message = malloc(MAXLINE);
    int CLRFCount = 0;
    int readSize = 0;
    int contentLength = 1;
    int inBody = 0;
    char * body;
    char * bodySoFar;
    while(1){
        setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeAlive, sizeof(timeAlive));
        while((readSize = recv(sockfd2, finalMessage, MAXLINE, 0)) > 0 && contentLength > 0){
            CLRFCount = 0;
            strncpy(message, finalMessage, readSize);
            send(connfd, finalMessage, readSize, 0);
            while(strstr(message, "\r\n\r\n") != NULL && inBody == 0){
                CLRFCount += 1;
                if(CLRFCount == 1 && inBody == 0){
                    contentLength = atoi(returnHeader(message, "Content-Length: "));
                    bodySoFar = malloc(contentLength);
                    strncpy(bodySoFar, strstr(message, "\r\n\r\n")+4, strlen(strstr(message, "\r\n\r\n")+4));
                    int test = strlen(bodySoFar);
                    contentLength -= test;
                    inBody = 1;
                    if(contentLength == 0){
                        return 0;
                    }
                }  
            }
            if(inBody == 1 && CLRFCount == 0)
            {
                contentLength -= readSize;
                strncpy(bodySoFar, finalMessage, readSize);
            }
        }

        //printf("%s\n", bodySoFar);




        //printf("%d", CLRFCount);
        //printf("%s\n", message);
        //printf("%d\n", contentLength);
        free(bodySoFar);
        free(finalMessage);
        free(message);
        fclose(blacklist);
        return 0;
    }
    fclose(blacklist);
    return -1;

}