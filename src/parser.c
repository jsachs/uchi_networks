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

void parse_message(int clientSocket);
void parse(char *msg, int clientSocket);
void constr_reply(char code[4], char *nick, char *param);

extern list_t userlist, chanlist;

//parse incoming data into messages, to deal with as needed
void parse_message(int clientSocket)
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
    // Begin identifying possible commands
    // this may become a separate function/module in the future
    if(strcmp(params[0], "NICK") == 0){
        if (clientpt->nick) {
            clientpt->nick = params[1];
            //constr_reply(/*reply to change nicknames*/);
            //and send the reply
        }
        else{
            clientpt->nick = params[1];
            if(clientpt->user){
                constr_reply("001", clientpt->nick, reply);
                if(send(clientSocket, reply, strlen(reply), 0) == -1)
                {
                    perror("Socket send() failed");
                    close(clientSocket);
                    pthread_exit(NULL);
                }
            }
        }
    }
    else if(strcmp(params[0], "USER") == 0){
        if (clientpt->user && clientpt->nick) {
            //error message: already registered
        }
        else{
            clientpt->user     = params[1];
            clientpt->fullname = params[4];
            if(clientpt->nick){
                constr_reply("001", clientpt->nick, reply);
                if(send(clientSocket, reply, strlen(reply), 0) == -1)
                {
                    perror("Socket send() failed");
                    close(clientSocket);
                    pthread_exit(NULL);
                }
            }   
        }
    }
    else
    {
        //error message: invalid command
    }
}

void constr_reply(char code[4], person *client, char *reply){ 
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





