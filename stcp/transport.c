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
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"

#define DEBUG(x, args...) printf(x, ## args)


#define MAXLEN 536
#define OFFSET 5
#define HEADERSIZE 20
#define WINLEN 3072
#define MAX_INIT_SEQ 256


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
    
    tcp_seq send_unack;
    tcp_seq send_next;
    tcp_seq send_wind;
    tcp_seq recv_next;
    tcp_seq recv_wind;
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);
void send_packet(int sd, uint8_t flags, context_t *ctx, uint16_t winsize, void *payload, size_t psize);
static STCPHeader * make_stcp_packet(uint8_t flags, tcp_seq seq, tcp_seq ack, int len);



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
    
    char buffer[sizeof(STCPHeader) + MAXLEN];
    
    generate_initial_seq_num(ctx);
    
    ctx->connection_state = CSTATE_CLOSED;
    
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
        tcplen = stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
        
        DEBUG("Sent SYN packet with seq number %d\n", ntohl(header->th_seq));
        
        if(tcplen < 0) {
            DEBUG("SYN send failed");
            errno = ECONNREFUSED;
        }
        else
        {
            ctx->send_unack += 1;
            ctx->send_next = 0;
            ctx->connection_state = CSTATE_SYN_SENT;
            
            while(ctx->connection_state == CSTATE_SYN_SENT)
            {
                unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA, NULL); /* yes we need to add timeout later */
                
                if(event & NETWORK_DATA)
                {
                    tcplen = stcp_network_recv(sd, buffer, sizeof(buffer));
                    flags = (TH_SYN|TH_ACK);
                    header = (STCPHeader *) buffer;
                    if(( tcplen < sizeof(STCPHeader)) || !((header->th_flags & TH_SYN)&&(header->th_flags & TH_ACK))) {
                        DEBUG("Did not receive SYN/ACK\n");
                        continue;
                    }
                    else
                    {
                        DEBUG("packet recieved with seq %d and ack %d\n", ntohl(header->th_seq), ntohl(header->th_ack));
                        if(ntohl(header->th_ack) == ctx->send_unack){
                            
                            ctx->recv_next = ntohl(header->th_seq) + 1;
                            header = make_stcp_packet(TH_ACK, ctx->send_unack, ctx->recv_next, 0);
                            tcplen = stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
                            DEBUG("Sent ACK packet with seq number %d\n", ntohl(header->th_seq));
                            if(tcplen < 0) {
                                DEBUG("ACK send failed");
                                errno = ECONNABORTED;
                                break;
                            }
                            ctx->connection_state = CSTATE_ESTABLISHED;
                            free(header);
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
            unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA, NULL); /* yes we need to add timeout later */
            if(event & NETWORK_DATA){
                tcplen = stcp_network_recv(sd, buffer, sizeof(buffer));
                STCPHeader *header = (STCPHeader *) buffer;
                if(tcplen < sizeof(STCPHeader) || header->th_flags != TH_SYN) {
                    DEBUG("Did not receive SYN packet\n");
                    continue;
                }
                else
                {
                    DEBUG("Received SYN with seq number %d\n", ntohl(header->th_seq));
                    ctx->connection_state = CSTATE_SYN_RECEIVED;

                    ctx->send_unack = ctx->initial_sequence_num;
                    ctx->send_next = 0;
                    ctx->recv_next = ntohl(header->th_seq) + 1;
                    ctx->send_wind = ntohs(header->th_win);
                    
                    header = make_stcp_packet(TH_SYN|TH_ACK, ctx->send_unack, ctx->recv_next, 0);
                    tcplen = stcp_network_send(sd, header, sizeof(STCPHeader), NULL);

                    DEBUG("SYN/ACK sent with ack number %d and seq number %d\n", htonl(header->th_ack), htonl(header->th_seq));
                    if(tcplen < 0){
                        DEBUG("SYN/ACK send failed");
                        errno = ECONNABORTED;
                        break;
                    }
                    else ctx->send_unack += 1;
                }
            }
        }
        while(ctx->connection_state == CSTATE_SYN_RECEIVED)
        {
            unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
            if(event & NETWORK_DATA){
                tcplen = stcp_network_recv(sd, buffer, sizeof(buffer));
                STCPHeader *header = (STCPHeader *) buffer;
                if(tcplen < sizeof(STCPHeader) || !(header->th_flags & TH_ACK)) {
                    DEBUG("Did not receieve ACK packet\n");
                    continue;
                }
                if(ntohl(header->th_ack) == ctx->send_unack) {
                    DEBUG("Receieved ACK with ack number %d\n", ntohl(header->th_ack));
                    ctx->connection_state = CSTATE_ESTABLISHED;
                }
            }
        }
    }
    
    /* potentially handle error conditions */
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
    /* you have to fill this up */
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
    uint8_t flags;
    uint16_t winsize = WINLEN;
    int tcplen;
    char packet[sizeof(STCPHeader) + 40 + MAXLEN]; /* Header size, plus options, plus payload */
    
    while (!ctx->done)
    {
        unsigned int event;
        
        /* see stcp_api.h or stcp_api.c for details of this function */
        /* XXX: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, ANY_EVENT, NULL);
        
        /* check whether it was the network, app, or a close request */
        if (event & APP_DATA)
        {
            DEBUG("Application Data\n");
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
            if(ctx->send_next < ctx->send_wind)
            {
                int send_size = MAXLEN;
                
                if((ctx->send_wind)-(ctx->send_next) > MAXLEN) send_size = MAXLEN;
				else send_size = ctx->send_wind - ctx->send_next;
                
                char * buffer = (char *) calloc(1, send_size);
                
                tcplen = stcp_app_recv(sd, buffer, send_size);
                
                if( tcplen > 0 ) {
                    DEBUG("Application data size: %d\n", tcplen);
                    /* deal with app data
                     * there will need to be some fancy
                     * while loop to get app data
                     */
                }
                free(buffer);
            }
            else DEBUG("Could not send application data");
        }
        
        if (event & NETWORK_DATA)
        {
            DEBUG("Network Data\n");
            /* network data transmission */
            /* see stcp_network_recv() */
            
            tcplen = stcp_network_recv(sd, packet, sizeof(packet));
            
            if( tcplen >= sizeof(STCPHeader) )
            {
                STCPHeader *header = (STCPHeader *)packet;
                DEBUG("Receieved packet with seq %d and ack %d\n", ntohl(header->th_seq), ntohl(header->th_ack));
                
                /* update the sliding window here */
                int data_size = tcplen - (header->th_off);
                DEBUG("Received net data size is %d\n", data_size);
                
                if (!(header->th_flags))
                {
		   if(ntohl(header->th_seq) == ctx->recv_next) {
						stcp_app_send(sd, header + (header->th_off), data_size);
						ctx->recv_next += data_size;
                        
						header = make_stcp_packet(TH_ACK, (ctx->send_unack)+(ctx->send_next), ctx->recv_next, 0);
						stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
						free(header);
					} 
                    else if(ntohl(header->th_seq) < ctx->recv_next + WINLEN) { /* data is ack'd but not sent to app */
						header = make_stcp_packet(TH_ACK, (ctx->send_unack)+(ctx->send_next), ctx->recv_next, 0);
						stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
						free(header);
					}
                }
                else if (header->th_flags & TH_ACK)
                {
                    if((ntohl(header->th_ack) > ctx->send_unack) && (ntohl(header->th_ack) <= (ctx->send_unack)+(ctx->send_next))) {
						int shift = ntohl(header->th_ack) - (ctx->send_unack);
						ctx->send_unack = ntohl(header->th_ack);
						ctx->send_next = ctx->send_next - shift;
					}
					if(ctx->connection_state == CSTATE_FIN_WAIT1) {
						DEBUG("State: FIN-WAIT2\n");
						ctx->connection_state = CSTATE_FIN_WAIT2;
					}
                    
					if(ctx->connection_state == CSTATE_CLOSE_WAIT) {
						DEBUG("State: CLOSED\n");
                        ctx->connection_state = CSTATE_CLOSED;
						break;
					}
                    
					if(ctx->connection_state == CSTATE_LAST_ACK) {
						DEBUG("State: CLOSED\n");
                        ctx->connection_state = CSTATE_CLOSED;
						break;
					}
                }
                else if (header->th_flags & TH_FIN)
                {
                    if(ctx->connection_state == CSTATE_ESTABLISHED) {
						DEBUG("Receiveing FIN packet\n");
						/* send ack */
						ctx->recv_next += 1;
						STCPHeader *header = make_stcp_packet(TH_ACK, ctx->send_unack, ctx->recv_next, 0);
						stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
						free(header);
                        
						/* inform app */
						stcp_fin_received(sd);
                        
						ctx->connection_state = CSTATE_CLOSE_WAIT;
						DEBUG("State: CLOSE-WAIT\n");
					}
                    
					if(ctx->connection_state == CSTATE_FIN_WAIT1) {
                        
						ctx->recv_next += 1;
						STCPHeader *header = make_stcp_packet(TH_ACK, ctx->send_unack, ctx->recv_next, 0);
						stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
						free(header);
                        
						/* inform app */
						stcp_fin_received(sd);
                        
						/* send ack */
						ctx->connection_state = CSTATE_CLOSING;
                        
						DEBUG("State: CLOSING\n");
					}
                    
					if(ctx->connection_state == CSTATE_FIN_WAIT2) {
					  /* send ack */
						ctx->recv_next += 1;
						STCPHeader *header = make_stcp_packet(TH_ACK, ctx->send_unack, ctx->recv_next, 0);
						stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
						free(header);
                        
						break;
					}
                }
                else if (header->th_flags == (TH_ACK|TH_FIN))
                {
                    if(ctx->connection_state == CSTATE_FIN_WAIT1) {
                        
						STCPHeader *header = make_stcp_packet(TH_ACK, ctx->send_unack, ctx->recv_next, 0);
						stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
						free(header);
                        
						break;
					}
                }
            }
            else DEBUG("Invalid Packet Length\n");
        }
        
        if (event & APP_CLOSE_REQUESTED)
        {
            DEBUG("Application close requested\n");
            if(ctx->connection_state == CSTATE_ESTABLISHED)
            {
                DEBUG("Sending FIN packet with seq %d, ack %d\n", ctx->send_unack, ctx->recv_next);
                STCPHeader *header = make_stcp_packet(TH_FIN, ctx->send_unack, ctx->recv_next, 0);
                stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
				free(header);
                
                ctx->send_unack += 1;
                ctx->connection_state = CSTATE_FIN_WAIT1;
                DEBUG("State: FIN-WAIT1\n");
            }
            if(ctx->connection_state == CSTATE_CLOSE_WAIT)
            {
                DEBUG("Sending FIN packet with seq %d, ack %d\n", ctx->send_unack, ctx->recv_next);
                STCPHeader *header = make_stcp_packet(TH_FIN, ctx->send_unack, ctx->recv_next, 0);
                stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
				free(header);
                
                ctx->send_unack += 1;
                ctx->connection_state = CSTATE_LAST_ACK;
                DEBUG("State: LAST-ACK\n");
            }
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




