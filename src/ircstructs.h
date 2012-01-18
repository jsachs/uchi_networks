//
//  ircstructs.h
//  
//

//definitions for userlist seek function
#define NICK 		0
#define USER 		1
#define FULLNAME 	2
#define ADDRESS		3
#define FD			4

#define MAXMSG 512
#define MAXPARAMS 16

typedef struct {
	char *pw; //operator password
    char *servername; //canonical name of server
    char *port; //port we're listening on
<<<<<<< HEAD
=======
    char *version;
    char *birthday;
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
    list_t *userlist;
    list_t *chanlist;
} chirc_server;

typedef struct {
	list_t userlist;
} chirc_user;

<<<<<<< HEAD
typedef char chirc_message[16][511];
=======
typedef char chirc_message[MAXPARAMS][MAXMSG-1];
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc

//element of userlist
typedef struct {
	int   clientSocket;
	char  nick[MAXMSG];
	char  user[MAXMSG];
	char  fullname[MAXMSG];
	char* address;
} person;

//parameter for seeker function
typedef struct{
	int field;
	int fd;
	char *value;
} el_indicator;
<<<<<<< HEAD
/*
struct serverArgs
{
	char *port;
	char *passwd;
};
*/
struct workerArgs
=======


typedef struct
{
	chirc_server *server;
} serverArgs;

typedef struct
>>>>>>> b48a25489f858a23ceb793a1adeb7e0651365fbc
{
	chirc_server *server;
    char *clientname;
	int socket;
} workerArgs;



