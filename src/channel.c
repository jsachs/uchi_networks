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
void send_names(chirc_server *server, channel *chan, person *user);
int fun_seek(const void *el, const void *indicator);
void user_exit(chirc_server *server, person *user);

void channel_join(person *client, chirc_server *server, char* channel_name){
    int oper = 0;
    int clientSocket = client->clientSocket;
    char reply[MAXMSG];
    
    mychan *newchan;
    
    char *cname = malloc(strlen(channel_name));
    strcpy(cname, channel_name);
    
    // First, check to see if the channel exists
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = CHAN;      // used in list seek
    seek_arg->value = cname;   // used in list seek
    
    pthread_mutex_lock(&lock);
    channel *channelpt = (channel *)list_seek(server->chanlist, seek_arg);
    pthread_mutex_unlock(&lock);
	free(seek_arg);

    // Create a new channel if it doesn't
    if (channelpt == NULL){
        oper = 1;
		channelpt = malloc(sizeof(channel));
		strcpy(channelpt->name, cname);
		channelpt->topic[0] = '\0';
		channelpt->mode[0] = '\0';
        channelpt->numusers = 0;
        pthread_mutex_init(&(channelpt->chan_lock), NULL);
        
        pthread_mutex_lock(&lock);
        list_append(server->chanlist, channelpt);
        pthread_mutex_unlock(&lock);
    }


	// Check to see if the user is already in the channel
    mychan *dummy = malloc(sizeof(mychan));
    strcpy(dummy->name, cname);
    if (list_contains(client->my_chans, dummy)) {
        return;
    }
    
    // Finally, add the user to the channel
    newchan = malloc(sizeof(mychan));
    strcpy(newchan->name, cname);
    newchan->mode[0] = '\0';
    if(oper)
        strcat(newchan->mode, "o");
    pthread_mutex_lock(&(client->c_lock));
    list_append(client->my_chans, newchan);
    pthread_mutex_unlock(&(client->c_lock));
    pthread_mutex_lock(&(channelpt->chan_lock));
    channelpt->numusers++;
    pthread_mutex_unlock(&(channelpt->chan_lock));

    // Send appropriate replies
    // This first reply is send to all channel users
    snprintf(reply, MAXMSG-1, ":%s!%s@%s JOIN %s", client->nick, client->user, client->address, cname);
    strcat(reply, "\r\n");
    sendtochannel(server, channelpt, reply, NULL);
    
    // if the channel has a topic, send RPL_TOPIC
    
    if(channelpt->topic[0] != '\0'){
        snprintf(reply, MAXMSG-1, "%s %s", cname, channelpt->topic);
        constr_reply(RPL_TOPIC, client, reply, server, NULL);
        pthread_mutex_lock(&(client->c_lock));
        if (send(clientSocket, reply, strlen(reply), 0) == -1) {
            perror("Socket send() failed");
            user_exit(server, client);
        }
        pthread_mutex_unlock(&(client->c_lock));
    }
    
    send_names(server, channelpt, client);
    
    constr_reply(RPL_ENDOFNAMES, client, reply, server, cname); //ie channel name as final parameter
    pthread_mutex_lock(&(client->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1){
        perror("Socket send() failed");
        user_exit(server, client);
    }
    pthread_mutex_unlock(&(client->c_lock)); 
    
    free(dummy);
}




