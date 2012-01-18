/*
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *
 *  server code for chirc project
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


#define MAXMSG 512

void parse_message(int clientSocket, chirc_server *server);

void *service_single_client(void *args) {
	workerArgs *wa;
	int socket, nbytes, i;
	chirc_server *ourserver;
	char buffer[100];
    char *clientname;
    person client;
    client.nick[0] = '\0';
    client.user[0] = '\0';
    client.fullname[0] = '\0';
    
    
	wa = (workerArgs*) args;
	socket = wa->socket;
	ourserver = wa->server;
    clientname = wa->clientname;
    client.clientSocket = socket;
    client.address = clientname;
    list_append(ourserver->userlist, &client);

    
	pthread_detach(pthread_self());

	parse_message(socket, ourserver);

	close(socket);
	pthread_exit(NULL);
}

