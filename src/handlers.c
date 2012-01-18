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

void constr_reply(char code[4], person *client, char *reply, chirc_server *server);
void do_registration(person *client, chirc_server *server);

int chirc_handle_NICK(chirc_server *server, person *user, chirc_message params);
int chirc_handle_USER(chirc_server *server, person *user, chirc_message params);
int chirc_handle_QUIT(chirc_server *server, person *user, chirc_message params);

void handle_chirc_message(chirc_server *server, person *user, chirc_message params)
{
    char *command = params[0];
    
    if      (strcmp(command, "NICK") == 0) chirc_handle_NICK(server, user, params);
    else if (strcmp(command, "USER") == 0) chirc_handle_USER(server, user, params);
    else if (strcmp(command, "QUIT") == 0) chirc_handle_QUIT(server, user, params);
}

int chirc_handle_NICK(chirc_server  *server, // current server
<<<<<<< HEAD
						 person    *user,   // current user
						 chirc_message msg     // message to be sent
)
{
    char reply[MAXMSG];
    int fd = user->fd;
    newnick = msg[1];
	if (user->nick){
        user->nick = newnick;
        constr_reply(/*whatever*/);
    }
    else{
        user->nick = newnick;
        if (user->user){
            constr_reply("001", newnick, reply);
            //send needs to be protected with mutex
            if(send(fd, reply, strlen(reply), 0) == -1){
                perror("Socket send() failed");
                close(fd);
                pthread_exit(NULL);
            }
        }
=======
                      person    *user,       // current user
                      chirc_message msg      // message to be sent
                      )
{
    char *newnick;
    int clientSocket = user->clientSocket;
    newnick = msg[1];
	if (strlen(user->nick)){
        strcpy(user->nick, newnick);
        // deal with a change in nick
    }
    else{
        strcpy(user->nick, newnick);
        if (strlen(user->user))
            do_registration(user, server);
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
    }
    return 0;
}

int chirc_handle_USER(chirc_server  *server, // current server
<<<<<<< HEAD
						 person    *user,   // current user
						 chirc_message msg     // message to be sent
)
{
	char reply[MAXMSG];
    int fd = user->fd;
    char *username = msg[1];
    char *fullname = msg[4];
    if (user->user && user->nick)
        ;//error message: already registered
    else{
        user->user = username;
        user->fullname = fullname;
        if(user->nick){
            constr_reply("001", user->nick, reply);
            //send needs to be protected with mutex
            if(send(fd, reply, strlen(reply), 0) == -1){
                perror("Socket send() failed");
                close(fd);
                pthread_exit(NULL);
            }
        }
    }
=======
                      person    *user,       // current user
                      chirc_message msg      // message to be sent
                      )
{
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    char *username = msg[1];
    char *fullname = msg[4];
    
    if ( strlen(user->user) && strlen(user->nick) ) {
        constr_reply(ERR_ALREADYREGISTRED, user, reply, server);
        
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_exit(NULL);
        }
    }
    else {
        strcpy(user->user, username);
        strcpy(user->fullname, fullname);
        if(strlen(user->nick))
            do_registration(user, server);
    }
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
    return 0;
}

int chirc_handle_QUIT(chirc_server  *server, // current server
<<<<<<< HEAD
						 person    *user,   // current user
						 chirc_message msg     // message to be sent
=======
                      person    *user,   // current user
                      chirc_message msg     // message to be sent
                      )
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
{
	return 0;
}
