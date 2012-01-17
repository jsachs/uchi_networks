//
//  structs.h
//  
//
//  Created by Nora on 1/9/12.
//  Copyright 2012 __MyCompanyName__. All rights reserved.
//

#ifndef _structs_h
#define _structs_h

typedef struct {
	
} chirc_server;

typedef struct {
	list_t userlist;
} chirc_user;

typedef struct {
	char *msg;
} chirc_message;

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

struct serverArgs
{
	char *port;
	char *passwd;
};

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
