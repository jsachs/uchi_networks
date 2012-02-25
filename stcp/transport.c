/*
 * transport.c 
 *
 * CS244a HW#3 (Reliable Transport)
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file. 
 *
 */


#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <netinet/in.h>
#include <unistd.h>
#include <time.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"
#include "simclist.h"

#define DEBUG(x, args...) printf(x, ## args)
#define max(a, b)  ( (a > b) ? (a) : (b) )
#define min(a, b)  ( (a < b) ? (a) : (b) )

#define MAXLEN 536        /* maximum payload size */
#define MAXOPS 40         /* maximum number of bytes for options */
#define OFFSET 5          /* standard offset with no options */
#define HEADERSIZE 20     /* standard size of an STCP header */
#define WINLEN 3072       /* fixed window size */
#define MAX_INIT_SEQ 256  /* maximum initial sequence number (+1) */
#define WORDSIZE 4        /* defines 4-byte words for use with offset */

/* macros for RTO */
#define ALPHA 0.125
#define BETA  0.25
#define K     4
#define G     0.1


enum { CSTATE_ESTABLISHED,
       CSTATE_SYN_SENT,
       CSTATE_SYN_RECEIVED,
       CSTATE_FIN_WAIT1,
       CSTATE_FIN_WAIT2,
       CSTATE_CLOSE_WAIT,
       CSTATE_LAST_ACK,
       CSTATE_CLOSING,
       CSTATE_CLOSED };


/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */
    
    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;
    
    /* these sequence numbers serve as "pointers" to the data sent and receieved */
    tcp_seq send_unack;
    tcp_seq send_next;
    tcp_seq send_wind;
    tcp_seq recv_next;
    tcp_seq recv_wind;
    
    /* the recv_buffer is supplemented by an indicator buffer
     * of 1s and NULLs to track where data exists in the recv_buffer */
    char recv_buffer[WINLEN];
    char recv_indicator[WINLEN];
    
    /* variables for timeout operations */
    struct timespec rto;
    struct timespec srtt;
    long   rttvar_sec;
    long   rttvar_nsec;
    
    /* this linked list servers as our send buffer */
    list_t *unackd_packets;
    
} context_t;

typedef struct
{
    struct timespec start_time;
    int             retry_count;
    tcp_seq         seq_num;
    
    void *packet;
    int   packet_size;
    
} packet_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);
static void packet_t_create(context_t *ctx, void *packet, int packetsize);
static void packet_t_remove(context_t *ctx);
static STCPHeader * make_stcp_packet(uint8_t flags, tcp_seq seq, tcp_seq ack, int len);
int recv_packet(mysocket_t sd, context_t *context, void *recvbuff, size_t buffsize, STCPHeader *header);

static void update_rto(context_t *ctx, packet_t *packet);


/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;
    uint8_t flags;
    int tcplen;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);

    /* ensures this array begins as entirely null characters */
    memset(ctx->recv_indicator, '\0', WINLEN);
    
    char buffer[sizeof(STCPHeader) + MAXLEN];
    
    generate_initial_seq_num(ctx);
    
    ctx->connection_state = CSTATE_CLOSED;
    list_t unackd;
    ctx->unackd_packets = &unackd;
    list_init(ctx->unackd_packets);
    
    /* XXX: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.  after the handshake completes, unblock the
     * application with stcp_unblock_application(sd).  you may also use
     * this to communicate an error condition back to the application, e.g.
     * if connection fails; to do so, just set errno appropriately (e.g. to
     * ECONNREFUSED, etc.) before calling the function.
     */
    if(is_active)
    {
        ctx->send_unack = ctx->initial_sequence_num;
        
        STCPHeader *header = make_stcp_packet(TH_SYN, ctx->send_unack, 0, 0);
        /* first step in the handshake: sending a SYN packet */
        tcplen = stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
        ctx->send_next = ctx->send_unack;
        /* this ensures the SYN packet will be resent if it is dropped/times out */
        packet_t_create(ctx, header, sizeof(STCPHeader));
        DEBUG("Sent SYN packet with seq number %d\n", ntohl(header->th_seq));
        
        if(tcplen < 0) {
            DEBUG("SYN send failed");
            errno = ECONNREFUSED;
        }
        else
        {
            ctx->send_next++;
            ctx->connection_state = CSTATE_SYN_SENT;
            /* the client now waits for a SYN/ACK */
            while(ctx->connection_state == CSTATE_SYN_SENT)
            {
                unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
                
                if(event & NETWORK_DATA)
                {
                    /* we now expect the next packet to be a SYN-ACK */
                    tcplen = recv_packet(sd, ctx, NULL, 0,  header);
                    flags = (TH_SYN|TH_ACK);
                    if( tcplen < 0 || !((header->th_flags & TH_SYN)&&(header->th_flags & TH_ACK))) {
                        DEBUG("Did not receive SYN/ACK\n");
                        continue;
                    }
                    else
                    {
                        DEBUG("packet received with seq %d and ack %d\n", ntohl(header->th_seq), ntohl(header->th_ack));
                        if(ntohl(header->th_ack) == ctx->send_next){
                            header = make_stcp_packet(TH_ACK, ctx->send_next, ctx->recv_next, 0);
                            tcplen = stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
                            /* the client now sends an ACK packet, and goes to the ESTABLISHED state */
                            DEBUG("Sent ACK packet with seq number %d\n", ntohl(header->th_seq));
                            if(tcplen < 0) {
                                DEBUG("ACK send failed");
                                errno = ECONNABORTED;
                                break;
                            }
                            ctx->connection_state = CSTATE_ESTABLISHED;
                            free(header);
                            header = NULL;
                        } 
                    }
                }
            }
        }
    }
    if(!is_active)
    {
        /* wait for a SYN packet */
        while(ctx->connection_state == CSTATE_CLOSED)
        {
            ctx->send_unack = ctx->initial_sequence_num;
            ctx->send_next = ctx->send_unack;
            unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA|TIMEOUT, NULL);
            if(event & NETWORK_DATA){
                STCPHeader *header = (STCPHeader *) malloc(sizeof(STCPHeader));
                tcplen = recv_packet(sd, ctx, NULL, 0, header);
                if(tcplen < 0 || header->th_flags != TH_SYN) {
                    DEBUG("Did not receive SYN packet\n");
                    continue;
                }
                else
                {
                    DEBUG("Received SYN with seq number %d\n", ntohl(header->th_seq));
                    ctx->connection_state = CSTATE_SYN_RECEIVED;
                    header = make_stcp_packet(TH_SYN|TH_ACK, ctx->send_unack, ctx->recv_next, 0);
                    tcplen = stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
                    /* send out a SYN/ACK packet to the initiating client */
                    DEBUG("SYN/ACK sent with ack number %d and seq number %d\n", htonl(header->th_ack), htonl(header->th_seq));
                    if(tcplen < 0){
                        DEBUG("SYN/ACK send failed");
                        errno = ECONNABORTED;
                        break;
                    }
                    /* ensures the SYN-ACK will be resent if it is not ACK'd */
                    packet_t_create(ctx, header, sizeof(STCPHeader));
                    ctx->send_next++;
                }
            }
        }
        while(ctx->connection_state == CSTATE_SYN_RECEIVED)
        {
            unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA|TIMEOUT, NULL);
            if(event & NETWORK_DATA){
            	 STCPHeader *header = (STCPHeader *) buffer;
                tcplen = recv_packet(sd, ctx, NULL, 0, header);
                if(tcplen < 0 || !(header->th_flags & TH_ACK)) {
                    DEBUG("Did not receive ACK packet\n");
                    continue;
                }
                /* if it's the right ACK packet, go to the ESTABLISHED state */
                if(ntohl(header->th_ack) == ctx->send_next) {
                    DEBUG("Received ACK with ack number %d\n", ntohl(header->th_ack));
                    ctx->connection_state = CSTATE_ESTABLISHED;
                }
            }
        }
    }
    if(ctx->connection_state == CSTATE_ESTABLISHED)
		DEBUG("State: seq %d ack %d send window %d\n", ctx->send_next, ctx->recv_next, ctx->send_wind);
    
    
    /* unblocks the application */
    /* relays possible error conditions */
    stcp_unblock_application(sd);
    
    control_loop(sd, ctx);
    
    /* do any cleanup here */
    free(ctx);
}


/* generate random initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);
    
#ifdef FIXED_INITNUM
    /* please don't change this! */
    ctx->initial_sequence_num = 1;
#else
    /* randomizes the seed for the rand function */
    /* reduced the rand value mod 256 to get [0,255] as possible values */
    unsigned int rand_seed = (unsigned int)time(NULL) + getpid();
	srand (rand_seed);
    ctx->initial_sequence_num = rand()%MAX_INIT_SEQ;
#endif
}


/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    assert(ctx);
    int tcplen;
    char payload[MAXLEN];
    STCPHeader *in_header;
    void *packtosend;
    char *indicpt;
    char tempbuff[WINLEN];
    stcp_event_type_t eventflag;
    
    while (!ctx->done)
    {
        unsigned int event;
        
        eventflag = ANY_EVENT;
        
        /* set timeout if there is any unack'd data; if not, just make it NULL */
        struct timespec timestart;
        struct timespec *timeout;


        if (ctx->send_unack < ctx->send_next){ 
            timestart = ((packet_t *)list_get_at(ctx->unackd_packets, 0))->start_time;
            timeout = &timestart;
            
            /* constructs the timeout with absolute time by adding the RTO */
            timeout->tv_sec += ctx->rto.tv_sec;
            timeout->tv_nsec += ctx->rto.tv_nsec;
	        
            /* ensures no nanosecond overflow */
            if (timeout->tv_nsec >= 1000000000) {
                timeout->tv_nsec -= 1000000000;
                timeout->tv_sec  += 1;
            }
            if (timeout->tv_sec <= time(NULL))
            /*earliest unacked packet has timed out, unless it's currently sitting in buffer */
                eventflag = NETWORK_DATA|TIMEOUT;
        }
        else {    
            timeout = NULL;
            DEBUG("No timeout set\n");
        } 

        
        
        /* see stcp_api.h or stcp_api.c for details of this function */
        /* XXX: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, eventflag, timeout);
        
        /* check whether it was the network, app, or a close request */
        if (event & APP_DATA)
        {
            DEBUG("Application Data\n");
            /* the application has requested that data be sent */
            /* we should send as long as send_wind > 0 */
	    int open_window = ctx->send_wind - (ctx->send_next - ctx->send_unack);
            if(open_window > 0)
            {
                int send_size;
                
                /* only read in as much from app as we can send */
                if(open_window > MAXLEN) send_size = MAXLEN;
				else send_size = open_window;
                
                void* buffer = (char *) calloc(1, send_size);
                
                tcplen = stcp_app_recv(sd, buffer, send_size);
                
                if( tcplen > 0 ) {
                    DEBUG("Application data size: %d\n", tcplen);
                    /*create and send packet*/
                    packtosend = (void *)make_stcp_packet(TH_ACK, ctx->send_next, ctx->recv_next, tcplen);
                    memcpy(packtosend + sizeof(STCPHeader), buffer, tcplen);
                    stcp_network_send(sd, packtosend, sizeof(STCPHeader) + tcplen, NULL);
                    DEBUG("Packet of payload size %d, ack number %d, seq number %d sent to network\n", tcplen, ctx->recv_next, ctx->send_next);
                    packet_t_create(ctx, packtosend, sizeof(STCPHeader) + tcplen); /* now a packet has been pushed onto the unacked queue */
                    /* update window length and send_next */
                    ctx->send_next += tcplen;
                }
                free(buffer);
                buffer = NULL;
                free(packtosend);
                packtosend = NULL;
            }
            else DEBUG("Could not get application data\n");
        }
        
        if (event & NETWORK_DATA)
        {
            DEBUG("Network Data\n");
            in_header = malloc(sizeof(STCPHeader));
            
            /* network data transmission */
            int data_size = recv_packet(sd, ctx, payload, MAXLEN, in_header);
            
            DEBUG("Received net data size is %d\n", data_size);
            
            
            /* send ACK as long as it's not past our window, and actually contained data */
            if(data_size > 0 || data_size == -2){ /* -2 indicates it received old data, which we should not send to the application */
                packtosend = (void *)make_stcp_packet(TH_ACK, ctx->send_next, ctx->recv_next, 0);
                stcp_network_send(sd, packtosend, sizeof(STCPHeader), NULL);
                free(packtosend);
                packtosend = NULL;
            }



            /* send payload to application, if it's valid */
            if(data_size > 0){
                DEBUG("Sent data of size %d to application\n", data_size);
                stcp_app_send(sd, ctx->recv_buffer, data_size);
           
            
                /* slide the window over */
                indicpt = strchr(ctx->recv_indicator, '\0');
                data_size = indicpt - ctx->recv_indicator;
                memcpy(tempbuff, ctx->recv_buffer + data_size, WINLEN - data_size);
                memset(ctx->recv_buffer, '\0', WINLEN);
                memcpy(ctx->recv_buffer, tempbuff, WINLEN - data_size);

                /* slide window indicator over */
                memcpy(tempbuff, indicpt, WINLEN - data_size);
                memset(ctx->recv_indicator, '\0', WINLEN);
                memcpy(ctx->recv_indicator, tempbuff, WINLEN - data_size);
            }

            /* deal with connection teardown, if need be */
            if (in_header->th_flags & TH_ACK){
                /* go from FIN-WAIT1 --> FIN-WAIT2 */
                if(ctx->connection_state == CSTATE_FIN_WAIT1){
                    DEBUG("State: FIN-WAIT2\n");
                    ctx->connection_state = CSTATE_FIN_WAIT2;
                }
                /* go from LAST-ACK --> CLOSED */
                if(ctx->connection_state == CSTATE_LAST_ACK) {
                    DEBUG("State: CLOSED\n");
                    ctx->connection_state = CSTATE_CLOSED;
                    free(in_header);
                    in_header = NULL;
                    break;
                }
                /* go from CLOSING --> CLOSED */
                if(ctx->connection_state == CSTATE_CLOSING){
                    DEBUG("State: CLOSED\n");
                    ctx->connection_state = CSTATE_CLOSED;
                    free(in_header);
                    in_header = NULL;
                    break;
                }
            }
            /* branching for the receipt of a FIN packet */
            if (in_header->th_flags & TH_FIN){
                DEBUG("Received FIN packet\n");
                /* Acknowledge FIN, which counts as a byte */
                ctx->recv_next++;
                packtosend = (void *)make_stcp_packet(TH_ACK, ctx->send_next, ctx->recv_next, 0);
                stcp_network_send(sd, packtosend, sizeof(STCPHeader), NULL);
                
                /* go into CLOSE-WAIT */
                if (ctx->connection_state == CSTATE_ESTABLISHED){
                    DEBUG("State: CLOSE_WAIT\n");
                    /* inform app of FIN */
                    stcp_fin_received(sd);
                    
                    ctx->connection_state = CSTATE_CLOSE_WAIT;
                }
                /* go from FIN-WAIT2 --> CLOSED */
                if (ctx->connection_state == CSTATE_FIN_WAIT2) {
                    DEBUG("State: CLOSED\n");
                    ctx->connection_state = CSTATE_CLOSED;
                    free(in_header);
                    in_header = NULL;
                    break;
                }
                /* go from FIN-WAIT1 --> CLOSING */
                if (ctx->connection_state == CSTATE_FIN_WAIT1){
                    DEBUG("State: CLOSING\n");
                    /* inform app of FIN */
                    stcp_fin_received(sd);
                    
                    ctx->connection_state = CSTATE_CLOSING;
                }
                free(in_header);
                in_header = NULL;
            }
        }
        
        if (event & APP_CLOSE_REQUESTED)
        {
            DEBUG("Application close requested\n");
            if(ctx->connection_state == CSTATE_ESTABLISHED)
            {
                /* need to send all outstanding data first */
                DEBUG("Sending FIN packet with seq %d, ack %d\n", ctx->send_next, ctx->recv_next);
                STCPHeader *header = make_stcp_packet(TH_FIN, ctx->send_next, ctx->recv_next, 0);
                stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
                ctx->send_unack += 1;
                packet_t_create(ctx, header, sizeof(STCPHeader));
                free(header);
                header = NULL;
                /* go into FIN-WAIT1 */
                ctx->connection_state = CSTATE_FIN_WAIT1;
                DEBUG("State: FIN-WAIT1\n");
            }
            if(ctx->connection_state == CSTATE_CLOSE_WAIT)
            {
                DEBUG("Sending FIN packet with seq %d, ack %d\n", ctx->send_next, ctx->recv_next);
                STCPHeader *header = make_stcp_packet(TH_FIN, ctx->send_next, ctx->recv_next, 0);
                stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
					 packet_t_create(ctx, header, sizeof(STCPHeader));
                ctx->send_next += 1;
                free(header);
                header = NULL;
                /* go from CLOSE-WAIT --> LAST-ACK */
                ctx->connection_state = CSTATE_LAST_ACK;
                DEBUG("State: LAST-ACK\n");
            }
        }
        if (event == TIMEOUT)
        {
        	/* TIMEOUT--resend all packets in ctx->unackd_packets */
        	packet_t *resendpack;
        	list_iterator_start(ctx->unackd_packets);
        	while (list_iterator_hasnext(ctx->unackd_packets)){
        		resendpack = (packet_t *)list_iterator_next(ctx->unackd_packets);
        		/* increment retries */
        		resendpack->retry_count++;
			clock_gettime(CLOCK_REALTIME, &(resendpack->start_time)); 
        		((STCPHeader *)(resendpack->packet))->th_ack = htonl(ctx->recv_next);
        		stcp_network_send(sd, resendpack->packet, resendpack->packet_size, NULL);
			DEBUG("Resent packet with sequence number %d\n", ntohl(((STCPHeader *)(resendpack->packet))->th_seq));
        	}
        	list_iterator_stop(ctx->unackd_packets);
    	}
   }
}


/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];
    
    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}




/**********************************************************************/
/*
 * adds a record of new, unacknowledged packet to unackd_packets
 * should be called BEFORE updating send_next
 * the list of unacked packets serves as our send buffer
 * it allows for easy management of timeouts and retransmissions
 */

static void packet_t_create(context_t *ctx, void *packet, int packetsize){
    packet_t *newpack = (packet_t *)malloc(sizeof(packet_t));
    struct timespec start;
    if (clock_gettime(CLOCK_REALTIME, &start) < 0)
        DEBUG("Failed to get time in packet_t_create\n");
    newpack->start_time = start;
    newpack->retry_count = 0;
    newpack->seq_num = ctx->send_next;
    newpack->packet = malloc(packetsize);
    memcpy(newpack->packet, packet, packetsize);
    newpack->packet_size = packetsize;
    list_append(ctx->unackd_packets, newpack);
    return;
}

/* removes every ack'd packet from unackd_packets
 * should be called AFTER updating send_unack
 */
static void packet_t_remove(context_t *ctx){
    while (list_size(ctx->unackd_packets) > 0){
        packet_t *oldpack = list_get_at(ctx->unackd_packets, 0);
        
        /* update RTO given this packet */
        update_rto(ctx, oldpack);

        
        /* if all the data in the packet was acknowledged, discard and delete from list */
        if (ctx->send_unack >= oldpack->seq_num + (oldpack->packet_size - sizeof(STCPHeader))){
        	   free(oldpack->packet);
        	   oldpack->packet = NULL;
            list_delete_at(ctx->unackd_packets, 0);
        }
        else
            /* this packet and all beyond it are unacknowledged */
            break;
    }
    return;
}

/*

0                   1                   2                   3   
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Source Port          |       Destination Port        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Sequence Number                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Acknowledgment Number                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Data |           |U|A|P|R|S|F|                               |
| Offset| Reserved  |R|C|S|S|Y|I|            Window             |
|       |           |G|K|H|T|N|N|                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Checksum            |         Urgent Pointer        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Options                    |    Padding    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             data                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

 * make_stcp_packet
 *
 * returns a STCP header pointer according to the STCP spec
 * there are no options, and the offset is always 20 bytes
 * the function also sets the advertised window,
 * and of course seq and ack numbers
 */
static STCPHeader * make_stcp_packet(uint8_t flags, tcp_seq seq, tcp_seq ack, int len)
{
    STCPHeader * header = (STCPHeader *) calloc(1, sizeof(STCPHeader) + len);
	assert(header);
    
	header->th_flags = flags;
	header->th_seq = htonl(seq);
    	header->th_ack = htonl(ack);
	header->th_off = OFFSET;
	header->th_win = htons(WINLEN);
	return header;
}


/* call stcp_network_recv, error-check, update context, return packet header in header and payload in recvbuff. Return value is size of payload*/
int recv_packet(mysocket_t sd, context_t *ctx, void *recvbuff, size_t buffsize, STCPHeader *header){
    size_t packlen, paylen, send_win;
    void *payload;
    void *buff = calloc(1, sizeof(STCPHeader) + MAXOPS + MAXLEN);
    size_t recv_window_size;
    int data_to_app = 0;
    
    /* zero out recvbuff and header so we're extra-sure there's no harm in re-using them */
    memset(recvbuff, '\0', buffsize);
    memset(header, '\0', sizeof(STCPHeader));
    
    /* receive data from network */
    packlen = stcp_network_recv(sd, buff, sizeof(STCPHeader) + MAXOPS + MAXLEN);
    if (packlen >= sizeof(STCPHeader)){
        
        /* get packet header */
        memcpy(header, buff, sizeof(STCPHeader));
        DEBUG("Received packet with ack number %d (if TH_ACK set) and seq number %d\n", ntohl(header->th_ack), ntohl(header->th_seq));
  
        paylen = packlen - (WORDSIZE * header->th_off);
        if (paylen > 0){
        		/* update context recv buffer */
        		int buffer_offset = ntohl(header->th_seq) - ctx->recv_next;

        		payload = buff + (WORDSIZE * header->th_off);
        		if (buffer_offset < 0){
        			buffer_offset = 0;
        			if (ntohl(header->th_seq) + paylen <= ctx->recv_next){
        				paylen = 0;
					data_to_app = -2;
        			}
        			else{
        				payload += ctx->recv_next - ntohl(header->th_seq);
        				paylen -= ctx->recv_next - ntohl(header->th_seq);
        			}
        		}
        		else if (buffer_offset > WINLEN){
        	 		free(buff);
        	 		return -1;
        		}

        		recv_window_size = WINLEN - buffer_offset;
        		if (paylen > recv_window_size){
        			paylen = recv_window_size;
        			DEBUG("Packet crossed received window, was truncated\n");
        		}

        		memcpy(ctx->recv_buffer + buffer_offset, payload, paylen);
        		memset(ctx->recv_indicator + buffer_offset, '1', paylen);
        		
        		/* update recv_next */
        		ctx->recv_next += strchr(ctx->recv_indicator, '\0') - ctx->recv_indicator;
        		
			if (data_to_app != -2)
        			data_to_app = strchr(ctx->recv_indicator, '\0') - ctx->recv_indicator;
        }
        else 
        		data_to_app = 0;


        /* if packet acknowledges previously unacknowledged data, update send_unack */
        if ((header->th_flags & TH_ACK) && ctx->send_unack <= ntohl(header->th_ack))
            ctx->send_unack = ntohl(header->th_ack);
            
        /* update list of unack'd packets */
          packet_t_remove(ctx);
        
        /* if packet is a SYN, set ctx->recv_next, even though it doesn't include new data */
        if (header->th_flags & TH_SYN)
        		ctx->recv_next = ntohl(header->th_seq) + 1;
        
        /* also update sender window size */
        send_win = ntohs(header->th_win); /* - (ctx->send_next - ctx->send_unack); /* advertised sender window minus data still in transit to sender */
        if (send_win <= WINLEN)
            ctx->send_wind = send_win;
        else
            ctx->send_wind = WINLEN;
    }
    else{
        DEBUG("Error: invalid packet length");
        free (buff);
        return -1;
    }
    
    free(buff);
    return data_to_app;
}


/*
 * we want to maintain a smoothed RTT (SRTT)
 * and an RTT variation (RTTVAR)
 *
 * initial RTT is set to 3 seconds
 *
 * when the first RTT measurement R is made:
 * SRTT = R
 * RTTVAR = R/2
 * RTO = SRTT + max(G, K*RTTVAR) (where G is clock granularity)
 * where K = 4
 *
 * when further RTT measurements R' are made:
 * RTTVAR = (1 - beta) * RTTVAR + beta * |SRTT - R'|
 * SRTT = (1 - alpha) * SRTT + alpha * R'
 * where alpha = 1/8 and beta = 1/4
 *
 * then the host updates:
 * RTO = SRTT + max(G, K*RTTVAR)
 *
 * RTO should always be rounded up to 1 second
 *
 * use Karn's algorithm to take RTT samples,
 * i.e. cannot use retransmitted segments
 *
 * a clock granularity of 100 msec or less is good
 *
 * the basic algorithm:
 * 1. when a packet containing data is sent, start the timer so that it
 *    expires after RTO seconds for current RTO
 * 2. when all outstanding data is acknowledged, turn off the timer
 * 3. when an ACK is received that acknowledges new data,
 *    restart the timer so that it expires after RTO seconds
 * when the timer expires, perform the following:
 * 4. retransmit the earliest segment unacked by receiver
 * 5. host sets RTO = RTO * 2 (back off)
 * 6. start the timer to expire after RTO seconds
 */

static void update_rto(context_t *ctx, packet_t *packet)
{
    static int init = 0;
    /*
     * check to see if the packet has been retransmitted
     * if so, return and do nothing to the RTO
     */
    if (packet->retry_count) return;
    
    /* start by getting the RTT of the acked packet */
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    
    double diff_sec = difftime(tp.tv_sec, packet->start_time.tv_sec);
    time_t rtt_sec = (time_t) diff_sec;    

    long diff_nsec = max(tp.tv_nsec, packet->start_time.tv_nsec) - min(tp.tv_nsec, packet->start_time.tv_nsec);
    long rtt_nsec = diff_nsec;
    
    /* update the values of SRTT and RTTVAR */
    if(!init)
    {
        ctx->srtt.tv_sec = rtt_sec;
        ctx->srtt.tv_nsec = rtt_nsec;
        ctx->rttvar_sec = rtt_sec/2;
        ctx->rttvar_nsec = rtt_nsec/2;
        
        init = 1;
    }
    else
    {
        ctx->rttvar_sec = (1 - BETA)*ctx->rttvar_sec + BETA*abs(ctx->srtt.tv_sec - rtt_sec);
        ctx->rttvar_nsec = (1 - BETA)*ctx->rttvar_nsec + BETA*abs(ctx->srtt.tv_nsec - rtt_nsec);
        
        ctx->srtt.tv_sec = (1 - ALPHA)*ctx->srtt.tv_sec  + ALPHA*rtt_sec;
        ctx->srtt.tv_nsec = (1 - ALPHA)*ctx->srtt.tv_nsec  + ALPHA*rtt_nsec;
    }
    /* then the value of RTO is updated */
    ctx->rto.tv_sec = ctx->srtt.tv_sec + max(G, K*(ctx->rttvar_sec));
    ctx->rto.tv_nsec = ctx->srtt.tv_nsec + max(G, K*(ctx->rttvar_nsec));
    
    /* ensure the nsec field is not too large */
    if (ctx->rto.tv_nsec >= 1000000000) {
        ctx->rto.tv_nsec -= 1000000000;
        ctx->rto.tv_sec  += 1;
    }

    return;
}





