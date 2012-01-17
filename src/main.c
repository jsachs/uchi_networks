/*
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *
 *  main() code for chirc project
 *
 *  sachs_sandler
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

void parse_message(int clientSocket);
void *accept_clients(void *args);
void *service_single_client(void *args);
int fun_seek(const void *el, const void *indicator);

list_t userlist, chanlist;
chirc_server *ourserver;

int main(int argc, char *argv[])
{
	list_init(& userlist);
	list_init(& chanlist);
    ourserver = malloc(sizeof(chirc_server));
    ourserver->userlist = &userlist;
    ourserver->chanlist = &chanlist;
	
	int opt;
	char *port = "6667", *passwd = NULL;
    //struct serverArgs *sa;

	if(list_attributes_seeker(&userlist, fun_seek) == -1){
		perror("list fail");
		exit(-1);
	}

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
    
    ourserver->port = port;

	if (!passwd)
	{
		fprintf(stderr, "ERROR: You must specify an operator password\n");
		exit(-1);
	}
	
    ourserver->pw = passwd;
    
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
    /*
	sa = malloc(sizeof(struct serverArgs));
	sa->port = port;
	sa->passwd = passwd;
	*/
	if (pthread_create(&server_thread, NULL, accept_clients, NULL/*, sa*/) < 0)
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
	/*
    struct serverArgs *sa;
	char *port, *passwd, *servname;
	
	sa = (struct serverArgs*) args;
	port = sa->port;
	passwd = sa->passwd;
	*/
    char *servname;
    char *port = ourserver->port;
	int serverSocket;
	int clientSocket;
	pthread_t worker_thread;
	struct addrinfo hints, *res, *p;
	struct sockaddr_in clientAddr;
	socklen_t sinSize = sizeof(struct sockaddr_storage);
	struct workerArgs *wa;
	int yes = 1;
	
	char hostname[HOSTNAMELEN];
    person client = {-1, NULL, NULL, NULL, NULL};

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE|AI_CANONNAME;

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

        servname = res->ai_canonname;
        ourserver->servername = malloc(strlen(servname));
        strcpy(ourserver->servername, servname);
        
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
    	client.fd = clientSocket;
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
	
	close(socket);
	pthread_exit(NULL);
}

int fun_seek(const void *el, const void *indicator){
    if (el == NULL || indicator == NULL){
        perror("bad argument to fun_seek");
        return 0;
    }
    person *client = (person *)el;
    el_indicator *el_info;
    el_info = (el_indicator *)indicator;
    int field = el_info->field;
    char *value;
    int fd;
    if (field == 4)
        fd = el_info->fd;
    else
        value = el_info->value;
    if (value == NULL || field < 0 || field > 4){
        perror("bad argument to fun_seek");
        return 0;
    }
    switch (field) {
        case 0:
            return (client->nick == value)?1:0;
            break;
        case 1:
            return (client->user == value)?1:0;
            break;
        case 2:
            return (client->fullname == value)?1:0;
            break;
        case 3:
            return (client->address == value)?1:0;
            break;
        case 4:
            return (client->fd == fd)?1:0;
        default:
            return 0;
            break;
    }
}
