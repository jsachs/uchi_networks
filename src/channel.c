/*
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *
 *  channel code for chirc project
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
extern pthread_mutex_t loglock;

void constr_reply(char code[4], person *client, char *reply, chirc_server *server, char *extra);

void channel_join(person *client, chirc_server *server, char *channame){
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    int i;
    char *replies[2] = {RPL_NAMREPLY,
    					RPL_ENDOFNAMES
    };
    snprintf(reply, MAXMSG-1, ":%s!%s@%s JOIN %s", client->nick, client->user, client->address, params[1]);
    strcat(reply, "\r\n");
    pthread_mutex_lock(&(client->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        close(clientSocket);
        pthread_mutex_lock(&lock);
        list_delete(server->userlist, user);
        pthread_mutex_unlock(&lock);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&(client->c_lock));
    
    for (i = 0; i < 2; i++){
        constr_reply(replies[i], client, reply , server, NULL);
        pthread_mutex_lock(&(client->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, client);
            pthread_mutex_unlock(&lock);
            free(client->address);
            free(client);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(client->c_lock));
    }
	// check if the channel exists
	// if not, make it
	// if it does, add to it
}