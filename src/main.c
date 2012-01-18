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
<<<<<<< HEAD
    //struct serverArgs *sa;

=======
    serverArgs *sa;
    time_t birthday = time(NULL);
    
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
	if(list_attributes_seeker(&userlist, fun_seek) == -1){
		perror("list fail");
		exit(-1);
	}
    
	while ((opt = getopt(argc, argv, "p:o:h")) != -1)
		switch (opt)
<<<<<<< HEAD
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
=======
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
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc

    
	if (!passwd)
	{
		fprintf(stderr, "ERROR: You must specify an operator password\n");
		exit(-1);
	}
<<<<<<< HEAD
	
    ourserver->pw = passwd;
=======
    
    /*initialize chirc_server struct*/
    ourserver = malloc(sizeof(chirc_server));
    ourserver->userlist = &userlist;
    ourserver->chanlist = &chanlist;
    ourserver->port = port;
    ourserver->pw = passwd;
    ourserver->version = "chirc-0.1";
    ourserver->birthday = ctime(&birthday);
<<<<<<< HEAD
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
=======
    ourserver->birthday[strlen(ourserver->birthday) - 1] = '\0';
>>>>>>> c68f4865da866a9663d61dc4e4f9eea759a20703
    
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
<<<<<<< HEAD
    /*
	sa = malloc(sizeof(struct serverArgs));
	sa->port = port;
	sa->passwd = passwd;
	*/
	if (pthread_create(&server_thread, NULL, accept_clients, NULL/*, sa*/) < 0)
=======
    
    sa = malloc(sizeof(serverArgs));
    sa->server = ourserver;
    
	if (pthread_create(&server_thread, NULL, accept_clients, sa) < 0)
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
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
<<<<<<< HEAD
	/*
    struct serverArgs *sa;
	char *port, *passwd, *servname;
	
	sa = (struct serverArgs*) args;
	port = sa->port;
	passwd = sa->passwd;
	*/
=======
	#ifdef MUTEX
	pthread_mutex_t s_lock;
	pthread_mutex_init(&s_lock)
	#endif
	
    serverArgs *sa;
    chirc_server *ourserver;
     
    sa = (serverArgs*) args;
    ourserver = sa->server;

>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
    char *servname;
    char *port = ourserver->port;
	int serverSocket;
	int clientSocket;
	pthread_t worker_thread;
	struct addrinfo hints, *res, *p;
	struct sockaddr_in clientAddr;
	socklen_t sinSize = sizeof(struct sockaddr_storage);
	workerArgs *wa;
	int yes = 1;
	
	char hostname[HOSTNAMELEN];
    
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE|AI_CANONNAME;
<<<<<<< HEAD

=======
    
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
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
<<<<<<< HEAD

=======
        
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
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
    
<<<<<<< HEAD

=======
    
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
    
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
		
		/* determine name of client */
    	if (getnameinfo((struct sockaddr *) &clientAddr, sizeof(struct sockaddr), hostname, HOSTNAMELEN, NULL, 0, 0) != 0)
    	{
        	perror("getnameinfo failed");
        	close(clientSocket);
        	close(serverSocket);
        	pthread_exit(NULL);
    	}
        
		wa = malloc(sizeof(workerArgs));
		wa->server = ourserver;
        wa->clientname = malloc(strlen(hostname) + 1);;
        strcpy(wa->clientname, hostname);
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


