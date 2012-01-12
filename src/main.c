/*
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *
 *  main() code for chirc project
 *
 * sachs_sandler
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
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include "reply.h"
#include "simclist.h"
#include "ircstructs.h"

#define HOSTNAMELEN 30
#define NLOOPS 1000000

#ifdef MUTEX
pthread_mutex_t lock;
#endif

struct serverArgs
{
	char *port;
	char *passwd;
};

struct workerArgs
{
	int socket;
};

void parse_message(int clientSocket);
void *accept_clients(void *args);
void *service_single_client(void *args);

list_t userlist, chanlist;

int main(int argc, char *argv[])
{
	list_init(& userlist);
	list_init(& chanlist);
	
	int opt;
	char *port = "6667", *passwd = NULL;
    struct serverArgs *sa;

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
	
	pthread_t server_thread;

	sigset_t new;
	sigemptyset (&new);
	sigaddset(&new, SIGPIPE);
	if (pthread_sigmask(SIG_BLOCK, &new, NULL) != 0) 
	{
		perror("Unable to mask SIGPIPE");
		exit(-1);
	}

	#ifdef MUTEX
	pthread_mutex_init(&lock, NULL);
	#endif

	sa = malloc(sizeof(struct serverArgs));
	sa->port = port;
	sa->passwd = passwd;
	
	if (pthread_create(&server_thread, NULL, accept_clients, sa) < 0)
	{
		perror("Could not create server thread");
		exit(-1);
	}

	pthread_join(server_thread, NULL);

	#ifdef MUTEX
	pthread_mutex_destroy(&lock);
	#endif

	pthread_exit(NULL);
}

void *accept_clients(void *args)
{
	struct serverArgs *sa;
	char *port, *passwd;
	
	sa = (struct serverArgs*) args;
	port = sa->port;
	passwd = sa->passwd;
	
	int serverSocket;
	int clientSocket;
	pthread_t worker_thread;
	struct addrinfo hints, *res, *p;
	struct sockaddr_in clientAddr;
	socklen_t sinSize = sizeof(struct sockaddr_storage);
	struct workerArgs *wa;
	int yes = 1;
	
	char hostname[HOSTNAMELEN];
    person client = {NULL, NULL, NULL, NULL};

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(NULL, port, &hints, &res) != 0)
	{
		perror("getaddrinfo() failed");
		pthread_exit(NULL);
	}

	for(p = res;p != NULL; p = p->ai_next) 
	{
		if ((serverSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) 
		{
			perror("Could not open socket");
			continue;
		}

		if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
		{
			perror("Socket setsockopt() failed");
			close(serverSocket);
			continue;
		}

		if (bind(serverSocket, p->ai_addr, p->ai_addrlen) == -1)
		{
			perror("Socket bind() failed");
			close(serverSocket);
			continue;
		}

		if (listen(serverSocket, 5) == -1)
		{
			perror("Socket listen() failed");
			close(serverSocket);
			continue;
		}

		break;
	}

	freeaddrinfo(res);

	if (p == NULL)
	{
    	fprintf(stderr, "Could not find a socket to bind to.\n");
		pthread_exit(NULL);
	}

	while (1)
	{
		if ((clientSocket = accept(serverSocket, (struct sockaddr *) &clientAddr, &sinSize)) == -1) 
		{
			perror("Could not accept() connection");
			continue;
		}
		
		/* eventually all this will go in a separate function called by pthread, along with associated variables */ 
    	if (getnameinfo((struct sockaddr *) &clientAddr, sizeof(struct sockaddr), hostname, HOSTNAMELEN, NULL, 0, 0) != 0)
    	{
        	perror("getnameinfo failed");
        	close(clientSocket);
        	close(serverSocket);
        	pthread_exit(NULL);
    	}
    	client.address = hostname;
    	list_append(&userlist, &client);

		wa = malloc(sizeof(struct workerArgs));
		wa->socket = clientSocket;

		if (pthread_create(&worker_thread, NULL, service_single_client, wa) != 0) 
		{
			perror("Could not create a worker thread");
			free(wa);
			close(clientSocket);
			close(serverSocket);
			pthread_exit(NULL);
		}
	}

	pthread_exit(NULL);
}

void *service_single_client(void *args) {
	struct workerArgs *wa;
	int socket, nbytes, i;
	char buffer[100];

	wa = (struct workerArgs*) args;
	socket = wa->socket;

	pthread_detach(pthread_self());
	
	parse_message(socket);
	
	/*
	while(1)
	{
		nbytes = recv(socket, buffer, sizeof(buffer), 0);
		if (nbytes == 0)
			break;
		else if (nbytes == -1)
		{
			perror("Socket recv() failed");
			close(socket);
			pthread_exit(NULL);
		}
		   Ignore anything that's actually recv'd. We just want
		   to keep the connection open until the client disconnects
	}
	*/
	
	close(socket);
	pthread_exit(NULL);
}
