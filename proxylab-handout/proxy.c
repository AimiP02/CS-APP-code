#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 4
#define SBUFSIZE 16

struct uri_content {
    char hostname[MAXLINE];
    char path[MAXLINE];
    char port[MAXLINE];
};

void do_request(int fd);
int parse_uri(char *uri, struct uri_content *uri_data);
void build_header(char *header, struct uri_content *uri_data, rio_t *myio);
int connect_server(char *hostname, int port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void sbuf_init(sbuf_t *sbuf, int n);
void sbuf_insert(sbuf_t *sbuf, int item);
int sbuf_remove(sbuf_t *sbuf);
void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *connection_header = "Connection: close\r\n";
static const char *proxy_connection_head = "Proxy-Connection: close\r\n";
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *end_header = "\r\n";

int main(int argc, char *argv[])
{
    int listenfd, connfd;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    pthread_t tid;

    struct sockaddr_storage clientaddr;

    if( argc != 2 ) {
        fprintf(stderr, "usage :%s <port> \n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    sbuf_init(&sbuf, SBUFSIZE);
    for(int i = 0; i < NTHREADS; i++) {
        Pthread_create(&tid, NULL, thread, NULL);
    }

    while( 1 ) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s)\n", hostname, port);
    }
    return 0;
}

void do_request(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char server[MAXLINE];

    int serverfd;

    rio_t client_rio, server_rio;

    Rio_readinitb(&client_rio, fd);
    Rio_readlineb(&client_rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    if( strcasecmp(method, "GET") ) {
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return ;
    }

    struct uri_content *uri_data = (struct uri_content *)malloc(sizeof(struct uri_content));

    parse_uri(uri, uri_data);
    build_header(server, uri_data, &client_rio);

    serverfd = Open_clientfd(uri_data->hostname, uri_data->port);
    if(serverfd < 0) {
        fprintf(stderr, "connect server failed\n");
        return ;
    }

    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, server, strlen(server));

    int n_read;

    while( (n_read = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0 ){
        printf("Proxy receives %d bytes from server\n", n_read);
        Rio_writen(fd, buf, n_read);
    }
    Close(serverfd);
}

int parse_uri(char *uri, struct uri_content *uri_data) {
    char *ptr, *hostname_ptr, *port_ptr, *path_ptr;

    ptr = strstr(uri, "//");
    ptr = ptr == NULL ? ptr : ptr + 2;
    port_ptr = strstr(ptr, ":");

    /* 如果没有显式端口 */
    if( port_ptr == NULL ) {
        hostname_ptr = strstr(ptr, "/");
        /* 如果没有path */
        if( hostname_ptr == NULL ) {
            sscanf(ptr, "%s", uri_data->hostname);
        }
        /* 如果有path */
        else {
            /* 用终止符截断 */
            *hostname_ptr = '\0';
            sscanf(ptr, "%s", uri_data->hostname);
            path_ptr = hostname_ptr;
            *path_ptr = '/';
            sscanf(path_ptr, "%s", uri_data->path);
        }
    }
    /* 如果有显式端口 => e.g. http://localhost:8888/... */
    else {
        *port_ptr = '\0';
        sscanf(ptr, "%s", uri_data->hostname);
        port_ptr += 1;
        path_ptr = strstr(port_ptr, "/");
        *path_ptr = '\0';
        sscanf(port_ptr, "%s", uri_data->port);
        *path_ptr = '/';
        sscanf(path_ptr, "%s", uri_data->path);
    }
}

void build_header(char *header, struct uri_content *uri_data, rio_t *myio) {
    char buf[MAXLINE], request_header[MAXLINE], host_header[MAXLINE], other_header[MAXLINE];

    sprintf(request_header, "GET %s HTTP/1.0\r\n", uri_data->path);
    sprintf(host_header, "HOST: %s\r\n", uri_data->hostname);
    while( Rio_readlineb(myio, buf, MAXLINE) > 0 ) {
        if( !strcmp(buf, end_header) ) {
            break;
        }
        else if( !strncasecmp(buf, "HOST", 4) ) {
            strcpy(host_header, buf);
        }
        else if( !strncasecmp(buf, "User-Agent", 10)
                || !strncasecmp(buf, "Proxy-Connection", 16)
                || !strncasecmp(buf, "Connection", 10) ) {
                    continue;
                }
        else {
            strcat(other_header, buf);
        }
    }

    sprintf(header, "%s%s%s%s%s%s%s", 
            request_header,
            host_header,
            connection_header,
            proxy_connection_head,
            user_agent_hdr,
            other_header,
            end_header);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXLINE];

    /* 构建Http response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* 打印Http response body */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content_type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content_length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void sbuf_init(sbuf_t *sbuf, int n) {
    sbuf->buf = Calloc(n, sizeof(int));
    sbuf->n = n;
    sbuf->front = sbuf->rear = 0;
    Sem_init(&sbuf->mutex, 0, 1);
    Sem_init(&sbuf->slots, 0, n);
    Sem_init(&sbuf->items, 0, 0);
}

void sbuf_insert(sbuf_t *sbuf, int item) {
    P(&sbuf->slots);
    P(&sbuf->mutex);
    sbuf->buf[ (++sbuf->rear)%(sbuf->n) ] = item;
    V(&sbuf->mutex);
    V(&sbuf->items);
}

int sbuf_remove(sbuf_t *sbuf) {
    int item;
    P(&sbuf->items);
    P(&sbuf->mutex);
    item = sbuf->buf[ (++sbuf->front)%(sbuf->n) ];
    V(&sbuf->mutex);
    V(&sbuf->slots);
    return item;
}

void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    while( 1 ) {
        int connfd = sbuf_remove(&sbuf);
        do_request(connfd);
        Close(connfd);
    }
}