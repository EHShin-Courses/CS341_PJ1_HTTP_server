all: server client

server: server.c
	gcc -g server.c -o server

client: client.c
	gcc -g client.c -o client

testclient: testclient.c
	gcc -g testclient.c -o testclient

clean:
	rm -f client server
