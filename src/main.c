/*
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *
 *  main() code for chirc project
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "reply.h"
#include "simclist.h"
#include "ircstructs.h"

#define HOSTNAMELEN 30


void parse_message(int clientSocket, int serverSocket);

list_t userlist, chanlist;

int main(int argc, char *argv[])
{
	/* Initialize lists of nicks/users and channels */
	list_init(& userlist);
	list_init(& chanlist);
	
	int opt;
	char *port = "6667", *passwd = NULL;
    char hostname[HOSTNAMELEN];
    person client = {NULL, NULL, NULL, NULL};

	while ((opt = getopt(argc, argv, "p:o:h")) != -1)
		switch (opt)
		{
			case 'p':
				port = strdup(optarg);
				break;
			case 'o':
				passwd = strdup(optarg);
				break;
			default:
				printf("ERROR: Unknown option -%c\n", opt);
				exit(-1);
		}

	if (!passwd)
	{
		fprintf(stderr, "ERROR: You must specify an operator password\n");
		exit(-1);
	}
	
	/* Initialize all the crap for sockets */

	int serverSocket;  // Used to listen for connections
	int clientSocket;  // Used to communicate with one specific client
	
	struct sockaddr_in serverAddr, clientAddr;       // These will hold information for server and client sockets
	
	int yes = 1;       // Used to call setsockopt()
	
	socklen_t sinSize = sizeof(struct sockaddr_in);  // Used to call accept()

	memset(&serverAddr, 0, sizeof(serverAddr)); // ensures sin_zero is all zeros
	
	serverAddr.sin_family = AF_INET;          // IPv4
	serverAddr.sin_port = htons(atoi(port));  // TCP port number for IRC
	serverAddr.sin_addr.s_addr = INADDR_ANY;  // Bind to any address

	/* Creates the socket */
	serverSocket = socket(PF_INET,      // Family: IPv$
						  SOCK_STREAM,  // Type: Full-duplex, Reliable
						  IPPROTO_TCP); // Protocol: TCP
	
	/* Ensure socket was created correctly */
	if(serverSocket == -1)
	{
		perror("Could not open socket");
		close(serverSocket);
		exit(-1);
	}
	
	/* Make the address reusable */					  
	if(setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
	{
		perror("Socket setsockopt() failed");
		close(serverSocket);
		exit(-1);
	}
	
	/* Bind the socket to the address */
	if(bind(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == -1)
	{
		perror("Socket bind() failed");
		close(serverSocket);
		exit(-1);
	}
	
	/* Begin listening */
	if(listen(serverSocket, 5) == -1)
	{
		perror("Socket listen() failed");
		close(serverSocket);
		exit(-1);
	}

	
	/* Begin the loop to run the server */
	while(1)
	{
		/* When an incoming connection arrives, accept it.
		   The call to accept() blocks until the incoming connection arrives */
		if((clientSocket = accept(serverSocket, (struct sockaddr *) &clientAddr, &sinSize)) == -1)
		{
			perror("Socket accept() failed");
			close(serverSocket);
			exit(-1);
		}

        /* eventually all this will go in a separate function called by pthread, along with associated variables */ 
        if (getnameinfo((struct sockaddr *) &clientAddr, sizeof(struct sockaddr), hostname, HOSTNAMELEN, NULL, 0, 0) != 0){
            perror("getnameinfo failed");
            close(clientSocket);
            close(serverSocket);
            exit(-1);
        }
        client.address = hostname;
        list_append(&userlist, &client);
        
        //and we'll need to add person to our userlist; we'll add nick, user, etc later as appropriate

		parse_message(clientSocket, serverSocket);
	}

	close(serverSocket);
    
	return 0;
}

