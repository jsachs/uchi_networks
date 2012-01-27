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

void channel_join(person *client, chirc_server *server){
    int i;
    int clientSocket = client->clientSocket;
    char reply[MAXMSG];
    char *replies[4] = {RPL_TOPIC,
                        RPL_NAMEREPLY,
    };
    pthread_mutex_lock(&lock);
    (server->numregistered)++;
    pthread_mutex_unlock(&lock);
    
    for (i = 0; i < 4; i++){
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
    
    chirc_handle_LUSERS(server, client, NULL);
    chirc_handle_MOTD(server, client, NULL);
}