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
void user_exit(chirc_server *server, person *user);

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
        case 301: // RPL_AWAY
            strcpy(replmsg, extra);
            break;
        case 305: //RPL_UNAWAY
            strcpy(replmsg, ":You are no longer marked as being away");
            break;
        case 306: //RPL_NOWAWAY
            strcpy(replmsg, ":You have been marked as being away");
            break;
        case 311: // RPL_WHOISUSER
            sprintf(replmsg, "%s", extra);
            break;
        case 312: // RPL_WHOISSERVER
            sprintf(replmsg, "%s", extra);
            break;
        case 313: // RPL_WHOISOPERATOR
            sprintf(replmsg, "%s :is an IRC operator", extra);
            break;
        case 318: // RPL_ENDWHOIS
            sprintf(replmsg, "%s :End of WHOIS list", nick);
            break;
        case 319: // RPL_WHOISCHANNELS
            sprintf(replmsg, ":%s", extra);
            break;
        case 323: // RPL_LISTEND
        	sprintf(replmsg, ":End of LIST");
        	break;
        case 331: // RPL_NOTOPIC
        	sprintf(replmsg, "%s :No topic is set", extra);
        	break;
        case 332: // RPL_TOPIC
        	sprintf(replmsg, "%s", reply);
        	break;
        case 353: // RPL_NAMREPLY
        	strcpy(replmsg, extra);
        	break;
        case 366: // RPL_ENDOFNAMES
        	sprintf(replmsg, "%s :End of NAMES list", extra);
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
        case 381: // RPL_YOUREOPER
            sprintf(replmsg, ":You are now an IRC operator");
            break;
        case 324:
        	sprintf(replmsg, "%s", extra);
        	break;
        case 401: // ERR_NOSUCHNICK
            sprintf(replmsg, "%s :No such nick/channel", extra);
            break;
        case 403: // ERR_NOSUCHCHANNEL
        	sprintf(replmsg, "%s :No such channel", extra);
        	break;
        case 404: // ERR_CANNOTSENDTOCHAN
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
        case 442: // ERR_NOTONCHANNEL
        	sprintf(replmsg, "%s :You're not on that channel", extra);
        	break;
        case 451:
            strcpy(replmsg, ":You have not registered");
            break;
        case 462: // ERR_ALREADYREGISTERED
            strcpy(replmsg, ":Unauthorized command (already registered)");
            break;
        case 464: // ERR_PASSWDMISMATCH
            sprintf(replmsg, ":Password incorrect");
            break;
        case 472: // ERR_UNKNOWNMODE
        	sprintf(replmsg, "%s :is unknown mode char to me for %s", reply, extra);
        	break;
        case 482: // ERR_CHANOPRIVISNEEDED
            sprintf(replmsg, "%s :You're not channel operator", extra);
            break;
        case 501: // ERR_UMODEUNKNOWN
            sprintf(replmsg, ":Unknown MODE flag");
            break;
        case 502: // ERR_USERSDONTMATCH
            sprintf(replmsg, ":Cannot change mode for other users");
            break;
        default:
            break;
    }
    snprintf(reply, MAXMSG - 2, "%s %s %s %s", prefix, code, nick, replmsg);
    strcat(reply, "\r\n");
    return;
}

//send all registration replies
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
            user_exit(server, client);
        }
        pthread_mutex_unlock(&(client->c_lock));
    }
    
    chirc_handle_LUSERS(server, client, NULL);
    chirc_handle_MOTD(server, client, NULL);
}

//send RPL_NAMREPLY for given channel to given user
void send_names(chirc_server *server, channel *chan, person *user){
    int clientSocket = user->clientSocket;
    char chanusers[MAXMSG];
    char reply[MAXMSG];
    person *someuser;
    int buff;
    mychan *userchan;
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    int first = 1;
    
    seek_arg->field = CHAN;
    
    pthread_mutex_lock(&(chan->chan_lock));
    sprintf(chanusers, "= %s :", chan->name);
    seek_arg->value = chan->name;           //may be safer to use strcpy here, but need to allocate space for seek_arg->value
    pthread_mutex_unlock(&(chan->chan_lock));
    
    pthread_mutex_lock(&lock);
    list_iterator_start(server->userlist);
    while(list_iterator_hasnext(server->userlist) && strlen(chanusers) < MAXMSG - 3){   //must be 3 less than MAXMSG so adding space and two mode chars won't cause overflow
        someuser = list_iterator_next(server->userlist);
        pthread_mutex_lock(&(someuser->c_lock));
        userchan = (mychan *)list_seek(someuser->my_chans, seek_arg);
        if (userchan != NULL) { //user is a member of chan
            if(!first)
                strcat(chanusers, " ");
            if(strchr(userchan->mode, (int) 'o') != NULL)
                strcat(chanusers, "@");
            if(strchr(userchan->mode, (int) 'v') != NULL)
                strcat(chanusers, "+");
            buff = MAXMSG - strlen(reply);
            strncat(chanusers, someuser->nick, buff);
            first = 0;
        }
        pthread_mutex_unlock(&(someuser->c_lock));
    }
    list_iterator_stop(server->userlist);
    pthread_mutex_unlock(&lock);
    
    constr_reply(RPL_NAMREPLY, user, reply, server, chanusers);
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1){
        perror("Socket send() failed");
        user_exit(server, user);
    }
    pthread_mutex_unlock(&(user->c_lock));
                             
    free(seek_arg);
    return;
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
    mychan *my_chan = NULL;
    
    //unpack arguments appropriately
    switch (field) {
        case 4:
            fd = el_info->fd;
            client = (person *)el;
            break;
        case 5:
            chan = (channel *)el;
            value = el_info->value;
            break;
        case 6:
            my_chan = (mychan *)el;
            value = el_info->value;
            break;
        default:
            client = (person *)el;
            value = el_info->value;
            break;
    }
    
    if ((field != 4 && strlen(value) == 0) || field < 0 || field > 6){
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
        case 6:
            if (strcmp(my_chan->name, value) == 0)
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
        if ((sender == NULL || strcmp(user->nick, sender) != 0) && list_contains(user->my_chans, dummy)){
            pthread_mutex_lock(&(user->c_lock));    //should actually lock before reading user_nick, but that causes deadlock--deal with this later
            chanSocket = user->clientSocket;
            if(send(chanSocket, msg, strlen(msg), 0) == -1)
            {
                perror("Socket send() failed");
                user_exit(server, user);
            }   
            pthread_mutex_unlock(&(user->c_lock));
        }
    }
    list_iterator_stop(server->userlist);
    pthread_mutex_unlock(&lock);
    free(dummy);
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
    free(seek_arg);
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

void channel_destroy(chirc_server *server, channel *chan){
    pthread_mutex_lock(&(chan->chan_lock));
    //make sure it should actually be destroyed
    if (chan->numusers != 0) {
        pthread_mutex_unlock(&(chan->chan_lock));
        return;
    }
    pthread_mutex_lock(&lock);
    list_delete(server->chanlist, chan);
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&(chan->chan_lock));
    
    pthread_mutex_destroy(&(chan->chan_lock));
    
    //free(chan);
    return;
}

void user_exit(chirc_server *server, person *user){         //removes all information about user and frees all associated structs/memory
    pthread_t userid = user->tid;
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = CHAN;
    channel *chan;
    mychan *dummy = malloc(sizeof(mychan));
    pthread_mutex_lock(&(user->c_lock));
    close(user->clientSocket);
    //if user is a member of any channels, decrement those channels numuser counters
    list_iterator_start(user->my_chans);
    while(list_iterator_hasnext(user->my_chans)){
        dummy = (mychan *)list_iterator_next(user->my_chans);
        seek_arg->value = dummy->name;
        pthread_mutex_lock(&lock);
        chan = (channel *)list_seek(server->chanlist, seek_arg);
        pthread_mutex_unlock(&lock);
        pthread_mutex_lock(&(chan->chan_lock));
        (chan->numusers)--;
        pthread_mutex_unlock(&(chan->chan_lock));
        if(chan->numusers == 0)
            channel_destroy(server, chan);
    }
    list_iterator_stop(user->my_chans);
    
    //free memory and delete user from userlist
    list_destroy(user->my_chans);
    free(user->address);
    
    pthread_mutex_lock(&lock);
    list_delete(server->userlist, user);
    server->numregistered--;
    pthread_mutex_unlock(&lock);
    
    pthread_mutex_unlock(&(user->c_lock));
    pthread_mutex_destroy(&(user->c_lock));
    
    free(seek_arg);
    free(dummy);
    if(userid == pthread_self())
        pthread_exit(NULL);
    else
        pthread_cancel(userid);
}
