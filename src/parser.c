/* 
 *
 *  CMSC 23300 / 33300 - Networks and Distributed Systems
 *  
 *  A very simple socket client. Reads at most 100 bytes from a socket and quits.
 *  
 *  Written by: Borja Sotomayor
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
#include "reply.h"
#include "simclist.h"
#include "ircstructs.h"

#define MAXMSG 512

void parse(char *msg, int clientSocket, int serverSocket);
void constr_reply(char code[4], char *nick, char *param);

extern list_t userlist, chanlist;

//parse incoming data into messages, to deal with as needed
void parse_message(int clientSocket, int serverSocket)
{
    char buf[MAXMSG + 1];
    char msg[MAXMSG - 1]; //max length 510 characters + \0
    char *msgstart, *msgend;
    int remaind;
    int msglength = 0;
    int morelength = 0;
    int nbytes = 0;
    int truncated = 0;
    int CRLFsplit = 0;
    
    memset(msg, '\0', MAXMSG - 1);
    
	while (1) {
        msgstart = buf;
        if ((nbytes = recv(clientSocket, buf, MAXMSG, 0)) <= 0) {
            perror("ERROR: recv failure");
            exit(-1);
        }
        buf[nbytes] = '\0';
        if(CRLFsplit){
            CRLFsplit = 0;
            if (buf[0] == '\n') {
                msgstart = buf + 1;
                if (msglength > 0) {
                    msg[msglength - 2] = '\0';
                    parse(msg, clientSocket, serverSocket);
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
            parse(msg, clientSocket, serverSocket); //not sure yet where usr will come from
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
            parse(msg, clientSocket, serverSocket);
            memset(msg, '\0', MAXMSG - 1);
            msglength = 0;
            truncated = 1;
        }
    }
}

void parse(char *msg, int clientSocket, int serverSocket) {
    char params[16][511]; //param[0] is command
    int counter = 0;
    int paramcounter = 0;
    int paramnum = 0;
    char reply[MAXMSG];
    person *clientpt = (person *)list_get_at(&userlist, 0);

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
                if(send(clientSocket, reply, strlen(reply), 0) <= 0)
                {
                    perror("Socket send() failed");
                    close(serverSocket);
                    close(clientSocket);
                    exit(-1);
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
                if(send(clientSocket, reply, strlen(reply), 0) <= 0)
                {
                    perror("Socket send() failed");
                    close(serverSocket);
                    close(clientSocket);
                    exit(-1);
                }
            }   
        }
    }
    else{
        //error message: invalid command
    }
}


void constr_reply(char code[4], char *nick, char *reply){  // maybe this should take a person as an argument?
    person *clientpt = (person *)list_get_at(&userlist, 0);
    int replcode = atoi(code);
    char replmsg[MAXMSG];
    //char prefix[MAXMSG];
    char *preset = malloc(512);
    char *user = clientpt->user;
    char *msg_clnt = clientpt->address;
    //prefix[0] = ':';
    //strcpy(prefix + 1, servername);
    char *prefix = ":foo.example.com";
    switch (replcode){
        case 1:  // 001
            preset  = ":Welcome to the Internet Relay Network";
            sprintf(replmsg, "%s %s!%s@%s", preset, nick, user, msg_clnt);
            break;
        default:
            break;
    }
    snprintf(reply, MAXMSG - 2, "%s %s %s %s", prefix, code, nick, replmsg);
    strcat(reply, "\r\n");
}




