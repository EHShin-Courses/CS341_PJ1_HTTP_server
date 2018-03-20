#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>

#define CHUNKSIZE 1024

enum method {GET, POST};
enum resptype {OK, BADREQ, NOTFOUND};

struct client_session{
    int initial;
    int sock_fd;
    int file_fd;
    enum method mthd;
    long objsize;
    long cursize;
    int do_response;
    enum resptype rtype;
    struct client_session * next;
};

struct client_session * cs_list;

int build_http_response(char ** message, int *msize, struct client_session * cs){
    
    char * buffer = (char *)malloc(128); // should be enough
    long int clength;
    if(cs->mthd == POST)
        clength = 0;
    else
        clength = cs->objsize;
    if(cs->rtype == OK){
        sprintf(buffer,
                "HTTP/1.0 200 OK\r\n"
                "Content-Length: %ld\r\n"
                "Connection: close\r\n"
                "\r\n",
                clength);
    }else if(cs->rtype == BADREQ){
        sprintf(buffer,
                "HTTP/1.0 400 Bad Request\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n");
    }else{
        sprintf(buffer,
                "HTTP/1.0 404 Not Found\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n");
    }
    *msize = strlen(buffer);
    *message = buffer;

    return 0;
}



int parse_http_request(char * buf, int nbytes, struct client_session * cs){
    int idx = 0;
    int newLine = 1;
    int clidx = -1;
    int cidx, clength;
    char * uu;
    int pathidx = -1;
    int in_path = 0;
    struct stat file_stat;
    int remainder;
    cs->rtype = OK; //for now
    //get method
    if(strncmp(buf, "GET", 3) == 0){
        cs->mthd = GET;
    }
    else if(strncmp(buf, "POST", 4) == 0){
        cs->mthd = POST;
    }
    while(idx < nbytes){
        //look for path
        if(pathidx == -1 || in_path){
            if(buf[idx] == ' '){
                if(in_path){
                    buf[idx] = '\0';
                    in_path = 0;
                }
                else{
                    buf[idx] = '.';
                    pathidx = idx;
                    in_path = 1;
                }
            }
        }
        if(newLine){
            newLine = 0;
            //look for header end
            if(strncmp(buf+idx, "\r\n", 2) == 0){
                cidx = idx + 2;
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
    if(clidx == -1 && cs->mthd == POST){
        cs->rtype = BADREQ;
    }
    else{
        cs->objsize = strtol(buf+clidx, &uu, 10);
        //printf("clength:%ld\n", cs->objsize);
    }
    

    if(pathidx == -1){
        cs->rtype = BADREQ;
        cs->file_fd = -1;
    }
    else{
        //Open Associated File
        if(cs->mthd == GET){
            // GET : open readable
            cs->file_fd = open(buf+pathidx, 0, O_RDONLY);
            if(cs->file_fd < 0){
                cs->rtype = NOTFOUND;
                cs->file_fd = -1;
            }
            else{
                //get file size
                fstat(cs->file_fd, &file_stat);
                cs->objsize = file_stat.st_size;
                if(S_ISDIR(file_stat.st_mode)){
                    cs->rtype = BADREQ;
                    close(cs->file_fd);
                    cs->file_fd = -1;
                }
            }
            //printf("req path:[%s]\n", buf+pathidx);
        }
        else{
            // POST : create writable
            cs->file_fd = creat(buf+pathidx, 0777);
            if(S_ISDIR(file_stat.st_mode)){
                cs->rtype = BADREQ;
                close(cs->file_fd);
                cs->file_fd = -1;
            }
           if(cs->file_fd < 0){
                cs->rtype = BADREQ;
                cs->file_fd = -1;
           }
           //write data immediately after header  
           remainder = nbytes - cidx;
           if(remainder > 0 && cs->file_fd > 0){
               write(cs->file_fd, buf+cidx, nbytes); 
           }
        }
    }

    if(cs->mthd == GET || cs->rtype != OK){
        //printf("BADRESP/NOTFOUND!\n");
        cs->do_response = 1;
    }

    return 0;
}

struct client_session * add_session(int sock_fd){
    struct client_session * cs;
    cs = (struct client_session *)malloc(sizeof(struct client_session));
    cs->initial = 1;
    cs->sock_fd = sock_fd;
    cs->cursize = 0;
    cs->do_response = 0;
    cs->next = cs_list;
    cs_list = cs;
    return cs;
}

struct client_session * find_session(int fd){
    struct client_session * cs;
    for(cs = cs_list; cs != NULL; cs = cs->next){
        if(cs->file_fd == fd || cs->sock_fd == fd)
            return cs;
    }
    return NULL;
}

int delete_session(struct client_session * cs, fd_set * masterp){
    // free all resources associated with a client session, and remove from list
    FD_CLR(cs->sock_fd, masterp);
    close(cs->sock_fd);
    if(cs->file_fd != -1){
        close(cs->file_fd);
        FD_CLR(cs->file_fd, masterp);
    }

    struct client_session * prev;
    //list pointers...
    if(cs_list == cs)
        cs_list = cs->next;
    else{
        for(prev = cs_list; prev != NULL; prev = prev->next){
            if(prev->next == cs)
                break;
        }
        prev->next = cs->next;
    }
    free(cs);
    return 0;
}

int handle_session(struct client_session * cs, fd_set *masterp, fd_set *read_fdsp, fd_set *write_fdsp, int * max_fd){
    char buf[CHUNKSIZE];
    int remsize;
    int nbytes;
    int msize;
    char * message;
    
    if(cs->initial){
        cs->initial = 0;
        nbytes = recv(cs->sock_fd, buf, CHUNKSIZE, 0);
        
        // read and parse header
        // fields to setup:
        // file_fd, mthd, objsize, do_response, rtype
        parse_http_request(buf, nbytes, cs);
        if(cs->file_fd != -1){
            if(cs->rtype == OK){
                FD_SET(cs->file_fd, masterp);
                if(cs->file_fd > *max_fd)
                   *max_fd = cs->file_fd;
            }
        }

    }else{
        if(cs->mthd == POST && (!(cs->do_response))){
            // must recieve sth first
            if(FD_ISSET(cs->sock_fd, read_fdsp)){
                if(FD_ISSET(cs->file_fd, write_fdsp)){
                    // socket -> file ready
                    remsize = cs->objsize - cs->cursize;
                    if(remsize > CHUNKSIZE)
                        remsize = CHUNKSIZE;
                    nbytes = recv(cs->sock_fd, buf, remsize, 0);                
                    if(nbytes == 0){
                    // recv 0 bytes means disconnection
                        delete_session(cs, masterp);
                        return 1;
                    }
                    write(cs->file_fd, buf, nbytes);
                    cs->cursize += nbytes;
                }
            }
        }else{
            // must send sth first
            if(FD_ISSET(cs->sock_fd, write_fdsp)){
                if(cs->do_response){
                    //printf("will do response...\n");         
                    build_http_response(&message, &msize, cs);
                    send(cs->sock_fd, message, msize, 0);
                    free(message);
                    if(cs->mthd == POST || cs->rtype != OK){
                        //session finished
                        delete_session(cs, masterp); 
                    }
                    else{
                        // new send data for GET
                        cs->do_response = 0;
                    }
                }
                else if(FD_ISSET(cs->file_fd, read_fdsp)){
                    // file -> socket ready
                    
                    //send content
                    remsize = cs->objsize - cs->cursize;
                    if(remsize > CHUNKSIZE)
                        remsize = CHUNKSIZE;
                    nbytes = read(cs->file_fd, buf, remsize);
                    if(nbytes != remsize){
                        printf("filesize mismatch!\n");
                        return 1;
                    }
                    nbytes = send(cs->sock_fd, buf, remsize, 0);
                    cs->cursize += nbytes;
                    if(nbytes == 0){
                    // send 0 bytes means disconnection
                        delete_session(cs, masterp);
                        return 1;
                    }
                }
            }
        }
    }
    if((!(cs->do_response)) && cs->objsize == cs->cursize){
        if(cs->mthd == POST){
            //finished receiving POST content
            //send 200 OK
            cs->rtype = OK;
            cs->do_response = 1;
        }
        else{
            //finished sending GET content
            delete_session(cs, masterp);
        }
    }
    return 0;
}

void *get_in_addr(struct sockaddr *sa){
    if(sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int setup_listener(const char * port){

    struct addrinfo hints, *ai, *p;
    int rv, listener;
    int yes=1;

    //setup listener
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if((rv = getaddrinfo(NULL, port, &hints, &ai)) != 0){
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }
    for(p = ai; p != NULL; p = p->ai_next){
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol); 
        if(listener < 0) continue;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if(bind(listener, p->ai_addr, p->ai_addrlen)<0){
            close(listener);
            continue;
        }
        break;
    }
    if(p==NULL){
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }
    freeaddrinfo(ai);
    if(listen(listener, 10) == -1){
        perror("listen");
        exit(3);
    }

    return listener;
}


int main(int argc, char ** argv){

    fd_set master;
    fd_set read_fds;
    fd_set write_fds;

    int fdmax;
    int listener;
    int newfd;
    struct sockaddr_storage remoteaddr;
    socklen_t addrlen;
    char buf[256];
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int i;

    struct client_session * cs;
    struct client_session * nextcs;


    //initialization
    cs_list = NULL;
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    listener = setup_listener(argv[2]);
    FD_SET(listener, &master);
    fdmax = listener;

    //main loop
    for(;;){
        //copy set
        read_fds = master;
        write_fds = master;

        if(select(fdmax+1, &read_fds, &write_fds, NULL, NULL) == -1){
            perror("select");
            exit(4);
        }

        //check listener
        if(FD_ISSET(listener, &read_fds)){
            //new client
            addrlen = sizeof remoteaddr;
            newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);     
            if(newfd == -1){
               perror("accept");
            }else{
               FD_SET(newfd, &master);
               if(newfd > fdmax){
                   fdmax = newfd;
               }
               add_session(newfd);
            }

        }

        //traverse client sessions
        for(cs = cs_list; cs != NULL; cs = nextcs){
            nextcs = cs->next; // in case of cs deletion
            handle_session(cs, &master, &read_fds, &write_fds, &fdmax);
        }
    }
    return 0;
}
