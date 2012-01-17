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

void *service_single_client(void *args) {
	struct workerArgs *wa;
	int socket, nbytes, i;
	chirc_server *ourserver;
	char buffer[100];
    
	wa = (struct workerArgs*) args;
	socket = wa->socket;
	ourserver = wa->server;
    
	pthread_detach(pthread_self());
	
	//parse_message(socket);
	
	close(socket);
	pthread_exit(NULL);
}
