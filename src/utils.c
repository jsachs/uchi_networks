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

void constr_reply(char code[4], person *client, char *reply, chirc_server *server) {
    int replcode = atoi(code);
    char replmsg[MAXMSG];
    char preset[MAXMSG];
    char *servname = server->servername;
    char *version = server->version;
    char prefix[MAXMSG] = ":";
    strcat(prefix, servname);
    char *nick = client->nick;
    char *user = client->user;
    char *msg_clnt = client->address;
    switch (replcode){
        case 1:  // 001
            sprintf(replmsg, ":Welcome to the Internet Relay Network %s!%s@%s", nick, user, msg_clnt);
            break;
        case 2:
            sprintf(replmsg, ":Your host is %s running version %s", servname, version);
            break;
        case 3:
            sprintf(replmsg, ":This server was created %s", server->birthday);
            break;
        case 4:
            sprintf(replmsg, "%s %s ao mtov", servname, version); 
            break;
        case 251:
            strcpy(replmsg, ":There are 1 users and 0 services on 1 servers");
            break;
        case 252:
            strcpy(replmsg, "0 :operator(s) online");
            break;
        case 253:
            strcpy(replmsg, "0 :unknown connection(s)");
            break;
        case 254:
            strcpy(replmsg, "0 :channels formed");
            break;
        case 255:
            strcpy(replmsg, ":I have 1 clients and 1 servers");
            break;
        case 422:
             strcpy(replmsg, ":MOTD File is missing");
             break;
        case 462:
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
    char *replies[9] = {RPL_WELCOME,
                        RPL_YOURHOST,
                        RPL_CREATED,
                        RPL_MYINFO,
                        RPL_LUSERCLIENT,
                        RPL_LUSEROP,
                        RPL_LUSERUNKNOWN,
                        RPL_LUSERCHANNELS,
                        RPL_LUSERME};
    for (i = 0; i < 9; i++){
        constr_reply(replies[i], client, reply , server);
        if(send(clientSocket, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(clientSocket);
            pthread_exit(NULL);
        }
    }
    
    //later there will be more to MOTD stuff than this
    constr_reply(ERR_NOMOTD, client, reply, server);
    if(send(clientSocket, reply, strlen(reply), 0) == -1)
    {
        perror("Socket send() failed");
        close(clientSocket);
        pthread_exit(NULL);
    }
    
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
    if (value == NULL || field < 0 || field > 4){
        perror("bad argument to fun_seek");
        return 0;
    }
    switch (field) {
        case 0:
            return (client->nick == value)?1:0;
            break;
        case 1:
            return (client->user == value)?1:0;
            break;
        case 2:
            return (client->fullname == value)?1:0;
            break;
        case 3:
            return (client->address == value)?1:0;
            break;
        case 4:
            return (client->clientSocket == fd)?1:0;
        default:
            return 0;
            break;
    }
}