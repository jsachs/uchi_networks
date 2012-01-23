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

extern pthread_mutex_t lock;
extern pthread_mutex_t loglock;

void constr_reply(char code[4], person *client, char *reply, chirc_server *server, char *extra);
void do_registration(person *client, chirc_server *server);
void logprint (logentry *tolog, chirc_server *ourserver, char *logerror);

int chirc_handle_NICK(chirc_server *server, person *user, chirc_message params);
int chirc_handle_USER(chirc_server *server, person *user, chirc_message params);
int chirc_handle_QUIT(chirc_server *server, person *user, chirc_message params);
int chirc_handle_PRIVMSG(chirc_server *server, person *user, chirc_message params);
int chirc_handle_NOTICE(chirc_server *server, person *user, chirc_message params);
int chirc_handle_PING(chirc_server *server, person *user, chirc_message params);
int chirc_handle_MOTD(chirc_server *server, person *user, chirc_message params);
int chirc_handle_WHOIS(chirc_server *server, person *user, chirc_message params);
int chirc_handle_LUSERS(chirc_server *server, person *user, chirc_message params);
int chirc_handle_UNKNOWN(chirc_server *server, person *user, chirc_message params);

void handle_chirc_message(chirc_server *server, person *user, chirc_message params)
{
    char *command = params[0];
    
    if      (strcmp(command, "NICK") == 0)    chirc_handle_NICK(server, user, params);
    else if (strcmp(command, "USER") == 0)    chirc_handle_USER(server, user, params);
    else if (strcmp(command, "QUIT") == 0)    chirc_handle_QUIT(server, user, params);
    
    else if (strcmp(command, "PRIVMSG") == 0) chirc_handle_PRIVMSG(server, user, params);
    else if (strcmp(command, "NOTICE") == 0)  chirc_handle_NOTICE(server, user, params);
    
    else if (strcmp(command, "WHOIS") == 0)   chirc_handle_WHOIS(server, user, params);
    
    else if (strcmp(command, "PING") == 0)    chirc_handle_PING(server, user, params);
    else if (strcmp(command, "PONG") == 0) ;
    else if (strcmp(command, "LUSERS") == 0) chirc_handle_LUSERS(server, user, params);
    else if (strcmp(command, "MOTD") == 0)    chirc_handle_MOTD(server, user, params);
    
    else chirc_handle_UNKNOWN(server, user, params);
}

int chirc_handle_NICK(chirc_server  *server, // current server
                      person    *user,       // current user
                      chirc_message msg      // message to be sent
                      )
{
    char reply[MAXMSG];                         // reply to be sent as a response
    char *newnick;                              // used for registering the new NICK
    int clientSocket = user->clientSocket;
    newnick = msg[1];                           // takes the new NICK from the user input
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = NICK;      // used in list seek
    seek_arg->value = newnick;   // used in list seek
    
    pthread_mutex_lock(&lock);
    person *clientpt = (person *)list_seek(server->userlist, seek_arg);
    pthread_mutex_unlock(&lock);
    free(seek_arg);
    
    if (clientpt) {
        constr_reply(ERR_NICKNAMEINUSE, user, reply, server, newnick);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)  // if there is an error, disconnect the client and delete them from the userlist
        {
            perror("Socket send() failed");
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, user);
            pthread_mutex_unlock(&lock);
            close(clientSocket);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    else{
        if (strlen(user->nick)){
            pthread_mutex_lock(&lock);
            strcpy(user->nick, newnick);
            pthread_mutex_unlock(&lock);
            // deal with a change in nick later
        }
        else{
            pthread_mutex_lock(&lock);
            strcpy(user->nick, newnick);
            pthread_mutex_unlock(&lock);
            if (strlen(user->user))
                do_registration(user, server); // registers the client if they have added a nick and username
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
    
    memmove(fullname, fullname+1, strlen(fullname));  // strips the leading colon off of the fullname
    
    if ( strlen(user->user) && strlen(user->nick) ) {
        constr_reply(ERR_ALREADYREGISTRED, user, reply, server, NULL);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, user);
            pthread_mutex_unlock(&lock);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    else {
        pthread_mutex_lock(&lock);
        strcpy(user->user, username);
        strcpy(user->fullname, fullname);
        pthread_mutex_unlock(&lock);
        if(strlen(user->nick))
            do_registration(user, server); // registers the client if they have added a nick and username
    }
    return 0;
}

int chirc_handle_QUIT(chirc_server  *server, // current server
						 person    *user,   // current user
						 chirc_message msg     // message to be sent
                      )
{
	char reply[MAXMSG];
    char *quitmsg;
    int clientSocket = user->clientSocket;
    if(strlen(msg[1]))
        quitmsg = msg[1];
    else
        quitmsg = ":Client Quit";
    
    snprintf(reply, MAXMSG - 2, ":%s!%s@%s QUIT %s", user->nick, user->user, user->address, quitmsg);
    pthread_mutex_lock(&(user->c_lock));
    
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        close(clientSocket);
        pthread_mutex_lock(&lock);
        list_delete(server->userlist, user);
        pthread_mutex_unlock(&lock);
        pthread_exit(NULL);
    }
    
    pthread_mutex_unlock(&(user->c_lock));
    snprintf(reply, MAXMSG - 2, "ERROR :Closing Link: %s (%s)", user->address, quitmsg);
    pthread_mutex_lock(&(user->c_lock));
    
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
    }
    close(clientSocket);
    pthread_mutex_unlock(&(user->c_lock));
    pthread_mutex_lock(&lock);
    list_delete(server->userlist, user);
    pthread_mutex_unlock(&lock);
    pthread_exit(NULL);
    
    return 0;
}

int chirc_handle_PRIVMSG(chirc_server *server, person *user, chirc_message params)
{
    char priv_msg[MAXMSG];
    char reply[MAXMSG];
    int senderSocket = user->clientSocket;
    char *target_nick = params[1];
    char logerror[MAXMSG];
    
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = NICK;
    seek_arg->value = target_nick;
    pthread_mutex_lock(&lock);
    person *recippt = (person *)list_seek(server->userlist, seek_arg);
    pthread_mutex_unlock(&lock);
    free(seek_arg);
    
    if (!recippt) {
        constr_reply(ERR_NOSUCHNICK, user, reply, server, target_nick);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(senderSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(senderSocket);
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, user);
            pthread_mutex_unlock(&lock);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
    }
    else
    {
        pthread_mutex_lock(&(user->c_lock));
        snprintf(priv_msg, (MAXMSG-1), ":%s!%s@%s %s %s %s", user->nick,
                                                             user->user,
                                                             user->address,
                                                             params[0],
                                                             params[1],
                                                             params[2]
        );
        strcpy(user->tolog->msgout, priv_msg);
        strcat(priv_msg, "\r\n");
        pthread_mutex_unlock(&(user->c_lock));    
        strcpy(user->tolog->userout, params[1]);
        pthread_mutex_lock(&(recippt->c_lock));
        pthread_mutex_lock(&loglock);
        if(send(recippt->clientSocket, priv_msg, strlen(priv_msg), 0) == -1)
        {
            pthread_mutex_lock(&loglock);
            sprintf(logerror, "send to user %s from PRIVMSG failed with errno %d\n", recippt->nick, errno);
            logprint(NULL, server, logerror);
            pthread_mutex_unlock(&loglock);
            
            close(recippt->clientSocket);
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, recippt);
            pthread_mutex_unlock(&lock);
            pthread_exit(NULL);
        }
        logprint(user->tolog, server, NULL);
        pthread_mutex_unlock(&loglock);
        pthread_mutex_unlock(&(recippt->c_lock));
    } 
    return 0;
}

int chirc_handle_NOTICE(chirc_server *server, person *user, chirc_message params)
{
    char notice[MAXMSG];
    char *target_nick = params[1];
    char logerror[MAXMSG];
    
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = NICK;
    seek_arg->value = target_nick;
    pthread_mutex_lock(&lock);
    person *recippt = (person *)list_seek(server->userlist, seek_arg);
    pthread_mutex_unlock(&lock);
    free(seek_arg);
    
    if (recippt)
    {
        pthread_mutex_lock(&(recippt->c_lock));
        snprintf(notice, MAXMSG - 2, ":%s!%s@%s %s %s %s", user->nick,
                                                           user->user,
                                                           user->address,
                                                           params[0],
                                                           params[1],
                                                           params[2]
        );
        strcat(notice, "\r\n");
    
    
        if(send(recippt->clientSocket, notice, strlen(notice), 0) == -1)
        {
            pthread_mutex_lock(&loglock);
            sprintf(logerror, "send to %s from NOTICE failed with errno %d\n", recippt->nick, errno);
            logprint(NULL, server, logerror);
            pthread_mutex_unlock(&loglock);
            
            close(recippt->clientSocket);
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, user);
            pthread_mutex_unlock(&lock);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(recippt->c_lock));
    }

    return 0;
}

int chirc_handle_PING(chirc_server *server, person *user, chirc_message params){
    char PONGback[MAXMSG];
    int clientSocket = user->clientSocket;
    char *servername = malloc(strlen(server->servername) + 1);
    char logerror[MAXMSG];
    
    pthread_mutex_lock(&lock);
    strcpy(servername, server->servername);
    snprintf(PONGback, MAXMSG - 2, "PONG %s", servername);
    strcat(PONGback, "\r\n");
    pthread_mutex_unlock(&lock);
    free(servername);
    
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, PONGback, strlen(PONGback), 0) == -1)
    {
        pthread_mutex_lock(&loglock);
        sprintf(logerror, "send to user %s from PING failed with errno %d\n", user->nick, errno);
        logprint(NULL, server, logerror);
        pthread_mutex_unlock(&loglock);
        
        close(clientSocket);
        pthread_mutex_lock(&lock);
        list_delete(server->userlist, user);
        pthread_mutex_unlock(&lock);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    return 0;
}

int chirc_handle_MOTD(chirc_server *server, person *user, chirc_message params)
{
    char motd[80];
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    
    FILE *fp;
    if((fp = fopen("motd.txt", "r")) == NULL)  // what to do if no MOTD is found
    {
        constr_reply(ERR_NOMOTD, user, reply, server, NULL);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, user);
            pthread_mutex_unlock(&lock);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    
    else
    {   
        constr_reply(RPL_MOTDSTART, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, user);
            pthread_mutex_unlock(&lock);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        
        while(fgets(motd,sizeof(motd),fp) != NULL) // loops through lines of MOTD, constructing a RPL_MOTD for each line
        {
            if (motd[strlen(motd) - 1] == '\n') {
            motd[strlen(motd) - 1] = '\0';
            }
            
            constr_reply(RPL_MOTD, user, reply, server, motd);
            pthread_mutex_lock(&(user->c_lock));
            if(send(clientSocket, reply, strlen(reply), 0) == -1)
            {
                perror("Socket send() failed");
                pthread_mutex_lock(&lock);
                list_delete(server->userlist, user);
                pthread_mutex_unlock(&lock);
                close(clientSocket);
                pthread_exit(NULL);
            }
            pthread_mutex_unlock(&(user->c_lock));
        }
        
        
        constr_reply(RPL_ENDOFMOTD, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, user);
            pthread_mutex_unlock(&lock);
            close(clientSocket);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    return 0;
}
    
int chirc_handle_WHOIS(chirc_server *server, person *user, chirc_message params)
{
    char reply[MAXMSG];
    char wiuser[MAXMSG];
    char wiserver[MAXMSG];
    int clientSocket = user->clientSocket;
    char *target_nick = params[1];
    
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = NICK;
    seek_arg->value = target_nick;
    pthread_mutex_lock(&lock);
    person *whoispt = (person *)list_seek(server->userlist, seek_arg);
    pthread_mutex_unlock(&lock);
    if (!whoispt) {  // the case where no nick is found
        constr_reply(ERR_NOSUCHNICK, user, reply, server, target_nick);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));    
    }
    else
    {
        pthread_mutex_lock(&(user->c_lock));
        snprintf(wiuser, MAXMSG - 2, "%s %s %s * :%s", params[1],
                                                       whoispt->user,
                                                       whoispt->address,
                                                       whoispt->fullname
        );
        pthread_mutex_unlock(&(user->c_lock));    
        
        constr_reply(RPL_WHOISUSER, user, reply, server, wiuser); // passes the whois lookup for user to constr_reply
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        pthread_mutex_lock(&(user->c_lock));
        
        snprintf(wiserver, MAXMSG - 2, "%s %s :%s", params[1],
                                                    whoispt->address,
                                                    "<server info>"   // placeholder
        );
        pthread_mutex_unlock(&(user->c_lock));    
        
        constr_reply(RPL_WHOISSERVER, user, reply, server, wiserver);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        constr_reply(RPL_ENDOFWHOIS, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_exit(NULL);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
    }
    
    return 0;
}

int chirc_handle_LUSERS(chirc_server *server, person *user, chirc_message params){
    char reply[MAXMSG];
    char stats[5];
    int clientSocket = user->clientSocket;
    unsigned int unknown;
    char logerror[MAXMSG];
    
    //check number of known connections
    pthread_mutex_lock(&lock);
    unsigned int userme = list_size(server->userlist);
    unsigned int numchannels = list_size(server->chanlist);
    unsigned int known = server->numregistered;
    pthread_mutex_unlock(&lock);

    sprintf(stats, "%u", known);

    

    constr_reply(RPL_LUSERCLIENT, user, reply, server, stats);
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        pthread_mutex_lock(&loglock);
        sprintf(logerror, "send to %s from LUSERS failed with errno %d\n", user->nick, errno);
        logprint(NULL, server, logerror);
        pthread_mutex_unlock(&loglock);
        
        pthread_mutex_lock(&lock);
        list_delete(server->userlist, user);
        pthread_mutex_unlock(&lock);
        close(clientSocket);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    //we'll check for operators during the iteration session once we've implemented OPER
    sprintf(stats, "%u", 0);
    
    constr_reply(RPL_LUSEROP, user, reply, server, stats);
    
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        pthread_mutex_lock(&loglock);
        sprintf(logerror, "send to user %s from LUSERS failed with errno %d\n", user->nick, errno);
        logprint(NULL, server, logerror);
        pthread_mutex_unlock(&loglock);
        
        pthread_mutex_lock(&lock);
        list_delete(server->userlist, user);
        pthread_mutex_unlock(&lock);
        close(clientSocket);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    if (userme >= known){
        unknown = userme - known;
        sprintf(stats, "%u", unknown);
    }
    else
        sprintf(stats, "err");
    
    constr_reply(RPL_LUSERUNKNOWN, user, reply, server, stats);
    
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        pthread_mutex_lock(&loglock);
        sprintf(logerror, "send to user %s from LUSERS failed with errno %d\n", user->nick, errno);
        logprint(NULL, server, logerror);
        pthread_mutex_unlock(&loglock);
        
        pthread_mutex_lock(&lock);
        list_delete(server->userlist, user);
        pthread_mutex_unlock(&lock);
        close(clientSocket);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    sprintf(stats, "%u", numchannels);
    
    constr_reply(RPL_LUSERCHANNELS, user, reply, server, stats);
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        pthread_mutex_lock(&loglock);
        sprintf(logerror, "send to user %s from LUSERS failed with errno %d\n", user->nick, errno);
        logprint(NULL, server, logerror);
        pthread_mutex_unlock(&loglock);
        
        pthread_mutex_lock(&lock);
        list_delete(server->userlist, user);
        pthread_mutex_unlock(&lock);
        close(clientSocket);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    sprintf(stats, "%u", userme);
    constr_reply(RPL_LUSERME, user, reply, server, stats);
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        pthread_mutex_lock(&lock);
        list_delete(server->userlist, user);
        pthread_mutex_unlock(&lock);
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
        pthread_mutex_lock(&lock);
        list_delete(server->userlist, user);
        pthread_mutex_unlock(&lock);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    return 0;
}
