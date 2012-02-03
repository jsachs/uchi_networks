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

//forward declarations of functions in utils
void constr_reply(char code[4], person *client, char *reply, chirc_server *server, char *extra);
void do_registration(person *client, chirc_server *server);
void sendtoallchans(chirc_server *server, person *user, char *msg);
void channel_join(person *client, chirc_server *server, char* channel_name);
void sendtochannel(chirc_server *server, channel *chan, char *msg, char *sender);
void channel_destroy(chirc_server *server, channel *chan);
void user_exit(chirc_server *server, person *user);
void send_names(chirc_server *server, channel *chan, person *user);

//all the handlers
int chirc_handle_NICK(chirc_server *server, person *user, chirc_message params);
int chirc_handle_USER(chirc_server *server, person *user, chirc_message params);
int chirc_handle_QUIT(chirc_server *server, person *user, chirc_message params);
int chirc_handle_PRIVMSG(chirc_server *server, person *user, chirc_message params);
int chirc_handle_NOTICE(chirc_server *server, person *user, chirc_message params);
int chirc_handle_PING(chirc_server *server, person *user, chirc_message params);
int chirc_handle_MOTD(chirc_server *server, person *user, chirc_message params);
int chirc_handle_WHOIS(chirc_server *server, person *user, chirc_message params);
int chirc_handle_LUSERS(chirc_server *server, person *user, chirc_message params);
int chirc_handle_AWAY(chirc_server *server, person *user, chirc_message params);
int chirc_handle_JOIN(chirc_server *server, person *user, chirc_message params);
int chirc_handle_PART(chirc_server *server, person *user, chirc_message params);
int chirc_handle_TOPIC(chirc_server *server, person *user, chirc_message params);

int chirc_handle_LIST(chirc_server *server, person *user, chirc_message params);
int chirc_handle_WHO(chirc_server *server, person *user, chirc_message params);
int chirc_handle_NAMES(chirc_server *server, person *user, chirc_message params);
int chirc_handle_MODE(chirc_server *server, person *user, chirc_message params);
int chirc_handle_OPER(chirc_server *server, person *user, chirc_message params);

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
    else if (strcmp(command, "PONG") == 0) ;  //drop PONG silently
    else if (strcmp(command, "LUSERS") == 0)  chirc_handle_LUSERS(server, user, params);
    else if (strcmp(command, "MOTD") == 0)    chirc_handle_MOTD(server, user, params);
    
    else if (strcmp(command, "JOIN") == 0)    chirc_handle_JOIN(server, user, params);
    else if (strcmp(command, "AWAY") == 0)    chirc_handle_AWAY(server, user, params);
    else if (strcmp(command, "PART") == 0)    chirc_handle_PART(server, user, params);
    else if (strcmp(command, "TOPIC") == 0)   chirc_handle_TOPIC(server, user, params);
    else if (strcmp(command, "NAMES") == 0)   chirc_handle_NAMES(server, user, params);
    else if (strcmp(command, "MODE") == 0)   chirc_handle_MODE(server, user, params);
    else if (strcmp(command, "OPER") == 0)   chirc_handle_OPER(server, user, params);
    
    else chirc_handle_UNKNOWN(server, user, params);
}

int chirc_handle_NICK(chirc_server  *server, // current server
                      person    *user,       // current user
                      chirc_message msg      // message received
                      )
{
    char reply[MAXMSG];                         // reply to be sent as a response
    char *newnick;                              // used for registering the new NICK
    int clientSocket = user->clientSocket;
    newnick = msg[1];
    
    //check list to see if someone already has that nickname
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = NICK;      // used in list seek
    seek_arg->value = newnick;   // used in list seek
    
    pthread_mutex_lock(&lock);
    person *clientpt = (person *)list_seek(server->userlist, seek_arg);
    pthread_mutex_unlock(&lock);
    free(seek_arg);
    
    
    if (clientpt) { //nickname is already in use
        constr_reply(ERR_NICKNAMEINUSE, user, reply, server, newnick);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)  // if there is an error, disconnect the client and delete them from the userlist
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    else{
        if (strlen(user->nick)){    //changing NICK already given
            //send notification of change in NICK
            snprintf(reply, MAXMSG - 2, ":%s!%s@%s NICK :%s", user->nick, user->user, user->address, newnick);
            strcat(reply, "\r\n");
            pthread_mutex_lock(&(user->c_lock));
            if(send(clientSocket, reply, strlen(reply), 0) == -1)
            {
                perror("Socket send() failed");
                user_exit(server, user);
            }
            
            //actually change nick--also need to change it in each channel!
            pthread_mutex_unlock(&(user->c_lock));
            sendtoallchans(server, user, reply);
            pthread_mutex_lock(&lock);
            strcpy(user->nick, newnick);
            pthread_mutex_unlock(&lock);

        }
        else{
            //this is the first time nick is given
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
                      chirc_message msg      // message received
                      )
{
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    char *username = msg[1];
    char *fullname = msg[4];
    
    //remove colon at front of fullname
    memmove(fullname, fullname+1, strlen(fullname));

    if ( strlen(user->user) && strlen(user->nick) ) {   //already registered
        constr_reply(ERR_ALREADYREGISTRED, user, reply, server, NULL);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    else {  //first time user is sent, so get info
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
						 chirc_message msg     // message received
                      )
{
	char reply[MAXMSG];
    char *quitmsg;
    int clientSocket = user->clientSocket;
    
    if(strlen(msg[1]))
        quitmsg = msg[1];
    else
        quitmsg = ":Client Quit";   //default
    
    //relay quit message to channels
    snprintf(reply, MAXMSG - 2, ":%s!%s@%s QUIT %s", user->nick, user->user, user->address, quitmsg);
    strcat(reply, "\r\n");
    
    sendtoallchans(server, user, reply);
    
    //send ERROR reply
    snprintf(reply, MAXMSG - 2, "ERROR :Closing Link: %s (%s)", user->address, quitmsg + 1);
    strcat(reply, "\r\n");
    
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
    }
    pthread_mutex_unlock(&(user->c_lock));
    //close socket and remove user from userlist
    user_exit(server, user);
    
    return 0;
}

int chirc_handle_PRIVMSG(chirc_server *server, //current server
                         person *user,          //current user
                         chirc_message params)  //message received
{
    char priv_msg[MAXMSG];
    char reply[MAXMSG];
    int senderSocket = user->clientSocket;
    char *target_name = params[1];  //may be a nickname or channel name
    char *recipaway;
    char awaymsg[MAXMSG];
    mychan *dummy = malloc(sizeof(mychan));
    strcpy(dummy->name, target_name);
    
    //check that sender is registered
    if(!(strlen(user->nick) && strlen(user->user))){
        constr_reply(ERR_NOTREGISTERED, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(senderSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        free(dummy);
        return 0;
    }
    
    //try to get recipient from userlist and chanlist
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = NICK;
    seek_arg->value = target_name;
    pthread_mutex_lock(&lock);
    person *recippt = (person *)list_seek(server->userlist, seek_arg);
    seek_arg->field = CHAN;
    channel *chanpt = (channel *)list_seek(server->chanlist, seek_arg);
    pthread_mutex_unlock(&lock);


    
    
    if (!recippt && !chanpt) {
        constr_reply(ERR_NOSUCHNICK, user, reply, server, target_name);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(senderSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
    }
    
    //relay message
    else{
        pthread_mutex_lock(&(user->c_lock));
        snprintf(priv_msg, (MAXMSG-1), ":%s!%s@%s %s %s %s", user->nick,
                                                             user->user,
                                                             user->address,
                                                             params[0],
                                                             params[1],
                                                             params[2]
        );

        strcat(priv_msg, "\r\n");
        pthread_mutex_unlock(&(user->c_lock)); 
        if (recippt != NULL){               //recipient is an individual
            pthread_mutex_lock(&(recippt->c_lock));
            if(send(recippt->clientSocket, priv_msg, strlen(priv_msg), 0) == -1)
            {
                perror("Socket send() failed");
                user_exit(server, recippt);
            }
            //check whether recipient is away
            recipaway = strchr(recippt->mode, (int) 'a');
            pthread_mutex_unlock(&(recippt->c_lock));
            if (recipaway != NULL) {    //recipient is away
                pthread_mutex_lock(&(recippt->c_lock));
                snprintf(awaymsg, MAXMSG, "%s %s", recippt->nick, recippt->away);
                pthread_mutex_unlock(&(recippt->c_lock));
                constr_reply(RPL_AWAY, user, reply, server, awaymsg);
                
                pthread_mutex_lock(&(user->c_lock));
                if(send(senderSocket, reply, strlen(reply), 0) == -1)
                {
                    perror("Socket send() failed");
                    user_exit(server, user);
                }
                pthread_mutex_unlock(&(user->c_lock));
            }
        }
        else{       //recipient is a channel
            //check that user is member of channel
            if (!list_contains(user->my_chans, dummy)) {
                constr_reply(ERR_CANNOTSENDTOCHAN, user, reply, server, target_name);
                
                pthread_mutex_lock(&(user->c_lock));
                if(send(senderSocket, reply, strlen(reply), 0) == -1)
                {
                    perror("Socket send() failed");
                    user_exit(server, user);
                }
                pthread_mutex_unlock(&(user->c_lock));
                
            }
            else
                sendtochannel(server, chanpt, priv_msg, user->nick);
        }
    }
        
    free(seek_arg);
    free(dummy);    
    return 0;
}

int chirc_handle_NOTICE(chirc_server *server,  //current server
                        person *user,          //current user
                        chirc_message params)  //message received
{
    int clientSocket = user->clientSocket;
    char notice[MAXMSG];
    char reply[MAXMSG];
    char *target_name = params[1];
    mychan *dummy = malloc(sizeof(mychan));
    strcpy(dummy->name, target_name);
    
    //get pointer to recipient
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = NICK;
    seek_arg->value = target_name;
    pthread_mutex_lock(&lock);
    person *recippt = (person *)list_seek(server->userlist, seek_arg);
    seek_arg->field = CHAN;
    channel *chanpt = (channel *)list_seek(server->chanlist, seek_arg);
    pthread_mutex_unlock(&lock);
    free(seek_arg);
    
    //check that sender is registered--although this is a notice, the reference server indicates that ERR_NOTREGISTERED should still be returned to unregistered user
    if(!(strlen(user->nick) && strlen(user->user))){
        constr_reply(ERR_NOTREGISTERED, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        free(dummy);
        return 0;
    }
    
    //construct message
    snprintf(notice, MAXMSG - 2, ":%s!%s@%s %s %s %s", user->nick,
             user->user,
             user->address,
             params[0],
             params[1],
             params[2]
             );
    
    strcat(notice, "\r\n");
    
    //do nothing if recipient does not exist
    
    //if the recipient is a user
    if (recippt)
    {
        pthread_mutex_lock(&(recippt->c_lock));
        if(send(recippt->clientSocket, notice, strlen(notice), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, recippt);
        }
        pthread_mutex_unlock(&(recippt->c_lock));
    }
    
    //if the recipient is a channel
    if (chanpt){
        
        //only send if user is member of channel; otherwise do nothing
        if (list_contains(user->my_chans, dummy)) 
            sendtochannel(server, chanpt, notice, user->nick);
    }
    free(dummy);
    return 0;
}

int chirc_handle_PING(chirc_server *server, //current server
                      person *user,         //current user
                      chirc_message params) //message received
{
    char PONGback[MAXMSG];
    int clientSocket = user->clientSocket;
    char *servername = malloc(strlen(server->servername) + 1);
    
    //get servername
    pthread_mutex_lock(&lock);
    strcpy(servername, server->servername);
    snprintf(PONGback, MAXMSG - 2, "PONG %s", servername);
    strcat(PONGback, "\r\n");
    pthread_mutex_unlock(&lock);
    
    free(servername);
    
    //check that sender is registered
    if(!(strlen(user->nick) && strlen(user->user))){
        constr_reply(ERR_NOTREGISTERED, user, PONGback, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, PONGback, strlen(PONGback), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        return 0;
    }
    
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, PONGback, strlen(PONGback), 0) == -1)
    {
        perror("Socket send() failed");
        user_exit(server, user);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    return 0;
}

int chirc_handle_MOTD(chirc_server *server,     //current server
                      person *user,             //current user
                      chirc_message params)     //message received
{
    char motd[80];
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    
    FILE *fp;
    
    //check that sender is registered
    if(!(strlen(user->nick) && strlen(user->user))){
        constr_reply(ERR_NOTREGISTERED, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        return 0;
    }
    
    //error message for no MOTD
    if((fp = fopen("motd.txt", "r")) == NULL)
    {
        constr_reply(ERR_NOMOTD, user, reply, server, NULL);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
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
            user_exit(server, user);
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
                user_exit(server, user);
            }
            pthread_mutex_unlock(&(user->c_lock));
        }
        
        constr_reply(RPL_ENDOFMOTD, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    return 0;
}
 
//I think we're not sufficiently protecting whoispt here, deal with it later
int chirc_handle_WHOIS(chirc_server *server, //current server
                       person *user,         //current user
                       chirc_message params) //message received
{
    char reply[MAXMSG];
    char wiuser[MAXMSG];        //WHOISUSER message
    char wiserver[MAXMSG];      //WHOISSERVER message
    char wichannels[MAXMSG];    //WHOISCHANNELS message
    int clientSocket = user->clientSocket;
    char *target_nick = params[1];
    mychan *whochan;
    int buff = MAXMSG - 1;          //to keep track of space left in wichannels buffer
    int numchans = 0;
    
    //get pointer to person we're asking about
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = NICK;
    seek_arg->value = target_nick;
    
    pthread_mutex_lock(&lock);
    person *whoispt = (person *)list_seek(server->userlist, seek_arg);
    pthread_mutex_unlock(&lock);
    
    //check that sender is registered
    if(!(strlen(user->nick) && strlen(user->user))){
        constr_reply(ERR_NOTREGISTERED, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        free(seek_arg);
        return 0;
    }
    
    //no such person
    if (!whoispt) {
        constr_reply(ERR_NOSUCHNICK, user, reply, server, target_nick);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));    
    }
    
    else
    {
        //WHOISUSER
        snprintf(wiuser, MAXMSG - 2, "%s %s %s * :%s", params[1],
                                                       whoispt->user,
                                                       whoispt->address,
                                                       whoispt->fullname
        ); 
        
        constr_reply(RPL_WHOISUSER, user, reply, server, wiuser); // passes the whois lookup for user to constr_reply
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        //WHOISCHANNELS
        snprintf(wichannels, buff, "%s :", target_nick);
        pthread_mutex_lock(&(whoispt->c_lock));
        list_iterator_start(whoispt->my_chans);
        while(list_iterator_hasnext(whoispt->my_chans) && buff > 0){
            numchans++;
            whochan = (mychan *)list_iterator_next(whoispt->my_chans);
            if(strchr(whochan->mode, (int) 'o') != NULL){
                strcat(wichannels, "@");
            }
            buff = MAXMSG - strlen(wichannels);
            strncat(wichannels, whochan->name, buff);
            if (buff > 0 && list_iterator_hasnext(whoispt->my_chans)){
                strcat(wichannels, " ");
            }
        }
        pthread_mutex_unlock(&(whoispt->c_lock));
        
        //only send WHOISCHANNELS if user is on channels
        if(numchans){
            constr_reply(RPL_WHOISCHANNELS, user, reply, server, wichannels);
            pthread_mutex_lock(&(user->c_lock));
            if(send(clientSocket, reply, strlen(reply), 0) == -1)
            {
                perror("Socket send() failed");
                user_exit(server, user);
            }
            pthread_mutex_unlock(&(user->c_lock));
        }
        
        //WHOISSERVER
        snprintf(wiserver, MAXMSG - 2, "%s %s :%s", params[1],
                                                    whoispt->address,
                                                    "chirc-0.3"   // should probably actually store this someplace, like server struct
        );
        pthread_mutex_unlock(&(user->c_lock));    
        
        constr_reply(RPL_WHOISSERVER, user, reply, server, wiserver);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        //WHOISOPERATOR
        if(strchr(whoispt->mode, (int)'@') != NULL){
            constr_reply(RPL_WHOISOPERATOR, user, reply, server, target_nick);
            pthread_mutex_lock(&(user->c_lock));
            if(send(clientSocket, reply, strlen(reply), 0) == -1)
            {
                perror("Socket send() failed");
                user_exit(server, user);
            }
        }
        
        //ENDOFWHOIS
        constr_reply(RPL_ENDOFWHOIS, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
    }
    free(seek_arg);
    return 0;
}

int chirc_handle_LUSERS(chirc_server *server,   //current server
                        person *user,           //current user
                        chirc_message params){  //message received
    char reply[MAXMSG];
    char stats[5];
    int clientSocket = user->clientSocket;
    unsigned int unknown;
    
    //check number of known connections
    pthread_mutex_lock(&lock);
    unsigned int userme = list_size(server->userlist);
    unsigned int numchannels = list_size(server->chanlist);
    unsigned int known = server->numregistered;
    pthread_mutex_unlock(&lock);
    
    //check that sender is registered
    if(!(strlen(user->nick) && strlen(user->user))){
        constr_reply(ERR_NOTREGISTERED, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        return 0;
    }
    
    //RPL_LUSERCLIENT
    sprintf(stats, "%u", known);
    constr_reply(RPL_LUSERCLIENT, user, reply, server, stats);
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        user_exit(server, user);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    //RPL_LUSEROP
    //we'll check for operators in server struct
    sprintf(stats, "%u", 0);
    
    constr_reply(RPL_LUSEROP, user, reply, server, stats);
    
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        user_exit(server, user);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    
    //RPL_LUSERUNKNOWN
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
        perror("Socket send() failed");
        user_exit(server, user);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    //RPL_LUSERCHANNELS
    sprintf(stats, "%u", numchannels);
    
    constr_reply(RPL_LUSERCHANNELS, user, reply, server, stats);
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        user_exit(server, user);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    //RPL_LUSERME
    sprintf(stats, "%u", userme);
    constr_reply(RPL_LUSERME, user, reply, server, stats);
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        user_exit(server, user);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    return 0;
}
    

int chirc_handle_JOIN(chirc_server *server,  //current server
                         person *user,          //current user
                         chirc_message params)  //message received
{
    channel_join(user, server, params[1]);
    return 0;
}

int chirc_handle_PART(chirc_server *server, person *user, chirc_message params)
{
	char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    mychan *dummy = malloc(sizeof(dummy));
    char *cname = malloc(strlen(params[1]));
    strcpy(cname, params[1]);
    strcpy(dummy->name, cname);
    
    // needs to check that the channel exists
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    seek_arg->field = CHAN;      // used in list seek
    seek_arg->value = cname;   // used in list seek
    pthread_mutex_lock(&lock);
    channel *channelpt = (channel *)list_seek(server->chanlist, seek_arg);
    pthread_mutex_unlock(&lock);
    free(seek_arg);
    
    if(channelpt == NULL){
    	constr_reply(ERR_NOSUCHCHANNEL, user, reply, server, cname);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        free(dummy);
        free(cname);
        return 0;
    }
    
    // needs to check that the user is in the channel
    if (!list_contains(user->my_chans, dummy)){
    	constr_reply(ERR_NOTONCHANNEL, user, reply, server, cname);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        free(dummy);
        free(cname);
        return 0;
    }
    
    // send the part message to the channel
    if(params[2][0]=='\0')
    	snprintf(reply,MAXMSG-1,":%s!%s@%s PART %s",user->nick,user->user,user->address,params[1]);
    else
    	snprintf(reply,MAXMSG-1,":%s!%s@%s PART %s %s",user->nick,user->user,user->address,params[1],params[2]);
    
    strcat(reply, "\r\n"); 
    sendtochannel(server, channelpt, reply, NULL);
    
    // delete the user from the channel
    // delete the channel from the user's list of channels
    pthread_mutex_lock(&(user->c_lock));
    list_delete(user->my_chans, dummy);
    pthread_mutex_unlock(&(user->c_lock));
    
    pthread_mutex_lock(&(channelpt->chan_lock));
    (channelpt->numusers)--;
    pthread_mutex_unlock(&(channelpt->chan_lock));
    
    // if the channel is empty, destroy the channel
    
    if(channelpt->numusers==0) {
    	channel_destroy(server, channelpt);
    }
    
    free(cname);
    free(dummy);
    return 0;
}

//needs to check that you're registered first
int chirc_handle_AWAY(chirc_server *server, person *user, chirc_message params){
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    char *away = NULL;
    char *c;
    
    //check that sender is registered
    if(!(strlen(user->nick) && strlen(user->user))){
        constr_reply(ERR_NOTREGISTERED, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        return 0;
    }
    
    //check whether sender is already away
    if (strlen(user->mode) != 0)
            away = strchr(user->mode, (int) 'a'); 
    
    //determine whether they're setting or removing away message
    if(strlen(params[1]) == 0){     //no away param, so removing away message
        //if mode is away, change it
        if(away != NULL){
            pthread_mutex_lock(&(user->c_lock));
                for(c = away; *c != '\0'; c++)
                    *c = *(c+1);
            pthread_mutex_unlock(&(user->c_lock));
        }

        //send RPL_UNAWAY
        constr_reply(RPL_UNAWAY, user, reply, server, NULL);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    
    else{
        //if mode isn't away, set it to away
        if(away == NULL){
            pthread_mutex_lock(&(user->c_lock));
            strcat(user->mode, "a");
            pthread_mutex_unlock(&(user->c_lock));
        }
            
        //set away message to params[1]
        pthread_mutex_lock(&(user->c_lock));
        strcpy(user->away, params[1]);
        pthread_mutex_unlock(&(user->c_lock));
        
        
        //send RPL_NOWAWAY
        constr_reply(RPL_NOWAWAY, user, reply, server, NULL);
        
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
                        
    return 0;
}

int chirc_handle_TOPIC(chirc_server *server, person *user, chirc_message params)
{
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    char *cname = malloc(strlen(params[1]));
    strcpy(cname, params[1]);
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    mychan *dummy = malloc(sizeof(mychan));
    strcpy(dummy->name, cname);
    
    
    // get a pointer to the requested channel
    seek_arg->field = CHAN;      // used in list seek
    seek_arg->value = cname;   // used in list seek
    pthread_mutex_lock(&lock);
    channel *channelpt = (channel *)list_seek(server->chanlist, seek_arg);
    pthread_mutex_unlock(&lock);
    
    // check to make sure the user is in the channel
    if (!list_contains(user->my_chans, dummy)){
    	constr_reply(ERR_NOTONCHANNEL, user, reply, server, cname);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        free(seek_arg);
        free(dummy);
        free(cname);
        return 0;
    }
    
    // if there is a topic parameter, check if the user is operator
    // if they are, they can set the topic
    if(params[2][0] != '\0') {
    	// check if channel is moderated             
	//if(strchr(user->mode,(int)'@') == NULL) return 0;
        
        // if topic is changed, relay it to the channel
    	strcpy(channelpt->topic, params[2]);
    	snprintf(reply,MAXMSG-1, ":%s!%s@%s TOPIC %s %s",user->nick,user->user,user->address,
    	                                                 cname,channelpt->topic);
    	strcat(reply, "\r\n");
        sendtochannel(server, channelpt, reply, NULL);
    }
    	
    
    // then determine the correct reply
    if(channelpt->topic[0] == '\0'){
    	constr_reply(RPL_NOTOPIC, user, reply, server, cname);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    else {
    	snprintf(reply,MAXMSG-1, "%s %s", cname, channelpt->topic);
        constr_reply(RPL_TOPIC, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    free(seek_arg);
    free(dummy);
    free(cname);
    return 0;
}
    
int chirc_handle_LIST(chirc_server *server, person *user, chirc_message params)
{
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    char *cname = malloc(strlen(params[1]));
    strcpy(cname, params[1]);
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    
    // if we get a specific channel request
    if(params[1][0] != '\0') {
    	seek_arg->field = CHAN;      // used in list seek
    	seek_arg->value = cname;   // used in list seek
    	pthread_mutex_lock(&lock);
    	channel *channelpt = (channel *)list_seek(server->chanlist, seek_arg);
    	pthread_mutex_unlock(&lock);
		
	snprintf(reply,MAXMSG-1,":%s!%s@%s PART %s %s",user->nick,user->user,user->address,params[1],params[2]);
    	strcat(reply, "\r\n"); // tests are not seeing this for some reason
    	}
    	
    	// otherwise, iterate over all channels
    
    
	free(cname);
	return 0;
}

int chirc_handle_NAMES(chirc_server *server, person *user, chirc_message params)
{
    int buff;
    int clientSocket = user->clientSocket;
    int first = 1;
    char reply[MAXMSG];
    char antisocial[MAXMSG];  //list of people not on channels
    channel *chan;
    person *someone;
    el_indicator *seek_arg = malloc(sizeof(seek_arg));
    seek_arg->field = CHAN;
    
    
    //check that sender is registered
    if(!(strlen(user->nick) && strlen(user->user))){
        constr_reply(ERR_NOTREGISTERED, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        
        free(seek_arg);
        return 0;
    }
    
    if(strlen(params[1]) == 0){  //no channel given
        //iterate through all channels
        pthread_mutex_lock(&lock);
        list_iterator_start(server->chanlist);
        while (list_iterator_hasnext(server->chanlist)) {
            chan = (channel *)list_iterator_next(server->chanlist);
            pthread_mutex_unlock(&lock);
            send_names(server, chan, user);
            pthread_mutex_lock(&lock);
        }
        list_iterator_stop(server->chanlist);
        
        strcpy(antisocial, "* * :");
        //iterate through users not on any channel
        list_iterator_start(server->userlist);
        while (list_iterator_hasnext(server->userlist) && MAXMSG - strlen(antisocial) > 1) {
            someone = (person *)list_iterator_next(server->userlist);
            pthread_mutex_lock(&(someone->c_lock));
            if(list_size(someone->my_chans) == 0){
                if (!first) {
                    strcat(antisocial, " ");
                }
                first = 0;
                buff = MAXMSG - strlen(antisocial);
                strncat(antisocial, someone->nick, buff);
            }
            pthread_mutex_unlock(&(someone->c_lock));
        }
        list_iterator_stop(server->userlist);
        pthread_mutex_unlock(&lock);
        
        constr_reply(RPL_NAMREPLY, user, reply, server, antisocial);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1){
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
    }
    else{   //only give NAMES reply for one channel
        seek_arg->value = params[1];
        pthread_mutex_lock(&lock);
        chan = (channel *)list_seek(server->chanlist, seek_arg);
        pthread_mutex_unlock(&lock);
        if(chan != NULL)
            send_names(server, chan, user);
        else{
            free(seek_arg);
            return(0);
        }
    }
    
    //send RPL_ENDOFNAMES
    constr_reply(RPL_ENDOFNAMES, user, reply, server, "*");
    if(send(clientSocket, reply, strlen(reply), 0) == -1){
        perror("Socket send() failed");
        user_exit(server, user);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    free(seek_arg);
    
    return 0;
}

int chirc_handle_WHO(chirc_server *server, person *user, chirc_message params)
{
    return 0;
}

int chirc_handle_MODE(chirc_server *server, person *user, chirc_message params)
{
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    
    // member status modes
    if(params[3][0] != '\0'){
    
    return 0;
    }

    // channel modes
    seek_arg->field = CHAN;      
    seek_arg->value = params[1];   
    pthread_mutex_lock(&lock);
    channel *channelpt = (channel *)list_seek(server->chanlist, seek_arg);
    pthread_mutex_unlock(&lock);

    if(channelpt != NULL){
    

    return 0;
    }

    // user modes
    if(strcmp(params[1],user->nick)!=0){ // names don't match
        constr_reply(ERR_USERSDONTMATCH, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        return 0;
    }
    if(strpbrk(params[2], "ao") == NULL){ // not a valid mode
        constr_reply(ERR_UMODEUNKNOWNFLAG, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        return 0;
    }
    if(params[2][0] == '+'){ // add operator priv
        //strcat(user->mode, params[2][1]);
        return 0;
    }
    if(params[2][0] == '-'){
        // do something to remove the priv
        return 0;
    }
        

    return 0;
}

int chirc_handle_OPER(chirc_server *server, person *user, chirc_message params)
{
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    
    if (strcmp(params[2], server->pw)!=0) {
	constr_reply(ERR_PASSWDMISMATCH, user, reply, server, params[0]);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        return 0;
    }
    else {
        // give the person operator power first
        if(strchr(user->mode, (int)'@') == NULL) // check to make sure not already operator
            strcat(user->mode, "@");

        // then send them their message
        constr_reply(RPL_YOUREOPER, user, reply, server, NULL);
        pthread_mutex_lock(&(user->c_lock));
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            user_exit(server, user);
        }
        pthread_mutex_unlock(&(user->c_lock));
        return 0;
    }

    return 0;
}


int chirc_handle_UNKNOWN(chirc_server *server,  //current server
                         person *user,          //current user
                         chirc_message params)  //message received
{
    char reply[MAXMSG];
    int clientSocket = user->clientSocket;
    constr_reply(ERR_UNKNOWNCOMMAND, user, reply, server, params[0]);
    
    pthread_mutex_lock(&(user->c_lock));
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        user_exit(server, user);
    }
    pthread_mutex_unlock(&(user->c_lock));
    
    return 0;
}
