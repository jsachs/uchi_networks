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

void constr_reply(char code[4], person *client, char *reply){ 
    char *nick;
    int replcode = atoi(code);
    char replmsg[MAXMSG];
    //char prefix[MAXMSG];
    char preset[MAXMSG];
    char *user = client->user;
    char *msg_clnt = client->address;
    //prefix[0] = ':';
    //strcpy(prefix + 1, servername);
    char *prefix = ":foo.example.com"; // yes, this is hand-wavey. we will get server name later
    switch (replcode){
        case 1:  // 001
            strcpy(preset, ":Welcome to the Internet Relay Network");
            sprintf(replmsg, "%s %s!%s@%s", preset, nick, user, msg_clnt);
            break;
        default:
            break;
    }
    snprintf(reply, MAXMSG - 2, "%s %s %s %s", prefix, code, nick, replmsg);
    strcat(reply, "\r\n");
    return;
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
            return (client->fd == fd)?1:0;
        default:
            return 0;
            break;
    }
}