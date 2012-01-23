/* 
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *  
 * sachs_sandler 
 * 
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
#include <errno.h>
#include <pthread.h>
#include "reply.h"
#include "simclist.h"
#include "ircstructs.h"

#define MAXMSG 512

extern pthread_mutex_t lock;
extern pthread_mutex_t loglock;

void parse(char *msg, int clientSocket, chirc_server *server);
void constr_reply(char code[4], person *nick, char *param);
void handle_chirc_message(chirc_server *server, person *user, chirc_message params);

//parse incoming data into messages, to deal with as needed
void parse_message(int clientSocket, chirc_server *server)
{
    char buf[MAXMSG + 1];
    char msg[MAXMSG - 1];     // max length 510 characters + \0
    char *msgstart, *msgend;  // tools to keep track of beginning/end of message
    int remaind;              // everything in buffer that's after the \r\n
    int msglength = 0;        // keeps track of everything send into message
    int morelength = 0;       // how much is going to be sent from remainder to message
    int nbytes = 0;           // used in constructing message to be sent
    int truncated = 0;        // used to track occurence of the end of a truncated message
    int CRLFsplit = 0;        // tracks the \r\n in difference message receptions
    char logerror[MAXMSG];
    
    memset(msg, '\0', MAXMSG - 1);
    
	while (1) {
        msgstart = buf;
        
        el_indicator *seek_arg = malloc(sizeof(el_indicator));
        
        seek_arg->field = FD;
        seek_arg->fd = clientSocket;
        
        pthread_mutex_lock(&lock);
        person *clientpt = (person *)list_seek(server->userlist, seek_arg);
        pthread_mutex_unlock(&lock);
        
        if ((nbytes = recv(clientSocket, buf, MAXMSG, 0)) == -1) {
            pthread_mutex_lock(&loglock);
            sprintf(logerror, "recv from socket %d failed with errno %d\n", clientSocket, errno);
            logprint(NULL, server, logerror);
            pthread_mutex_unlock(&loglock);
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, clientpt);
            pthread_mutex_unlock(&lock);
            close(clientSocket);
            pthread_exit(NULL);
        }
        
        if(nbytes == 0){
            printf("Connection closed by client\n");
            pthread_mutex_lock(&lock);
            list_delete(server->userlist, clientpt);
            pthread_mutex_unlock(&lock);
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
                    parse(msg, clientSocket, server);
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
            parse(msg, clientSocket, server);
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
            parse(msg, clientSocket, server);
            memset(msg, '\0', MAXMSG - 1);
            msglength = 0;
            truncated = 1;
        }
    }
}


void parse(char *msg, int clientSocket, chirc_server *server) {
    chirc_message params; // params[0] is command
    int i;
    int counter = 0;
    int paramcounter = 0;
    int paramnum = 0;
    char reply[MAXMSG];
    el_indicator *seek_arg = malloc(sizeof(el_indicator));
    
    seek_arg->field = FD;
    seek_arg->fd = clientSocket;
    
    pthread_mutex_lock(&lock);
    person *clientpt = (person *)list_seek(server->userlist, seek_arg);
    pthread_mutex_unlock(&lock);

    //note in log structure what message is and where it came from
    strcpy(clientpt->tolog->msgin, msg);
    strcpy(clientpt->tolog->userin, clientpt->nick);
    
    // process to break a message into its component commands/parameters
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
    
    handle_chirc_message(server, clientpt, params);
    
}

