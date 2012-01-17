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

/*
struct handler_entry handlers[] = {
    HANDLER_ENTRY (NICK),
    HANDLER_ENTRY (USER),
    HANDLER_ENTRY (QUIT),
    
    NULL_ENTRY
}
*/

void constr_reply(char code[4], person *client, char *param);
void do_registration(person *client, chirc_server *server);


int chirc_handle_NICK(chirc_server  *server, // current server
                      person    *user,   // current user
                      chirc_message msg     // message to be sent
                      )
{
    char reply[MAXMSG];
    char *newnick;
    int fd = user->fd;
    newnick = msg[1];
	if (user->nick){
        user->nick = newnick;
        // constr_reply(/*whatever*/);
    }
    else{
        user->nick = newnick;
        if (user->user)
            do_registration(user, server);
    }
    return 0;
}

int chirc_handle_USER(chirc_server  *server, // current server
                      person    *user,   // current user
                      chirc_message msg     // message to be sent
                      )
{
	char reply[MAXMSG];
    int fd = user->fd;
    char *username = msg[1];
    char *fullname = msg[4];
    if (user->user && user->nick){
        constr_reply(ERR_ALREADYREGISTRED, user, reply);
        if(send(fd, reply, strlen(reply), 0) == -1)
        {
            perror("Socket send() failed");
            close(fd);
            pthread_exit(NULL);
        }
    }
    else{
        user->user = username;
        user->fullname = fullname;
        if(user->nick)
            do_registration(user, server);
    }
    return 0;
}

int chirc_handle_QUIT(chirc_server  *server, // current server
                      person    *user,   // current user
                      chirc_message msg     // message to be sent
                      )
{
	/* some code I guess... */
}
