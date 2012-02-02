/*
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *
 *  utils code for chirc project
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

extern pthread_mutex_t lock;

int chirc_handle_MOTD(chirc_server *server, person *user, chirc_message params);
int chirc_handle_LUSERS(chirc_server *server, person *user, chirc_message params);

void constr_reply(char code[4], person *client, char *reply, chirc_server *server, char *extra) {
    int replcode = strtol(code, NULL, 10);
    char replmsg[MAXMSG];
    char *servname = server->servername;
    char *version = server->version;
    char prefix[MAXMSG] = ":";
    pthread_mutex_lock(&(client->c_lock));
    strcat(prefix, servname);
    pthread_mutex_unlock(&(client->c_lock));
    char *nick = client->nick;
    if (strlen(nick) == 0) {
        nick = "*";
    }
    char *user = client->user;
    char *msg_clnt = client->address;
    switch (replcode){
        case 1:   // RPL_WELCOME
            sprintf(replmsg, ":Welcome to the Internet Relay Network %s!%s@%s", nick, user, msg_clnt);
            break;
        case 2:   // RPL_YOURHOST
            sprintf(replmsg, ":Your host is %s running version %s", servname, version);
            break;
        case 3:   // RPL_CREATED
            sprintf(replmsg, ":This server was created %s", server->birthday);
            break;
        case 4:   // RPL_MYINFO
            sprintf(replmsg, "%s %s ao mtov", servname, version); 
            break;
        case 251: // RPL_LUSERCLIENT
            sprintf(replmsg, ":There are %s users and 0 services on 1 servers", extra);
            break;
        case 252: // RPL_LUSEROP
            sprintf(replmsg, "%s :operator(s) online", extra);
            break;
        case 253: // RPL_LUSERUNKNOWN
            sprintf(replmsg, "%s :unknown connection(s)", extra);
            break;
        case 254: // RPL_LUSERCHANNELS
            sprintf(replmsg, "%s :channels formed", extra);
            break;
        case 255: // RPL_LUSERNAME
            sprintf(replmsg, ":I have %s clients and 1 servers", extra);
            break;
        case 301:
            strcpy(replmsg, extra);
            break;
        case 305:
            strcpy(replmsg, ":You are no longer marked as being away");
            break;
        case 306:
            strcpy(replmsg, ":You have been marked as being away");
            break;
        case 311: // RPL_WHOISUSER
            sprintf(replmsg, "%s", extra);
            break;
        case 312: // RPL_WHOISSERVER
            sprintf(replmsg, "%s", extra);
            break;
        case 318: // RPL_ENDWHOIS
            sprintf(replmsg, "%s :End of WHOIS list", nick);
            break;
        case 375: // RPL_MOTDSTART
            sprintf(replmsg, ":- %s Message of the day - ", servname);
            break;
        case 372: // RPL_MOTD
            sprintf(replmsg, ":- %s", extra);
            break;
        case 376: // RPL_ENDOFMOTD
            sprintf(replmsg, ":- End of MOTD command");
            break;
        case 353:
        	sprintf(replmsg, "= #foobar :foobar1 foobar2 foobar3");
        	break;
        case 366:
        	sprintf(replmsg, "#foobar :End of NAMES list");
        	break;
        case 401: // ERR_NOSUCHNICK
            sprintf(replmsg, "%s :No such nick/channel", extra);
            break;
        case 404:
            sprintf(replmsg, "%s :Cannot send to channel", extra);
            break;
        case 421: // ERR_UNKNOWNCOMMAND
            sprintf(replmsg, "%s :Unknown command", extra);
            break;
        case 422: // ERR_NOMOTD
            strcpy(replmsg, ":MOTD File is missing");
            break;
        case 433: // ERR_NICKNAMEINUSE
            sprintf(replmsg, "%s :Nickname is already in use", extra);
            break;
        case 451:
            strcpy(replmsg, ":You have no registered");
            break;
        case 462: // ERR_ALREADYREGISTERED
            strcpy(replmsg, ":Unauthorized command (already registered)");
            break;
        
        default:
            break;
    }
    snprintf(reply, MAXMSG - 2, "%s %s %s %s", prefix, code, nick, replmsg);
    strcat(reply, "\r\n");
    return;
}

void do_registration(person *client, chirc_server *server){
    int i;
    int clientSocket = client->clientSocket;
    char reply[MAXMSG];
    char *replies[4] = {RPL_WELCOME,
                        RPL_YOURHOST,
                        RPL_CREATED,
                        RPL_MYINFO,
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

int fun_seek(const void *el, const void *indicator){
    if (el == NULL || indicator == NULL){
        perror("bad argument to fun_seek");
        return 0;
    }
    
    el_indicator *el_info;
    el_info = (el_indicator *)indicator;
    int field = el_info->field;
    char *value;
    int fd;
    person *client = NULL;
    channel *chan  = NULL;

    if (field == 5)
        chan = (channel *)el;
    else
	client = (person *)el;
 
    
    if (field == 4)
        fd = el_info->fd;
    else
        value = el_info->value;
    
    if ((field != 4 && strlen(value) == 0) || field < 0 || field > 5){
        perror("bad argument to fun_seek");
        return 0;
    }
    switch (field) {
        case 0:
            if (strcmp(client->nick, value) == 0)
                return 1;
            else
                return 0;
            break;
        case 1:
            if (strcmp(client->user, value) == 0)
                return 1;
            else
                return 0;
            break;
        case 2:
            if (strcmp(client->fullname, value) == 0)
                return 1;
            else
                return 0;
            break;
        case 3:
            if (strcmp(client->address, value) == 0)
                return 1;
            else
                return 0;
            break;
        case 4:
            if (client->clientSocket == fd)
                return 1;
            else
                return 0;
            break;
		case 5:
	  	 	if (strcmp(chan->name, value) == 0)
				return 1;
	   		else
				return 0;
            break;
	    default:
            return 0;
            break;
    }
}

void sendtochannel(chirc_server *server, channel *chan, char *msg, char *sender){
    int chanSocket;
    person *user;
    char *cname = chan->name;
    mychan *dummy = malloc(sizeof(mychan));
    strcpy(dummy->name, cname);
    
    pthread_mutex_lock(&(lock));
    list_iterator_start(server->userlist);
    while(list_iterator_hasnext(server->userlist)){
        user = (person *)list_iterator_next(server->userlist);
        pthread_mutex_lock(&(user->c_lock));
        if ((sender == NULL || strcmp(user->nick, sender) != 0) && list_contains(user->my_chans, dummy)){
            chanSocket = user->clientSocket;
            if(send(chanSocket, msg, strlen(msg), 0) == -1)
            {
                perror("Socket send() failed");
                close(chanSocket);
                pthread_mutex_lock(&lock);
                list_delete(server->userlist, user);
                pthread_mutex_unlock(&lock);
                pthread_exit(NULL);
            }   
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    list_iterator_stop(server->userlist);
    pthread_mutex_unlock(&lock);
}

//sends message to all channels a user is on. does not return message to sender
void sendtoallchans(chirc_server *server, person *user, char *msg){
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = CHAN;
    mychan *dummy;
    channel *chan;
    
    pthread_mutex_lock(&(user->c_lock));
    list_iterator_start(user->my_chans);
    while(list_iterator_hasnext(user->my_chans)){
          dummy = (mychan *)list_iterator_next(user->my_chans);
          seek_arg->value = dummy->name;
          pthread_mutex_lock(&lock);
          chan = (channel *)list_seek(server->chanlist, seek_arg);
          pthread_mutex_unlock(&lock);
          sendtochannel(server, chan, msg, user->nick);
    }
    list_iterator_stop(user->my_chans);
    pthread_mutex_unlock(&(user->c_lock));
}

int fun_compare(const void *a, const void *b){
    if (a == NULL || b == NULL) {
        printf("bad compare functions\n");
        return -1;
    }
    mychan *chan1 = (mychan *)a;
    mychan *chan2 = (mychan *)b;
    
    if (strcmp(chan1->name, chan2->name) == 0)
        return 0;
    else
        return -1;
}

void logprint (logentry *tolog, chirc_server *ourserver, char *message){
    FILE *logpt = fopen("log.txt", "a");
    if(tolog != NULL)
        fprintf(logpt, "Received message \"%s\" from %s, sent message \"%s\" to %s\n", tolog->msgin, tolog->userin, tolog->msgout, tolog->userout);
    else
        fprintf(logpt, "%s\n", message);
    fclose(logpt);
    return;
}
