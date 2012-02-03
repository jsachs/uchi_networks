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
#define CHAN        5


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
    unsigned int numregistered;
} chirc_server;
 
typedef char chirc_message[MAXPARAMS][MAXMSG-1];

//element of userlist
typedef struct {
	int   clientSocket;
	char  nick[MAXMSG];
	char  user[MAXMSG];
	char  fullname[MAXMSG];
	char* address;
        char mode[5];
        char away[MAXMSG];  //away message
	pthread_mutex_t c_lock;
       list_t *my_chans;   //list of mychan structs
       pthread_t tid;
} person;

//parameter for seeker function
typedef struct{
	int field;
	int fd;
	char *value;
} el_indicator;


//pass to server threads
typedef struct
{
	chirc_server *server;
} serverArgs;

//pass to thread to handle each client
typedef struct
{
    chirc_server *server;
    char *clientname;
    int socket;
} workerArgs;

typedef struct {
    char name[MAXMSG];
    char topic[MAXMSG];
    char mode[5];
    int numusers;
    pthread_mutex_t chan_lock;
} channel;

typedef struct {
    char name[MAXMSG];  //name of channel
    char mode[5];       //member status mode 
} mychan;





