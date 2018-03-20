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

/* for storing http request parse state */
struct parse_state{
    char line_buffer[64]; //buffer for single header line
    int lb_idx; //buffer index of next byte
    char * path;
    int found_cl;
};

/* one for each client connection */
struct client_session{
    int initial;
    int sock_fd;
    int file_fd;
    enum method mthd;
    long objsize;
    long cursize;
    int do_response;
    enum resptype rtype;
    struct parse_state * ps;
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


int parse_atmt(char * data, int recvbytes, struct client_session * cs){

    int data_idx;
    char c;
    char * lb;
    struct parse_state * ps;
    int header_end;
    int i;
    int path_idx;
    int path_len;
    char * uu;
    int cidx;

    if(cs->ps == NULL){
        //initial bytes from client
        cs->ps = (struct parse_state *)malloc(sizeof(struct parse_state));
        if(cs->ps == NULL){
            perror("malloc fail");
            return -1;
        }
        cs->ps->lb_idx = 0;
        cs->ps->path = NULL;
        cs->ps->found_cl = 0;
    }

    ps = cs->ps;
    lb = cs->ps->line_buffer;

    for(data_idx = 0; data_idx < recvbytes; data_idx++){
        //read byte by byte
        c = data[data_idx];
        lb[ps->lb_idx] = c;
        if(c == '\n' && ps->lb_idx > 0){
            if(lb[ps->lb_idx-1] == '\r'){
                //end of line
                if(ps->lb_idx == 1){
                    //end of header
                    cs->rtype = OK; //for now
                    header_end = 1;
                    break;
                }
                if(cs->ps->path == NULL){
                    //this must be end of first line
                    
                    //extract method
                    if(strncmp(lb, "GET", 3) == 0){
                        cs->mthd = GET;
                    }
                    else if(strncmp(lb, "POST", 4) == 0){
                        cs->mthd = POST;
                    } 
                    else{
                        cs->rtype = BADREQ;
                        header_end = 1;
                        break;
                    }

                    //extract path
                    path_idx = -1;
                    for(i=0; i <= ps->lb_idx; i++){
                        if(lb[i]==' '){
                            if(path_idx == -1){
                                lb[i]='.';
                                path_idx = i;
                            }else{
                                path_len = i - path_idx;
                                break;
                            }
                        }
                    }
                    if(path_idx == -1){
                        cs->rtype = BADREQ;
                        header_end = 1;
                        break;
                    }
                    else{
                        ps->path = (char *)malloc(path_len+1);
                        memcpy(ps->path, lb+path_idx, path_len);
                        (ps->path)[path_len] = '\0';
                    }
                }
                else if(cs->mthd == POST && ps->found_cl == 0){
                    //look for content-length header
                    if(strncmp(lb, "Content-Length: ", 16)==0){
                        cs->objsize = strtol(lb+16, &uu, 10);
                        ps->found_cl = 1;
                    }
                }
                ps->lb_idx = -1; //reset line buffer
            }
        }
        (ps->lb_idx)++;
    }
    if(header_end){
        cs->initial = 0;
        parse_tail(cs, data, data_idx, recvbytes - data_idx -1);
    }
    //header is not yet finished
    return 0;
}

/* called when request header parsing is complete */
int parse_tail(struct client_session * cs, char * data, int cidx, int remainder){

    struct stat file_stat; 
    // check additional bad request conditions
    if(cs->rtype != BADREQ){
        if(cs->mthd == POST && cs->ps->found_cl == 0){
            cs->rtype = BADREQ;
        }
    }
    if(cs->rtype == BADREQ){
        cs->file_fd = -1;
        cs->do_response = 1;
        return 0;
    }
    else{
        // Open associated file
        if(cs->mthd == GET){
            // GET : open readable
            cs->file_fd = open(cs->ps->path, 0, O_RDONLY);
            if(cs->file_fd < 0){
                cs->rtype = NOTFOUND;
                cs->do_response = 1;
                return 0;
            }
            //get file size
            fstat(cs->file_fd, &file_stat);
            cs->objsize = file_stat.st_size;
            if(S_ISDIR(file_stat.st_mode)){
                //don't give directory
                cs->rtype = BADREQ;
                close(cs->file_fd);
                cs->file_fd = -1;
                cs->do_response = 1;
                return 0;
            }
        }
        else{
            // POST : create writable
            cs->file_fd  = creat(cs->ps->path, 0777);
            if(cs->file_fd < 0){
                cs->rtype = BADREQ;
                cs->do_response = 1;
                return 0;
            }
            //write remaining data
            if(remainder > 0){
                printf("there is remainder!\n");
                write(cs->file_fd, data+cidx, remainder);
            }
        }
    }
    if(cs->mthd == GET){
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
    cs->ps = NULL;
    cs->next = cs_list;
    cs_list = cs;
    return cs;
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
        nbytes = recv(cs->sock_fd, buf, CHUNKSIZE, 0);
        if(nbytes < 0){
            if(errno != EWOULDBLOCK){
                perror("intial recv");
            }
            return 0;
        }
        parse_atmt(buf, nbytes, cs); 
        
        // read and parse header
        // fields to setup:
        // file_fd, mthd, objsize, do_response, rtype
        if(cs->initial == 0 && cs->file_fd != -1){
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

                    if(nbytes < 0){
                      if(errno != EWOULDBLOCK){
                        perror("post recv");
                      }
                      return 0;
                    } 

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
                    nbytes = send(cs->sock_fd, message, msize, 0);
         
                    if(nbytes < 0){
                        if(errno != EWOULDBLOCK){
                            perror("send response");
                        }
                        return 0;
                    } 
         
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
                    if(nbytes < 0){
                         if(errno != EWOULDBLOCK){
                            perror("get send");
                         }
                         return 0;
                    } 

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
        int flags = fcntl(listener, F_GETFL, 0);
        int rc = fcntl(listener, F_SETFL, flags|O_NONBLOCK);
        if(rc<0){
            close(listener);
            continue;
        }
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
            do{
                newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);     
                if(newfd < 0){
                    if(errno != EWOULDBLOCK){
                        perror("accept() fail");
                        exit(-1);
                    }
                    break;
                }
                FD_SET(newfd, &master);
                if(newfd > fdmax){
                   fdmax = newfd;
                }
                add_session(newfd);
            }while(newfd >= 0);
        }

        //traverse client sessions
        for(cs = cs_list; cs != NULL; cs = nextcs){
            nextcs = cs->next; // in case of cs deletion
            handle_session(cs, &master, &read_fds, &write_fds, &fdmax);
        }
    }
    return 0;
}
