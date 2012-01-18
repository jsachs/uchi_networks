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
    char *version;
    char *birthday;
    list_t *userlist;
    list_t *chanlist;
} chirc_server;

typedef struct {
	list_t userlist;
} chirc_user;

typedef char chirc_message[MAXPARAMS][MAXMSG-1];

//element of userlist
typedef struct {
	int   clientSocket;
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

<<<<<<< HEAD
struct serverArgs
=======
typedef struct
>>>>>>> e45d50ade95c2bd94215e6758b2c0dac97174748
{
	chirc_server *server;
} serverArgs;

typedef struct
{
	chirc_server *server;
    char *clientname;
	int socket;
} workerArgs;



