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
#include <time.h>
#include <fcntl.h>
//#include "requestHandler.h"
//#include "httpHeaders.h"

#define MAXLINE 8192 /*max text line length*/
#define LISTENQ 1024 /*maximum number of client connections*/
int forceStopChildren = 0;

int beginRequest(int connfd, int timeout);
int checkMessage(int connfd, char *method, char *URI, char *version, int errNO, int keepAlive);
int attemptRequest(int connfd, char *URI, char *version, char *request, int keepalive, int requestSize, int URILength, int timeout);

typedef struct
{
    int done;
    pthread_mutex_t mutex;
} shared_data;

static shared_data *data = NULL;
static shared_data *data2 = NULL;

void initialise_shared()
{
    // place our shared data in shared memory
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_SHARED | MAP_ANONYMOUS;
    data = mmap(NULL, sizeof(shared_data), prot, flags, -1, 0);
    assert(data);

    data->done = 0;

    // initialise mutex so it works properly in shared memory
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&data->mutex, &attr);
}
void initialise_shared2()
{
    // place our shared data in shared memory
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_SHARED | MAP_ANONYMOUS;
    data2 = mmap(NULL, sizeof(shared_data), prot, flags, -1, 0);
    assert(data2);

    data2->done = 0;

    // initialise mutex so it works properly in shared memory
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&data2->mutex, &attr);
}

void checkStop(int signum)
{

    // printf("testing\n");
    int status = 0;
    pid_t wpid;
    while ((wpid = wait(&status)) > 0)
        ;
    munmap(data, sizeof(data));
    munmap(data2, sizeof(data2));
    exit(0);
}
void checkStopChild(int signum)
{
    forceStopChildren = 1;
}
int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <port> <timeout>\n", argv[0]);
        exit(0);
    }

    int portno;
    portno = atoi(argv[1]);
    int timeout = atoi(argv[2]);
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int sockfd, optval, connfd;
    socklen_t clientLength;
    pid_t childPID;
    signal(SIGINT, checkStop);

    mkdir("cache", 0700);

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(portno);

    initialise_shared();
    initialise_shared2();

    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Problem in creating the socket\n");
        exit(-1);
    }

    if (bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        perror("ERROR on binding\n");
        exit(-2);
    }

    if (listen(sockfd, LISTENQ) < 0)
    {
        perror("Maximum sessions reached!\n");
    }
    printf("Waiting for connections\n");

    while (1)
    {
        clientLength = sizeof(clientaddr);
        connfd = accept(sockfd, (struct sockaddr *)&clientaddr, &clientLength);

        printf("Connection accepted.\n");
        if ((childPID = fork()) == 0)
        {
            signal(SIGINT, checkStopChild);
            if (close(sockfd) < 0)
            {
                perror("ERROR CLOSING LISTENING SOCKET\n");
            }
            printf("Socket opened: %d\n", connfd);
            beginRequest(connfd, timeout);
            printf("Closing socket: %d\n", connfd);
            if (close(connfd) < 0)
            {
                perror("ERROR CLOSING CONNECTION SOCKET\n");
            }
            exit(0);
        }
        if (close(connfd) < 0)
        {
            perror("ERROR CLOSING CONNECTION SOCKET (PARENT)\n");
        }
        waitpid(-1, NULL, WNOHANG);
    }
    printf("parent exiting\n");
    munmap(data, sizeof(data));
    munmap(data2, sizeof(data2));
    return -1;
}
char *returnHeader(char *request, char *desiredHeader)
{
    char host_buf[MAXLINE];
    strcpy(host_buf, request);
    char *host = strstr(host_buf, desiredHeader);
    strtok(host, " ");
    char *hostname = strtok(NULL, "\r\n");
    return hostname;
}

int beginRequest(int connfd, int timeout)
{
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
    setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&defaultTimeout, sizeof(defaultTimeout));
    // int retval = select(connfd+1, &testingTimeout, NULL, NULL, &timeAlive);
    if (forceStopChildren == 1)
    {
        defaultTimeout.tv_sec = 2;
        defaultTimeout.tv_usec = 0;
        setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&defaultTimeout, sizeof(defaultTimeout));
        stopTheLoop = 1;
    }

    int CLRFCount;
    char *finalMessage;
    finalMessage = malloc(MAXLINE);
    while (1)
    {
        setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeAlive, sizeof(timeAlive));
        while ((readSize = recv(connfd, finalMessage, MAXLINE, 0)) > 0)
        {
            CLRFCount = 0;
            strncpy(message, finalMessage, readSize);

            while (strstr(message, "\r\n\r\n") != NULL)
            {
                CLRFCount += 1;
                if (CLRFCount == 1)
                {
                    break;
                }
            }
            if (CLRFCount == 1)
            {
                break;
            }
        }
        if (readSize <= 0)
        {
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
        for (int i = 0; message[i]; i++)
        {
            lowerCaseMessage[i] = tolower(message[i]);
        }
        printf("Received request %s %s %s\n", method, URI, version);
        // printf("still in loop\n");

        if (strstr(lowerCaseMessage, "connection: keep-alive") != NULL && forceStopChildren == 0)
        {
            keepAlive = 1;
            setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeAlive, sizeof(timeAlive));
        }
        else if (strstr(lowerCaseMessage, "connection: close") != NULL || forceStopChildren == 1)
        {
            keepAlive = 0;
            forceStopChildren = 0;
        }
        else
        {
            if (strcmp(version, "HTTP/1.1") == 0)
            {
                keepAlive = 1;
                setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeAlive, sizeof(timeAlive));
            }
            else
            {
                keepAlive = 0;
            }
        }

        if (strstr(message, "HTTP/") == NULL)
        {
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("No method.\n");
        }
        else if (strstr(message, "\r\n\r\n") == NULL)
        {
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("Improper URI\n");
        }
        else if (strstr(URI, "../") != NULL)
        {
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("Improper URI\n");
        }
        else if (version[4] != '/')
        {
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("Improper URI\n");
        }
        else if (strstr(message, "Host:") == NULL && strcmp(version, "HTTP/1.1") == 0)
        {
            checkMessage(connfd, method, URI, version, 400, keepAlive);
            printf("No host header in HTTP/1.1 request\n");
        }
        else if (check400 > 4 || check400 < 3)
        {
            if (checkMessage(connfd, method, URI, version, 400, keepAlive) != 0)
            {
                printf("400 error\n");
            }
        }
        else if (strchr(test, ':') == NULL && check400 == 4)
        {
            if (checkMessage(connfd, method, URI, version, 400, keepAlive) != 0)
            {
                printf("400 error\n");
            }
        }
        else if ((errNo = checkMessage(connfd, method, URI, version, 0, keepAlive)) != 0)
        {
            printf("405 or 505 encountered.\n");
        }

        else
        {

            errNo = attemptRequest(connfd, URI, version, message, keepAlive, strlen(message), strlen(URI), timeout);

            if (errNo != 0)
            {
                printf("error received while transmitting file\n");
            }
            if (keepAlive == 0)
            {
                printf("Client connection closed.\n");
                free(finalMessage);
                return -1;
            }
        }
        memset(&message, 0, MAXLINE);
        if (keepAlive == 0)
        {
            printf("Client connection closed.\n");
            free(finalMessage);
            return -1;
        }
    }
    if (forceStopChildren == 1 && stopTheLoop == 0)
    {
        beginRequest(connfd, timeout);
    }
    else if (readSize <= 0)
    {
        printf("Client timed out!\n");
        free(finalMessage);
        return -1;
    }
    free(finalMessage);
    return 0;
}

int checkMessage(int connfd, char *method, char *URI, char *version, int errNO, int keepAlive)
{
    char getErrorHeader[MAXLINE];
    if (errNO == 400)
    {
        char getError[MAXLINE] = "<!DOCTYPE html>\n<html>\n<title>400 Bad Request</title>\n<body>The request could not be parsed or is malformed</body>\n</html>";
        if (keepAlive == 1)
        {
            snprintf(getErrorHeader, MAXLINE, "HTTP/1.0 400 Bad Request\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n", strlen(getError));
        }
        else
        {
            snprintf(getErrorHeader, MAXLINE, "HTTP/1.0 400 Bad Request\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", strlen(getError));
        }

        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 400;
    }
    else if (errNO == 403)
    {
        char getError[MAXLINE] = "<!DOCTYPE html>\n<html>\n<title>403 Forbidden</title>\n<body>The requested file can not be accessed due to a file permission issue</body>\n</html>";
        if (keepAlive == 1)
        {
            snprintf(getErrorHeader, MAXLINE, "%s 403 Forbidden\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }
        else
        {
            snprintf(getErrorHeader, MAXLINE, "%s 403 Forbidden\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }
        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 403;
    }
    else if (errNO == 404)
    {
        char getError[MAXLINE] = "<!DOCTYPE html>\n<html>\n<title>404 Not Found</title>\n<body>The requested file can not be found in the document tree</body>\n</html>";
        if (keepAlive == 1)
        {
            snprintf(getErrorHeader, MAXLINE, "%s 404 Not Found\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }
        else
        {
            snprintf(getErrorHeader, MAXLINE, "%s 404 Not Found\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }
        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 404;
    }
    else if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0)
    {
        char getError[MAXLINE] = "<!DOCTYPE html>\n<html>\n<title>505 HTTP Version Not Supported</title>\n<body>An HTTP version other than 1.0 or 1.1 was requested.</body>\n</html>";
        if (keepAlive == 1)
        {
            snprintf(getErrorHeader, MAXLINE, "HTTP/1.0 505 HTTP Version Not Supported\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n", strlen(getError));
        }
        else
        {
            snprintf(getErrorHeader, MAXLINE, "HTTP/1.0 505 HTTP Version Not Supported\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", strlen(getError));
        }
        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 505;
    }
    else if (strcmp(method, "GET") != 0)
    {
        char getError[MAXLINE] = "<!DOCTYPE html>\n<html>\n<title>405 Method Not Allowed</title>\n<body>A method other than GET was requested.</body>\n</html>";
        if (keepAlive == 1)
        {
            snprintf(getErrorHeader, MAXLINE, "%s 405 Method Not Allowed\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }
        else
        {
            snprintf(getErrorHeader, MAXLINE, "%s 405 Method Not Allowed\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: %lu\r\n\r\n", version, strlen(getError));
        }
        send(connfd, getErrorHeader, strlen(getErrorHeader), 0);
        write(connfd, getError, strlen(getError));
        return 405;
    }
    else
    {
        return 0;
    }
}

char *get_filename_ext(char *filename)
{
    char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        return "";
    return dot + 1;
}
int getFileType(char *URI, char *fileType)
{
    char *filename = get_filename_ext(URI);
    if (strcmp(filename, "html") == 0 || strcmp(filename, "htm") == 0)
    {
        strcat(fileType, "text/html");
        return 0;
    }
    else if (strcmp(filename, "txt") == 0)
    {
        strcat(fileType, "text/plain");
        return 0;
    }
    else if (strcmp(filename, "png") == 0)
    {
        strcat(fileType, "image/png");
        return 0;
    }
    else if (strcmp(filename, "gif") == 0)
    {
        strcat(fileType, "image/gif");
        return 0;
    }
    else if (strcmp(filename, "jpg") == 0)
    {
        strcat(fileType, "image/jpeg");
        return 0;
    }
    else if (strcmp(filename, "css") == 0)
    {
        strcat(fileType, "text/css");
        return 0;
    }
    else if (strcmp(filename, "js") == 0)
    {
        strcat(fileType, "application/javascript");
        return 0;
    }
    else
    {
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

int sendFile(int connfd, char *URI, char *version, char *filename, int keepAlive)
{
    FILE *fp;
    fp = fopen(filename, "rb");
    printf("Arrived in reader\n");
    pthread_mutex_unlock(&data->mutex);
    struct flock lock2, savelock;
    //fcntl(fd, F_GETLK, &readLock)
    // SEND FILE NOW!
    lock2.l_type = F_RDLCK;
    lock2.l_start = 0;
    lock2.l_whence = 0;
    lock2.l_len = 0;
    printf("%d\n", fileno(fp));
    fcntl(fileno(fp), F_SETLKW, &lock2);
    printf("Got past lock...\n");

    char contentHeader[MAXLINE];
    int length = 0;
    int filesize = 0;

    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    rewind(fp);
    // CRAFTING HEADER.
    char fileType[MAXLINE];
    bzero(fileType, MAXLINE);

    if (URI[strlen(URI) - 1] == '/')
    {
        strcpy(fileType, "text/html");
    }
    else
    {
        getFileType(URI, fileType);
    }
    length += snprintf(contentHeader, MAXLINE, "%s 200 OK\r\n", version);

    length += snprintf(contentHeader + length, MAXLINE - length, "Content-Type: %s\r\n", fileType);

    if (keepAlive == 1 && forceStopChildren == 0)
    {
        length += snprintf(contentHeader + length, MAXLINE - length, "Connection: keep-alive\r\n");
    }
    else if (keepAlive == 0 || forceStopChildren == 1)
    {
        length += snprintf(contentHeader + length, MAXLINE - length, "Connection: close\r\n");
    }
    length += snprintf(contentHeader + length, MAXLINE - length, "Content-Length: %u\r\n\r\n", filesize);
    send(connfd, contentHeader, strlen(contentHeader), 0);
    // CRAFTING CONTENTS WITH FILE
    char *transferBuf;
    transferBuf = malloc(MAXLINE); // use a malloc instead of a static array.

    int test;
    while (/*!feof(fp)*/ filesize > 0)
    { // https://stackoverflow.com/questions/33783470/sending-picture-via-tcp
        test = fread(transferBuf, 1, MAXLINE, fp);
        if (test > 0)
        {
            if ((test = send(connfd, transferBuf, test, 0)) < 0)
            {
                perror("test");
            }
            filesize -= test;
        }
        else
        {
            printf("%s\n", transferBuf);
            break;
        }
        memset(transferBuf, 0, MAXLINE);
    }
    /*
    FILE* testFile;
    testFile = fopen("test.jpg", "wb");
    fwrite(transferBuf, 1, sizeof(transferBuf), testFile);
    fclose(testFile);
    */

    // printf("%ld\n");
    lock2.l_type = F_UNLCK;
    fcntl(fileno(fp), F_SETLK, &lock2);
    printf("exiting file cache\n");
    free(transferBuf);
    // bzero(URI, MAXLINE);
    fclose(fp);
    return 0;
}

int checkBlacklist(char *destHostname, char *IP, char *URI, char *version, int keepalive, int connfd)
{
    FILE *blacklist;
    int errNo;
    char line[253];
    size_t len = 0;
    blacklist = fopen("blacklist", "r");

    while (fgets(line, sizeof(line), blacklist))
    {
        len = strlen(line);
        char *dub = malloc(strlen("www.") + strlen(destHostname));
        strcpy(dub, "www.");
        strcat(dub, destHostname);
        sscanf(line, "%[^\r\n]", line);
        sscanf(line, "%[^\n]", line);
        if (destHostname == NULL)
        {
            printf("%d\n", strcmp(line, IP));
        }
        if (strcmp(line, destHostname) == 0 || strcmp(line, dub) == 0 || strcmp(line, IP) == 0)
        {
            if ((errNo = checkMessage(connfd, "GET", URI, version, 403, keepalive)) != 0)
            {
                free(dub);
                fclose(blacklist);
                return -1;
            }
        }
        free(dub);
    }
    fclose(blacklist);
    return 0;
}

int attemptRequest(int connfd, char *URI, char *version, char *request, int keepalive, int requestSize, int URILength, int timeout)
{
    // printf("%s\n", request);
    struct timeval timeAlive;
    timeAlive.tv_sec = 10;
    timeAlive.tv_usec = 0;
    char *destHostname = returnHeader(request, "Host: ");

    int hashRes;
    hashRes = hash(URI);

    int length = snprintf(NULL, 0, "%d", hashRes);
    char *str;
    str = malloc(length + 1);
    snprintf(str, length + 1, "%d", hashRes);

    if (checkBlacklist(destHostname, "", URI, version, keepalive, connfd) == -1)
    {
        return -1;
    }

    char hostbuffer[256];
    char hostbuffer2[256];
    strcpy(hostbuffer, destHostname);
    char *IP;
    struct hostent *host_entry;

    int noCache = 0;

    // To retrieve hostname

    // To retrieve host information

    FILE *fp;
    char path[MAXLINE];
    strcat(path, "cache/");
    strcat(path, str);
    printf("%s\n", path);
    if (strstr(URI, "?") != NULL)
    {
        noCache = 1;
    }

    char *prePort = strstr(destHostname, ":");
    int portno;
    if (prePort != NULL)
    {
        portno = atoi(prePort + 1);
        strncpy(hostbuffer2, hostbuffer, (strlen(hostbuffer) - strlen(prePort)));
        hostbuffer2[strlen(hostbuffer) - strlen(prePort)] = '\0';
        host_entry = gethostbyname(hostbuffer2);
    }
    else
    {
        strncpy(hostbuffer2, hostbuffer, strlen(hostbuffer));
        hostbuffer2[strlen(hostbuffer)] = '\0';
        portno = 80;
        host_entry = gethostbyname(hostbuffer2);
    }
    // To convert an Internet network
    // address into ASCII string
    IP = inet_ntoa(*((struct in_addr *)
                         host_entry->h_addr_list[0]));
    printf("Host IP: %s\n", IP);
    if (checkBlacklist("", IP, URI, version, keepalive, connfd) == -1)
    {
        return -1;
    }
    pthread_mutex_unlock(&data->mutex);
    if (access(path, F_OK) == 0)
    {

        struct stat attr;
        stat(path, &attr);
        printf("Last modified time: %s\n", ctime(&attr.st_mtime));
        time_t rawtime;
        rawtime = time(NULL);
        printf("Current local time and date: %s\n", ctime(&rawtime));

        int timeDiff = difftime(rawtime, attr.st_mtime);
        if (timeDiff >= timeout && noCache == 0)
        {
            printf("Execution time = %d\n", timeDiff);
            remove(path);
            fp = fopen(path, "wb");
            pthread_mutex_unlock(&data->mutex);
        }
        else
        {

            sendFile(connfd, URI, version, path, keepalive); // sendFile(int connfd, char* URI,char* version, char* filename, int keepAlive)
            bzero(path, MAXLINE);
            printf("Execution time = %d\n", timeDiff);
            // free(path);
            free(str);
            return 0;
        }
    }
    else if (noCache == 0)
    {
        fp = fopen(path, "wb");
        pthread_mutex_unlock(&data->mutex);
    }
    free(str);
    //



    // To convert an Internet network
    // address into ASCII string
    struct sockaddr_in serveraddr; /* server's addr */
                                   /* client addr */
    int sockfd2, optval;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr(IP);
    serveraddr.sin_port = htons(portno);
    optval = 1;
    setsockopt(sockfd2, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));
    if ((sockfd2 = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Problem in creating the socket\n");
        close(sockfd2);
        return -1;
    }
    if (connect(sockfd2, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
    {
        perror("Problem in connecting to the server");
        checkMessage(connfd, "GET", URI, version, 403, keepalive);
        remove(path);
        close(sockfd2);
        return -1;
    }

    int test;
    char *request2 = strstr(URI, "//") + 2;
    char *request3 = strstr(request2, "/");
    char request4[MAXLINE];
    // printf("%s\n", request3);
    snprintf(request4, MAXLINE, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", request3, hostbuffer2);
    // printf("%s", request4);
    if ((test = send(sockfd2, request4, MAXLINE, 0)) < 0)
    {
        perror("testErrorSend");
    }

    // CRAFTING HEADER.
    char fileType[MAXLINE];
    bzero(fileType, MAXLINE);
    char contentHeader[MAXLINE];
    int length2 = 0;
    if (URI[strlen(URI) - 1] == '/')
    {
        strcpy(fileType, "text/html");
    }
    else
    {
        getFileType(URI, fileType);
    }

    char *finalMessage = malloc(MAXLINE);
    char *message = malloc(MAXLINE);
    int CLRFCount = 0;
    int readSize = 0;
    int contentLength = 1;
    int inBody = 0;
    struct flock lock, savelock;
    //fcntl(fd, F_GETLK, &readLock)
    // SEND FILE NOW!
    lock.l_type = F_WRLCK;
    lock.l_start = 0;
    lock.l_whence = 0;
    lock.l_len = 0;
    int test2 = fcntl(fileno(fp), F_SETLKW, &lock);
    printf("File Locked for writing.\n");
    printf("%d\n", fileno(fp));
    //sleep(100);
    while (1)
    {

        //setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeAlive, sizeof(timeAlive));
        while ((readSize = recv(sockfd2, finalMessage, MAXLINE, 0)) > 0 && contentLength > 0)
        {
            CLRFCount = 0;
            // printf("%d\n", readSize);
            strncpy(message, finalMessage, readSize);
            // send(connfd, finalMessage, readSize, 0);
            if (readSize == 0)
            {
                return 0;
            }
            while (strstr(finalMessage, "\r\n\r\n") != NULL && inBody == 0)
            {
                CLRFCount += 1;
                if (CLRFCount == 1 && inBody == 0)
                {
                    // printf("%s\n", finalMessage);
                    if (strstr(finalMessage, "200 OK") == NULL)
                    {
                        noCache = 1;
                        printf("404 error encountered\n");
                        fclose(fp);
                        remove(path);
                    }
                    length2 += snprintf(contentHeader, MAXLINE, "%s 200 OK\r\n", version);

                    length2 += snprintf(contentHeader + length2, MAXLINE - length2, "Content-Type: %s\r\n", fileType);

                    if (keepalive == 1 && forceStopChildren == 0)
                    {
                        length2 += snprintf(contentHeader + length2, MAXLINE - length2, "Connection: keep-alive\r\n");
                    }
                    else if (keepalive == 0 || forceStopChildren == 1)
                    {
                        length2 += snprintf(contentHeader + length2, MAXLINE - length2, "Connection: close\r\n");
                    }
                    contentLength = atoi(returnHeader(message, "Content-Length: "));
                    char *check = strstr(finalMessage, "\r\n\r\n");
                    length2 += snprintf(contentHeader + length2, MAXLINE - length2, "Content-Length: %u\r\n\r\n", contentLength);
                    check = check + 4;
                    int header_length = check - finalMessage;
                    if (noCache == 0)
                    {
                        send(connfd, contentHeader, strlen(contentHeader), 0);
                        fwrite(finalMessage + header_length, 1, readSize - header_length, fp);
                        send(connfd, finalMessage + header_length, readSize - header_length, 0);
                    }
                    else
                    {
                        send(connfd, finalMessage, readSize, 0);
                    }
                    contentLength -= (readSize - header_length);
                    inBody = 1;
                    if (contentLength == 0)
                    {
                        break;
                    }
                }
            }

            if (inBody == 1 && CLRFCount == 0)
            {
                if (noCache == 0)
                {
                    fwrite(finalMessage, 1, readSize, fp);
                }
                // printf("%s\n", finalMessage);
                send(connfd, finalMessage, readSize, 0);
                contentLength -= readSize;
            }
            if (contentLength == 0)
            {
                break;
            }
            memset(finalMessage, 0, MAXLINE);
        }
        // sendFile(connfd, URI, version, path, keepalive);
        printf("exiting file write\n");
        // free(path);
        if(readSize == 0){
            printf("Received a read size of 0!!!! Remaining Content Length: %d\n", contentLength);
        }
        else if(readSize == -1){
            printf("Received a read size of -1!!!!Remaining Content Length: %d\n", contentLength);
        }
        lock.l_type = F_UNLCK;
        fcntl(fileno(fp), F_SETLK, &lock);

        bzero(path, MAXLINE);
        close(sockfd2);
        free(finalMessage);
        free(message);
        if(noCache == 0){
            fclose(fp);
        }
        return 0;
    }
    printf("exiting file write\n");
    return -1;
}
