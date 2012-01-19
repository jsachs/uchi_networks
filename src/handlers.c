/*
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *
 *  handlers code for chirc project
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

void constr_reply(char code[4], person *client, char *reply, chirc_server *server, char *extra);
void do_registration(person *client, chirc_server *server);

int chirc_handle_NICK(chirc_server *server, person *user, chirc_message params);
int chirc_handle_USER(chirc_server *server, person *user, chirc_message params);
int chirc_handle_QUIT(chirc_server *server, person *user, chirc_message params);
int chirc_handle_PRIVMSG(chirc_server *server, person *user, chirc_message params);
int chirc_handle_NOTICE(chirc_server *server, person *user, chirc_message params);
int chirc_handle_PING(chirc_server *server, person *user, chirc_message params);
int chirc_handle_UNKNOWN(chirc_server *server, person *user, chirc_message params);

void handle_chirc_message(chirc_server *server, person *user, chirc_message params)
{
    char *command = params[0];
    
    if      (strcmp(command, "NICK") == 0) chirc_handle_NICK(server, user, params);
    else if (strcmp(command, "USER") == 0) chirc_handle_USER(server, user, params);
    else if (strcmp(command, "QUIT") == 0) chirc_handle_QUIT(server, user, params);
    else if (strcmp(command, "PING") == 0) chirc_handle_PING(server, user, params);
    else if (strcmp(command, "PONG") == 0) ;
    else chirc_handle_UNKNOWN(server, user, params);
}

int chirc_handle_NICK(chirc_server  *server, // current server
                      person    *user,       // current user
                      chirc_message msg      // message to be sent
                      )
{
    char reply[MAXMSG];
    char *newnick;
    int clientSocket = user->clientSocket;
    newnick = msg[1];
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = NICK;
    seek_arg->value = newnick;
    person *clientpt = (person *)list_seek(server->userlist, seek_arg);
    if (clientpt) {
        constr_reply(ERR_NICKNAMEINUSE, user, reply, server, newnick);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    else{
        if (strlen(user->nick)){
            strcpy(user->nick, newnick);
            // deal with a change in nick
        }
        else{
            strcpy(user->nick, newnick);
            if (strlen(user->user))
                do_registration(user, server);
        }
    }
    return 0;
}

int chirc_handle_USER(chirc_server  *server, // current server
                      person    *user,       // current user
                      chirc_message msg      // message to be sent
                      )
{
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    char *username = msg[1];
    char *fullname = msg[4];
    
    if ( strlen(user->user) && strlen(user->nick) ) {
        constr_reply(ERR_ALREADYREGISTRED, user, reply, server, NULL);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    else {
        strcpy(user->user, username);
        strcpy(user->fullname, fullname);
        if(strlen(user->nick))
            do_registration(user, server);
    }
    return 0;
}

int chirc_handle_QUIT(chirc_server  *server, // current server
						 person    *user,   // current user
						 chirc_message msg     // message to be sent
                      )
{
	return 0;
}

int chirc_handle_PRIVMSG(chirc_server *server, person *user, chirc_message params)
{
    return 0;
}

int chirc_handle_NOTICE(chirc_server *server, person *user, chirc_message params)
{
    return 0;
}

int chirc_handle_PING(chirc_server *server, person *user, chirc_message params){
    char PONGback[MAXMSG];
    int clientSocket = user->clientSocket;
    char *servername = malloc(strlen(server->servername) + 1);
    strcpy(servername, server->servername);
    snprintf(PONGback, MAXMSG - 2, "PONG %s", servername);
    strcat(PONGback, "\r\n");
    
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, PONGback, strlen(PONGback), 0) == -1)
    {
        perror("Socket send() failed");
        close(clientSocket);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    return 0;
}

int chirc_handle_UNKNOWN(chirc_server *server, person *user, chirc_message params){
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    constr_reply(ERR_UNKNOWNCOMMAND, user, reply, server, params[0]);
    
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        close(clientSocket);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    return 0;
}
