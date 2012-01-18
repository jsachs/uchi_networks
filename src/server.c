/*
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *
 *  server code for chirc project
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

extern list_t userlist, chanlist;

void *service_single_client(void *args)
{
	workerArgs *wa;
	int socket, i;

	chirc_server *ourserver;
	char buffer[100];
    char *clientname;
    person client = {-1, NULL, NULL, NULL, NULL};
    
	wa = (workerArgs*) args;
	socket = wa->socket;
	ourserver = wa->server;
    clientname = wa->clientname;
    client.fd = socket;
    client.address = clientname;
    list_append(ourserver->userlist, &client);

    
	pthread_detach(pthread_self());
	
	// parse_message(socket);
	char buf[MAXMSG + 1];
    char msg[MAXMSG - 1];     // max length 510 characters + \0
    char *msgstart, *msgend;  // tools to keep track of beginning/end of message
    int remaind;              // everything in buffer that's after the \r\n
    int msglength = 0;        // keeps track of everything send into message
    int morelength = 0;       // how much is going to be sent from remainder to message
    int nbytes = 0;           // used in constructing message to be sent
    int truncated = 0;        // used to track occurence of the end of a truncated message
    int CRLFsplit = 0;        // tracks the \r\n in difference message receptions
	
	memset(msg, '\0', MAXMSG - 1);
	
	while (1) {
        msgstart = buf;
        if ((nbytes = recv(clientSocket, buf, MAXMSG, 0)) == -1) {
            perror("ERROR: recv failure");
            close(clientSocket);
            pthread_exit(NULL);
        }
        if(nbytes == 0){
            printf("Connection closed by client\n");
            close(clientSocket);
            pthread_exit(NULL);
        }
        buf[nbytes] = '\0';
        if(CRLFsplit){      // procedure to deal with \r\n split across messages
            CRLFsplit = 0;
            if (buf[0] == '\n') {
                msgstart = buf + 1;
                if (msglength > 0) {
                    msg[msglength - 2] = '\0';
                    parse(msg, clientSocket);
                    memset(msg, '\0', MAXMSG - 1);
                    msglength = 0;
                }
            }
        }
        if (truncated) {
            //everything up to the first CRLF is the end of a truncated message and should be ignored.
            //if there's no CRLF we need to keep reading in until we find one.
            if((msgstart = strstr(buf, "\r\n") + 2) == NULL)
                continue;
            else
                truncated = 0;
        }
        //get and parse as many complete messages as buf contains
        while((msgend = strstr(msgstart, "\r\n")) != NULL){
            morelength = msgend - msgstart;
            if(msglength + morelength > MAXMSG - 2)  //msg is too long
                msgstart[MAXMSG - msglength] = '\0'; //terminate message at max allowed characters
            else
                *msgend = '\0';                      //terminate message at CRLF
            strcat(msg, msgstart);
            msgstart = msgend + 2;
            parse(msg, clientSocket);
            memset(msg, '\0', MAXMSG - 1);
            msglength = 0;
            if(msgstart == NULL)
                continue;
        }
        
        //deal with remainder of recv'd input
        morelength = strlen(msgstart);
        remaind = MAXMSG - msglength - 1; //i.e. how many more chars can fit in msg, including null terminator
        if (msgstart[morelength - 1] == '\r') 
            //CRLF might be split between two packets--we'll need to check when we read in the next one
            CRLFsplit = 1;
        if (morelength <= remaind){
            strcat(msg, msgstart);
            msglength = strlen(msg);
        }
        else {
            msgstart[remaind] = '\0';
            strcat(msg, msgstart);
            parse(msg, clientSocket);
            memset(msg, '\0', MAXMSG - 1);
            msglength = 0;
            truncated = 1;
        }
    }
	
	
	close(socket);
	pthread_exit(NULL);
}

void parse(char *msg, int clientSocket) {
    chirc_message params; // params[0] is command
    int counter = 0;
    int paramcounter = 0;
    int paramnum = 0;
    char reply[MAXMSG];
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    
    //may want to modify this to get by pthread id, not fd
    seek_arg->field = FD;
    seek_arg->fd = clientSocket;
    person *clientpt = (person *)list_seek(&userlist, seek_arg);
    // process to break a message into its component commands/parameters
    // potentially clean this up to ultilize strtok() at some point
    while(msg[counter] != '\0'){
        if (msg[counter] == ' '){
            params[paramnum][paramcounter] = '\0';
            paramnum++;
            paramcounter = 0;
        }
        else if(msg[counter] == ':' && paramcounter == 0){
            strcpy(params[paramnum], &msg[counter]);
            break;
        }
        else{
            params[paramnum][paramcounter] = msg[counter];
            paramcounter++;
        }
        counter++;
    }
    
    // use this section to pass parameters to handler
    
}

