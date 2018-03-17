#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT "9034"

#define MAXDATASIZE 100

void *get_in_addr(struct sockaddr *sa){
    if(sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int build_http_request(char ** message, int *size, char * method, char * path, char * host, char * port, char * body){
    
    *size = strlen(path)+strlen(body)+100; //rough size estimate
    char * buffer = (char *)malloc(*size);
    char * tbuf = (char *)malloc(64);
    int tlen;
    *message = buffer;
    int idx = 0;

    //method
    if(strncmp(method, "-G", 2) == 0){
        strncpy(buffer+idx, "GET", 3);
        idx += 3;
    }
    else if(strncmp(method, "-P", 2) == 0){
        strncpy(buffer+idx, "POST", 4);
        idx += 4;
    }
    else{
        printf("build http request : method error\n");
        return -1;
    }
    buffer[idx++] = ' ';

    //url
    tlen = strlen(path);
    strncpy(buffer+idx, path, tlen);
    idx += tlen;
    buffer[idx++] = ' ';

    //version
    tbuf = "HTTP/1.0\r\n";
    tlen = strlen(tbuf);
    strncpy(buffer+idx, tbuf, tlen);
    idx += tlen;

    //<headers>

    //Host
    tbuf = "Host: ";
    tlen = strlen(tbuf);
    strncpy(buffer+idx, tbuf, tlen);
    idx += tlen;

    tlen = strlen(host);
    strncpy(buffer+idx, host, tlen);
    idx += tlen;

    buffer[idx++] = ':';

    tlen = strlen(port);
    strncpy(buffer+idx, port, tlen);
    idx += tlen;

    buffer[idx++] = '\r';
    buffer[idx++] = '\n';

    //End of headers 
    buffer[idx++] = '\r';
    buffer[idx++] = '\n';

    //just to be safe
    buffer[idx] = '\0';

    *size = idx;

    return 0;
}


int parse_url(const char * url, char ** hostname, char ** port, char ** path){
    int hname_idx;
    int port_idx;
    int path_idx;

    int url_size = strlen(url);
    if(strncmp(url, "http://", 7) != 0){
        printf("parse_url : must start with 'http://'\n");
        return -1;
    }
    hname_idx = 7;

    for(port_idx = hname_idx; port_idx < url_size; port_idx++){
        if(url[port_idx] == ':'){
            port_idx += 1;
            break;
        }
    }
    if(port_idx == url_size){
        printf("parse_url : must specify port\n");
        return -1;
    }

    for(path_idx = port_idx; path_idx < url_size; path_idx++){
        if(url[path_idx] == '/'){
            break;
        }
    }
    if(path_idx == url_size){
        printf("parse_url : must specify path\n");
        return -1;
    }

    char * buffer = (char *)malloc(url_size-hname_idx+3);
    if(buffer == NULL){
        printf("parse_url : malloc fail\n"); 
        return -1;
    }
    
    *hostname = buffer;
    strncpy(*hostname, &(url[hname_idx]), port_idx-hname_idx-1);
    (*hostname)[port_idx - hname_idx-1] = '\0';

    *port = &(buffer[port_idx - hname_idx + 1]);
    strncpy(*port, &(url[port_idx]), path_idx-port_idx);
    (*port)[path_idx-port_idx] = '\0';

    *path = &(buffer[path_idx - hname_idx + 2]);
    strncpy(*path, &(url[path_idx]), url_size-path_idx);
    (*path)[url_size-path_idx] = '\0';

    return 0;
}

int main(int argc, char * argv[]){
    int sockfd, numbytes;
    char buf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
 
    char s[INET6_ADDRSTRLEN];
    char * hostname;
    char * port;
    char * path;

    char * message;
    int msize;

    if(argc != 3){
        fprintf(stderr, "usage: ./client -[G|P] url\n");
        exit(1);
    }

    //parse url
    if(parse_url(argv[2], &hostname, &port, &path) == -1){
        printf("Incorrect URL: %s\n", argv[2]);
        exit(1);
    } 

    //build HTTP request
    if(build_http_request(&message, &msize, argv[1], path, hostname, port, "") == -1){
        printf("HTTP request build error\n");
        exit(1);
    }

    printf("message:\n%s\n", message);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for(p=servinfo; p!=NULL; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype,
                        p->ai_protocol)) == -1){
            perror("client: socket");
            continue;
        }
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            close(sockfd);
            perror("client: connect");
            continue;
        }
        break;
    }
    if(p == NULL){
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);

    printf("client: connecting to %s\n", s);
    freeaddrinfo(servinfo);
    //strncpy(buf, "Hello there!", sizeof(buf));
    if(send(sockfd, message, msize, 0) == -1){
        perror("send");
    }

    if((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1){
        perror("recv");
        exit(1);
    }

    buf[numbytes] = '\0';
    printf("Data from server: %s\n", buf);
    close(sockfd);
    printf("client: connection closed\n");
    return 0;
}
