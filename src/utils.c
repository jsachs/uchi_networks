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
    int replcode = atoi(code);
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
        case 1:  // 001
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, ":Welcome to the Internet Relay Network %s!%s@%s", nick, user, msg_clnt);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 2:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, ":Your host is %s running version %s", servname, version);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 3:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, ":This server was created %s", server->birthday);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 4:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s %s ao mtov", servname, version); 
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 251:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, ":There are %s users and 0 services on 1 servers", extra);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 252:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s :operator(s) online", extra);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 253:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s :unknown connection(s)", extra);
            //sprintf(replmsg, "0 :unknown connection(s)");
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 254:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s :channels formed", extra);
            //sprintf(replmsg, "0 :channels formed");
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 255:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, ":I have %s clients and 1 servers", extra);
            //sprintf(replmsg, ":I have 0 clients and 1 servers");
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 311:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s", extra);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 312:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s", extra);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 318: // RPL_ENDWHOIS
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s :End of WHOIS list", nick);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 375:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, ":- %s Message of the day - ", servname);
           // pthread_mutex_unlock(&(client->c_lock));
            break;
        case 372:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, ":- %s", extra);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 376:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, ":- End of MOTD command");
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 401:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s :No such nick/channel", extra);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 421:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s :Unknown command", extra);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 422:
            //pthread_mutex_lock(&(client->c_lock));
            strcpy(replmsg, ":MOTD File is missing");
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 433:
            //pthread_mutex_lock(&(client->c_lock));
            sprintf(replmsg, "%s :Nickname is already in use", extra);
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        case 462:
            //pthread_mutex_lock(&(client->c_lock));
            strcpy(replmsg, ":Unauthorized command (already registered)");
            //pthread_mutex_unlock(&(client->c_lock));
            break;
        
        default:
            break;
    }
    //pthread_mutex_lock(&(client->c_lock));
    snprintf(reply, MAXMSG - 2, "%s %s %s %s", prefix, code, nick, replmsg);
    strcat(reply, "\r\n");
    //pthread_mutex_unlock(&(client->c_lock));
    return;
}

void do_registration(person *client, chirc_server *server){
    int i;
    int clientSocket = client->clientSocket;
    char reply[MAXMSG];
    char *replies[9] = {RPL_WELCOME,
                        RPL_YOURHOST,
                        RPL_CREATED,
                        RPL_MYINFO,
                        /*
                        RPL_LUSERCLIENT,
                        RPL_LUSEROP,
                        RPL_LUSERUNKNOWN,
                        RPL_LUSERCHANNELS,
                        RPL_LUSERME*/};
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
    person *client = (person *)el;
    el_indicator *el_info;
    el_info = (el_indicator *)indicator;
    int field = el_info->field;
    char *value;
    int fd;
    if (field == 4)
        fd = el_info->fd;
    else
        value = el_info->value;
    if ((field != 4 && strlen(value) == 0) || field < 0 || field > 4){
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
        default:
            return 0;
            break;
    }
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
