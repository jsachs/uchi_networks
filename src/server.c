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

extern pthread_mutex_t lock;

void parse_message(int clientSocket, chirc_server *server);
int fun_seek(const void *el, const void *indicator);
int fun_compare(const void *a, const void *b);

void *service_single_client(void *args) {
	
	workerArgs *wa;
	int socket;
	chirc_server *ourserver;
    char *clientname;
    list_t userchans;
    person client;
    client.nick[0] = '\0';
    client.user[0] = '\0';
    client.fullname[0] = '\0';
    client.mode[0] = '\0';
    pthread_mutex_init(&(client.c_lock), NULL);
    
    //unpack arguments
	wa = (workerArgs*) args;
	socket = wa->socket;
	ourserver = wa->server;
    
    //set up client struct
    list_init(&userchans);
    if(list_attributes_seeker(&userchans, fun_seek) == -1){
        perror("list fail");
        exit(-1);
    }
    if(list_attributes_comparator(&userchans, fun_compare) == -1){
        perror("list fail");
        exit(-1);
    }
    clientname = wa->clientname;
    client.clientSocket = socket;
    client.address = clientname;
    client.my_chans = &userchans;
    client.tid = pthread_self();
    
    free(wa);

    //add client to list
    pthread_mutex_lock(&lock);
    list_append(ourserver->userlist, &client);
    pthread_mutex_unlock(&lock);

	pthread_detach(pthread_self());

    //actually get messages
	parse_message(socket, ourserver);
	
	pthread_mutex_destroy(&(client.c_lock));

	close(socket);
	pthread_exit(NULL);
}

