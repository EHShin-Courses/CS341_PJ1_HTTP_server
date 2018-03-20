#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#define CHUNKSIZE 1024

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

#define TEMP_PATH "./temptemptem"

enum method {GET, POST};

void *get_in_addr(struct sockaddr *sa){
    if(sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int parse_http_response(char * buf, int resp_bytes, int * clength, int * cidx){
    // information to extract:
    //      content length (if exists)
    //      content start idx (if exists)
    int idx = 0;
    int newLine = 1;
    *cidx = -1;
    int clidx = -1;
    char * uu;
    while(idx < resp_bytes){
        if(newLine){
            newLine = 0;

            //look for end of header
            if(strncmp(buf+idx, "\r\n", 2) == 0){
                *cidx = idx+2;
                break;
            }

            //look for content-length
            if(strncmp(buf+idx, "Content-Length: ",16) == 0){
                clidx = idx + 16;
            }
        }

        if(buf[idx] == '\r'){
            idx++;
            newLine = 1;
        }
        idx++;
    }
    if(clidx == -1){
        *clength = -1;
    }
    else{
        *clength = strtol(buf+clidx, &uu, 10);
    }
    return 0;
}


int build_http_request(char ** message, int *size, enum method mthd, char * path, char * host, char * port, int clength){
    
    *size = 100+strlen(path); //rough size estimate
    char * buffer = (char *)malloc(*size);
    char * tbuf;
    char sbuf[64];
    int tlen;
    *message = buffer;
    int idx = 0;

    //method
    if(mthd == GET){
        strncpy(buffer+idx, "GET", 3);
        idx += 3;
    }
    else if(mthd == POST){
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

    //Content-Length
    tbuf = "Content-Length: ";
    tlen = strlen(tbuf);
    strncpy(buffer+idx, tbuf, tlen);
    idx += tlen;

    sprintf(sbuf, "%d", clength);
    tlen = strlen(sbuf);
    strncpy(buffer+idx, sbuf, tlen);
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
    /*
    if(strncmp(url, "http://", 7) != 0){
        printf("parse_url : must start with 'http://'\n");
        return -1;
    }
    */
    hname_idx = 0;

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
    char buf[CHUNKSIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
 
    char s[INET6_ADDRSTRLEN];
    char * hostname;
    char * port;
    char * path;
    char * message;
    int msize;
    int tempfd;
    int numwrite;
    long fsize;

    if(argc != 3){
        fprintf(stderr, "usage: ./client -[g|p] url\n");
        exit(1);
    }

    enum method mthd;
    if(strncmp(argv[1], "-G", 2) == 0 || strncmp(argv[1], "-g", 2) == 0){
        mthd = GET;
    }
    else if(strncmp(argv[1], "-P", 2) == 0 || strncmp(argv[1], "-p", 2)==0){
        mthd = POST;
    }
    else{
        fprintf(stderr, "method must be -g or -p\n");
        exit(1);
    }


    //parse url
    if(parse_url(argv[2], &hostname, &port, &path) == -1){
        printf("Incorrect URL: %s\n", argv[2]);
        exit(1);
    }

    //make temporary file
    fsize = 0;
    if(mthd == POST){
        tempfd = creat(TEMP_PATH, 0777);
        while(1){
            numbytes = read(STDIN_FILENO, buf, CHUNKSIZE);
            if(numbytes == 0) break;
            numwrite = write(tempfd, buf, numbytes);
            if(numwrite != numbytes){
                printf("write error!\n");
                exit(1);
            }
            fsize += numwrite;
        }
        close(tempfd);
    }

    //build HTTP request (w/o content)
    if(build_http_request(&message, &msize, mthd, path, hostname, port, fsize) == -1){
        printf("HTTP request build error\n");
        exit(1);
    }

    //printf("message:\n%s\n", message);

    //make connection
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0){
        //fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for(p=servinfo; p!=NULL; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype,
                        p->ai_protocol)) == -1){
            //perror("client: socket");
            continue;
        }
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            close(sockfd);
            //perror("client: connect");
            continue;
        }
        break;
    }
    if(p == NULL){
        //fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);

    //printf("client: connecting to %s\n", s);
    freeaddrinfo(servinfo);
    if(send(sockfd, message, msize, 0) == -1){
        perror("send");
    }
    //send content if necessary
    if(mthd == POST){
        tempfd = open(TEMP_PATH, 0);
        while(1){
            numbytes = read(tempfd, buf, CHUNKSIZE);
            if(numbytes == 0) break;
            if(send(sockfd, buf, numbytes, 0) == -1){
                perror("sendcontent");
            }
        }
    }

    int clength, cidx;
    int total_recieved = 0;
    int remainder;

    //get content length in first recv, then loop
    numbytes = recv(sockfd, buf, CHUNKSIZE, 0);

    buf[numbytes] = '\0';
    
    parse_http_response(buf, numbytes, &clength, &cidx);

    remainder = numbytes - cidx;
    if(remainder > 0){
        write(STDOUT_FILENO, buf+cidx, remainder);
    }

    while(total_recieved < clength){
        numbytes = recv(sockfd, buf, CHUNKSIZE, 0);
        if(numbytes == 0) break; //end of data
        write(STDOUT_FILENO, buf, numbytes);
        total_recieved += numbytes;
    }
    close(sockfd);

    remove(TEMP_PATH);

    return 0;
}
