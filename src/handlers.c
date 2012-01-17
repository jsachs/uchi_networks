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

struct handler_entry handlers[] = {
				HANDLER_ENTRY (NICK),
				HANDLER_ENTRY (USER),
				HANDLER_ENTRY (QUIT),
				
				NULL_ENTRY
				}

int chirc_handle_NICK(chirc_server  *server, // current server
						 chirc_user    *user,   // current user
						 chirc_message *msg     // message to be sent
{
	/* some code I guess... */
}

int chirc_handle_USER(chirc_server  *server, // current server
						 chirc_user    *user,   // current user
						 chirc_message *msg     // message to be sent
{
	/* some code I guess... */
}

int chirc_handle_QUIT(chirc_server  *server, // current server
						 chirc_user    *user,   // current user
						 chirc_message *msg     // message to be sent
{
	/* some code I guess... */
}
