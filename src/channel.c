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
void sendtochannel(chirc_server *server, channel *chan, char *msg, char *sender);
int fun_seek(const void *el, const void *indicator);

void channel_join(person *client, chirc_server *server, char* channel_name){
    int i;
    int clientSocket = client->clientSocket;
    char reply[MAXMSG];
    char *replies[2] = {RPL_NAMREPLY,
    			        RPL_ENDOFNAMES
    };
    
    
    char *cname = malloc(strlen(channel_name));
    strcpy(cname, channel_name);

    list_t newlist;
     
    
    // First, check to see if the channel exists
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = CHAN;      // used in list seek
    seek_arg->value = cname;   // used in list seek
    
    pthread_mutex_lock(&lock);
    channel *channelpt = (channel *)list_seek(server->chanlist, seek_arg);
    pthread_mutex_unlock(&lock);
	

    // Create a new channel if it doesn't
    if (channelpt == NULL){
        list_init(&newlist);
        if(list_attributes_seeker(&newlist, fun_seek) == -1){
			perror("list fail");
			exit(-1);
		}
		channelpt = malloc(sizeof(channel));
		strcpy(channelpt->name, cname);
		channelpt->topic[0] = '\0';
		channelpt->mode[0] = '\0';
		channelpt->chan_users = &newlist;
        pthread_mutex_init(&(channelpt->chan_lock), NULL);
        
        pthread_mutex_lock(&lock);
        list_append(server->chanlist, channelpt);
        pthread_mutex_unlock(&lock);
    }


	// Check to see if the user is already in the channel
    seek_arg->field = CHANUSER;      // used in list seek
    seek_arg->value = client->nick;   // used in list seek
    
    pthread_mutex_lock(&(channelpt->chan_lock));
    chanuser *chanuserpt = (chanuser *)list_seek(channelpt->chan_users, seek_arg);
    pthread_mutex_unlock(&(channelpt->chan_lock));
    free(seek_arg);
    
    if (chanuserpt != NULL) return;

    // Finally, add the user to the channel
    chanuser *newuser = malloc(sizeof(chanuser));
    strcpy(newuser->nick, client->nick);
    newuser->mode[0] = '+';
    pthread_mutex_lock(&(channelpt->chan_lock));
    list_append(channelpt->chan_users, newuser);
    pthread_mutex_unlock(&(channelpt->chan_lock));
    pthread_mutex_lock(&(client->c_lock));
    list_append(client->channel_names, cname);
    pthread_mutex_unlock(&(client->c_lock));

    // Send appropriate replies
    // This first reply is send to all channel users
    snprintf(reply, MAXMSG-1, ":%s!%s@%s JOIN %s", client->nick, client->user, client->address, cname);
    strcat(reply, "\r\n");
    sendtochannel(server, channelpt, reply, NULL);
    
    
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
}




