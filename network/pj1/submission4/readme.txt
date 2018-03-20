
Project 1 : Socket Programming

20150433 Eunhyouk Shin


< A Simple Non-Blocking HTTP1.0 POST / GET Server and Client >



Usage:

use 'make' to compile client and server

server:
    ./server -P [port]

client - get:
    ./client -G [host]:[port]/[path] > [output file]

client - post:
    ./client -P [host]:[port]/[path] < [input file]



- If server gets an inappropriate request: ex) POST with no Content-Length, GET directory
its response is '400 Bad Request'
If server cannot find file for GET request, its response is '404 Not Found'
Otherwise, server's response is '200 OK'


-Server can handle multiple clients simultaneously by non blocking I/O.
i.e. If the recv buffer is empty or send buffer is full, the server deals with
other workload. It can use select() to check which socket can be processed


-Each client connection will have 'struct client_session' which contains essential data
about the connection such as file descriptor of file being POST or GET.


-Both server and client can deal with arbitrary data size with scalability.
To know the input size from stdin, the client makes a temporary file
to measure the data size.


-Scalability is achieved by splitting data into CHUNKSIZE, thereby not exhausting the
heap data section. 


