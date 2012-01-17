//
//  ircstructs.h
//  
//

#ifndef _structs_h
#define _structs_h

typedef struct {
	char *pw; //operator password
    char *servername; //canonical name of server
    char *port; //port we're listening on
    list_t *userlist;
    list_t *chanlist;
} chirc_server;

typedef struct {
	list_t userlist;
} chirc_user;

typedef char chirc_message[16][511];

//element of userlist
typedef struct {
	int   fd;
	char* nick;
	char* user;
	char* fullname;
	char* address;
} person;

//parameter for seeker function
typedef struct{
	int field;
	int fd;
	char *value;
} el_indicator;
/*
 struct serverArgs
 {
 char *port;
 char *passwd;
 };
 */
struct workerArgs
{
	int socket;
};



//definitions for userlist seek function
#define NICK 		0
#define USER 		1
#define FULLNAME 	2
#define ADDRESS		3
#define FD			4



#endif
