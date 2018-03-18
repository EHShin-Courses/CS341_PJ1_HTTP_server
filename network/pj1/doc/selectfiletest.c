#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
int main(){
    int fd;
    char buf[1024];
    int nbytes, nready;
    fd_set master, readfds;
    FD_ZERO(&master);
    FD_ZERO(&readfds);
    
    fd = open("./ex.txt", 0);
    FD_SET(fd, &master);
    
    readfds = master;
    nready = select(fd+1, &readfds, NULL, NULL, NULL);
    printf("first n ready:%d\n", nready);
    if(FD_ISSET(fd, &readfds)){
        printf("read fds set\n");
    }
    else{
        printf("read fds not set\n");
    }

    readfds = master;
    nready = select(fd+1, &readfds, NULL, NULL, NULL);
    printf("second n ready:%d\n", nready);
    if(FD_ISSET(fd, &readfds)){
        printf("read fds set\n");
    }
    else{
        printf("read fds not set\n");
    }

    nbytes = read(fd, buf, 1023);
    buf[nbytes] = '\0';
    printf("reading content:\n%s\n", buf);

    readfds = master;
    nready = select(fd+1, &readfds, NULL, NULL, NULL);
    printf("third n ready:%d\n", nready);
    if(FD_ISSET(fd, &readfds)){
        printf("read fds set\n");
    }
    else{
        printf("read fds not set\n");
    }
    
    nbytes = read(fd, buf, 1023);
    if(nbytes == 0)
        printf("0 bytes read\n");

    readfds = master;
    nready = select(fd+1, &readfds, NULL, NULL, NULL);
    printf("fourth n ready:%d\n", nready);
    if(FD_ISSET(fd, &readfds)){
        printf("read fds set\n");
    }
    else{
        printf("read fds not set\n");
    }
 
    return 0;
}
