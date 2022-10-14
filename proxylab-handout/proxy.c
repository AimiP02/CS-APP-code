#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void do_request(int fd);
int parse_uri(char *uri, char *hostname, char *path, int *port);
void build_header(char *header, char *hostname, char *path, int port, rio_t *myio);
int connect_server(char *hostname, int port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg);

/* You won't lose style points for including this long line in your code */
static const char *connection_header = "Connection: close\r\n";
static const char *proxy_connection_head = "Proxy-Connection: close\r\n";
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *end_header = "\r\n";

int main(int argc, char *argv[])
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE], port[MAXLINE];

    struct addrinfo hints, *listp, *ptr;
    int ret, optval = 1;
    
    if(argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_flags |= AI_NUMERICSERV;
    
    if((ret = getaddrinfo(NULL, argv[1], &hints, &listp)) != 0) {
        fprintf(stderr, "getaddrinfo failed (port %s): %s\n", argv[1], gai_strerror(ret));
        exit(1);
    }

    for(ptr = listp; ptr; ptr = ptr->ai_next) {
        if((listenfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) < 0) {
            continue;
        }

        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

        if(bind(listenfd, ptr->ai_addr, ptr->ai_addrlen) == 0) {
            break;
        }
        close(listenfd);
    }

    freeaddrinfo(listp);
    if(!ptr) {
        exit(1);
    }

    if(listen(listenfd, LISTENQ) < 0) {
        close(listenfd);
        fprintf(stderr, "listen failed (port %s)\n", argv[1]);
        exit(1);
    }

    while(1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if((ret = getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0)) != 0) {
            fprintf(stderr, "getnameinfo failed (port %s)\n", argv[1]);
            continue;
        }
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        do_request(connfd);
        close(connfd);
    }

    return 0;
}

void do_request(int fd) {
    int n_read;
    int port = 80;
    int serverfd;
    char header_to_server[MAXLINE];
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];

    rio_t client_myio, server_myio;

    Rio_readinitb(&client_myio, fd);
    n_read = Rio_readlineb(&client_myio, buf, MAXLINE);

    if(!n_read) {
        return ;
    }

    printf("%s", buf);

    sscanf(buf, "%s %s %s", method, uri, version);

    if(strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not implemented");
        return ;
    }

    parse_uri(buf, hostname, path, &port);
    build_header(header_to_server, hostname, path, port, &client_myio);

    serverfd = connect_server(hostname, port);

    if(serverfd < 0) {
        fprintf(stderr, "connect_server failed\n");
        return ;
    }

    Rio_readinitb(&server_myio, serverfd);
    Rio_writen(serverfd, header_to_server, strlen(header_to_server));

    while((n_read = Rio_readlineb(&server_myio, buf, MAXLINE)) > 0) {
        printf("Proxy receives %d bytes from server\n", n_read);
        Rio_writen(fd, buf, n_read);
    }
    close(serverfd);
}

int parse_uri(char *uri, char *hostname, char *path, int *port) {
    char *ptr, *host_ptr, *path_ptr, *port_ptr;
    ptr = strstr(uri, "//");
    /* 如果uri里面有http就跳过，提取出hostname的位置 */
    ptr = ptr ? ptr + 2 : ptr;
    port_ptr = strstr(uri, ":");
    /* 如果没有显式port */
    if(!port_ptr) {
        host_ptr = strstr(ptr, "/");
        /* 如果没有path */
        if(!host_ptr) {
            sscanf(ptr, "%s", hostname);
        }
        else {
            /* 用终止符截断提取hostname */
            *host_ptr = "\0";
            sscanf(ptr, "%s", hostname);
            path_ptr = host_ptr;
            *path_ptr = "/";
            sscanf(path_ptr, "%s", path);
        }
    }
    /* 存在显式的port -> e.g. localhost:8888 */
    else {
        /* 用终止符截断提取hostname */
        *port_ptr = "\0";
        sscanf(ptr, "%s", hostname);
        port_ptr += 1;
        sscanf(port_ptr, "%d%s", port, path);
    }
}

void build_header(char *header, char *hostname, char *path, int port, rio_t *myio) {
    char buf[MAXLINE], request_header[MAXLINE], host_header[MAXLINE], other_header[MAXLINE];

    /* 构建request header和host header */
    sprintf(request_header, "GET %s HTTP/1.0\r\n", path);
    sprintf(host_header, "HOST: %s\r\n", hostname);

    while(Rio_readlineb(myio, buf, MAXLINE) > 0) {
        /* 如果buf的内容是'\r\n'，说明到了EOF */
        if(!strcmp(buf, end_header)) {
            break;
        }
        else if(!strncasecmp(buf, "HOST", 4)) {
            strcpy(host_header, buf);
        }
        else if(!strncasecmp(buf, "Connection", 10) || !strncasecmp(buf, "Proxy", 5)
                || !strncasecmp(buf, "User-Agent", 10))
                {
                    continue;
                }
        else {
            strcat(other_header, buf);
        }
    }

    /* 构建发送给服务端的header */
    sprintf(header, "%s%s%s%s%s%s%s", 
            request_header,
            host_header,
            connection_header,
            proxy_connection_head,
            user_agent_hdr,
            other_header,
            end_header);
}

int connect_server(char *hostname, int port) {
    struct addrinfo hints, *listp, *ptr;
    int connfd, ret;
    char port_str[10];

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_flags |= AI_NUMERICSERV;
    
    sprintf(port_str, "%d", port);

    if((ret = getaddrinfo(NULL, port, &hints, &listp))) {
        fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n", hostname, port_str, gai_strerror(ret));
        return -2;
    }

    for(ptr = listp; ptr; ptr = ptr->ai_next) {
        if((connfd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) < 0) {
            continue;
        }
        if((connect(connfd, ptr->ai_addr, ptr->ai_addrlen)) != -1) {
            break;
        }
        close(connfd);
    }

    freeaddrinfo(listp);
    if(!ptr) {
        return -1;
    }
    return connfd;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg) {
    char buf[MAXLINE];

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));

    sprintf(buf, "<html><title>Proxy Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s %s: %s\r\n", cause, errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
}