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
       CSTATE_CLOSED };    /* obviously you should have more states */


/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */
    
    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;
    
    tcp_seq seq_num_incoming;
    tcp_seq ack_num_incoming;
    tcp_seq seq_num_outgoing;
    tcp_seq ack_num_outgoing;
    
    char window_buffer[WINLEN];
    tcp_seq window_current;
    tcp_seq window_start;
    tcp_seq window_end;
    tcp_seq window_init;
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);
void send_packet(int sd, uint8_t flags, context_t *ctx, uint16_t winsize, void *payload, size_t psize);
static STCPHeader * make_stcp_packet(uint8_t flags, context_t *ctx, int len);



/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;
    uint8_t flags;
    uint16_t winsize = WINLEN;
    int tcplen;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);
    
    char buffer[sizeof(STCPHeader) + MAXLEN];
    
    generate_initial_seq_num(ctx);
    ctx->seq_num_outgoing = ctx->initial_sequence_num;
    
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
        STCPHeader *header = make_stcp_packet(TH_SYN, ctx, 0);
        tcplen = stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
        
        DEBUG("Sent SYN packet with seq number %d\n", ntohl(header->th_seq));
        
        if(tcplen < 0) {
            DEBUG("SYN send failed");
            errno = ECONNREFUSED;
        }
        else
        {
            ctx->connection_state = CSTATE_SYN_SENT;
            ctx->ack_num_incoming = ntohl(header->th_seq) + 1;
            ctx->seq_num_outgoing = ctx->ack_num_incoming;
            
            while(1)
            {
                unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA, NULL); /* yes we need to add timeout later */
                
                if(event & NETWORK_DATA)
                {
                    stcp_network_recv(sd, buffer, sizeof(buffer));
                    STCPHeader *inheader = (STCPHeader *) buffer;
                    DEBUG("packet recieved with ack number %d\n", ntohl(inheader->th_ack));
                    if(((inheader->th_flags & TH_SYN) &&  (inheader->th_flags & TH_ACK))  && ( (tcp_seq) ntohl(inheader->th_ack) == ctx->seq_num_outgoing)){
			ctx->seq_num_incoming = (ntohl(inheader->th_seq));
			ctx->ack_num_outgoing = ctx->seq_num_incoming + 1;
                        header = make_stcp_packet(TH_ACK, ctx, 0);
                        tcplen = stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
                        DEBUG("Sent ACK packet with seq number %d\n", ntohl(header->th_seq));
                        if(tcplen < 0) {
                            DEBUG("SYN send failed");
                            errno = ECONNREFUSED;
                        }
                        free(header);
                        break;
                    }
                }
            }
        }
    }
    if(!is_active)
    {
        STCPHeader *header = (STCPHeader *)calloc(1, sizeof(STCPHeader) + MAXLEN);
        /* wait for a SYN packet */
        while(1)
        {
            unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA, NULL); /* yes we need to add timeout later */
            if(event & NETWORK_DATA){
                stcp_network_recv(sd, buffer, sizeof(buffer));
                header = (STCPHeader *) buffer;
                if(header->th_flags & TH_SYN){
                    ctx->seq_num_incoming = ntohl(header->th_seq);
                    ctx->ack_num_incoming = ntohl(header->th_ack);
                    ctx->connection_state = CSTATE_SYN_RECEIVED;
                    ctx->ack_num_outgoing = ntohl(header->th_seq) + 1;
                    
                    header = make_stcp_packet(TH_SYN|TH_ACK, ctx, 0);
                    tcplen = stcp_network_send(sd, header, sizeof(STCPHeader), NULL);
                    DEBUG("SYN/ACK sent with ack number %d and seq number %d\n", header->th_ack, header->th_seq);
                    if(tcplen < 0){
                        DEBUG("SYN/ACK send failed");
                        errno = ECONNREFUSED;
                    }
                    break;
                }
            }
        }
        while(1)
        {
            unsigned int event = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
            if(event & NETWORK_DATA){
                stcp_network_recv(sd, buffer, sizeof(buffer));
                header = (STCPHeader *) buffer;
                if((header->th_flags & TH_ACK) & (header->th_ack == ctx->seq_num_outgoing + 1)) {
                    free(header);
                    break;
                }
            }
        }
    }
    
    
    /* potentially handle error conditions */    
    
    ctx->connection_state = CSTATE_ESTABLISHED;
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
    STCPHeader *header = (STCPHeader *)malloc(HEADERSIZE);
    while (!ctx->done)
    {
        unsigned int event;
        
        /* see stcp_api.h or stcp_api.c for details of this function */
        /* XXX: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, 0, NULL);
        
        /* check whether it was the network, app, or a close request */
        if (event & APP_DATA)
        {
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
            void *payload;
            stcp_app_recv(sd, payload, MAXLEN);
            
            /* deal with seq numbers */
            ctx->seq_num_outgoing += sizeof(&payload);
            
            send_packet(sd, flags, ctx, winsize, payload, sizeof(&payload));
        }
        
        if (event & NETWORK_DATA)
        {
            /* network data transmission */
            /* see stcp_network_recv() */
            void *payload;
            stcp_network_recv(sd, payload, MAXLEN);
            
            /* deal with extracting header */
            
            /* deal with seq numbers */
            
            stcp_app_send(sd, payload, sizeof(payload));
        }

        if (event & APP_CLOSE_REQUESTED)
        {
            /* send any unsent data */
            flags = TH_FIN;
            send_packet(sd, flags, ctx, winsize, NULL, 0);
            ctx->connection_state = CSTATE_FIN_WAIT1;
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

/* send_packet
 *
 * a wrapper function that takes flags, a context, window size,
 * and a pointer and size for the payload, if there is one
 */
void send_packet(int sd, uint8_t flags, context_t *ctx, uint16_t winsize, void *payload, size_t psize)
{
    void *packet_header;

    STCPHeader *header = (STCPHeader *)malloc(HEADERSIZE);
    header->th_seq = htonl(ctx->seq_num_outgoing);
    header->th_ack = htonl(ctx->ack_num_outgoing);
    header->th_off = OFFSET;
    header->th_flags = flags;
    header->th_win = htons(winsize);
    size_t headersize = HEADERSIZE;
    packet_header = malloc(HEADERSIZE);
    memcpy(packet_header, header, HEADERSIZE);
    
    /* deal with payload */
    if( !payload )
    {
        if (psize < MAXLEN){
            if (stcp_network_send(sd, packet_header, headersize, payload, psize, NULL) < 0){
                /* error handling */
            }
        }
        else {
            /* deal with too much data */
        }
    }
    
    else
    {
        if (stcp_network_send(sd, packet_header, headersize, NULL) < 0){
            /* error handling */
        }
    }
    free(header);
    free(packet_header);
    return;
}

static STCPHeader * make_stcp_packet(uint8_t flags, context_t *ctx, int len)
{
    STCPHeader * header = (STCPHeader *) calloc(1, sizeof(STCPHeader) + len);
	assert(header);
    
	header->th_flags = flags;
	header->th_seq = htonl(ctx->seq_num_outgoing);
    header->th_ack = htonl(ctx->ack_num_outgoing);
	header->th_off = OFFSET;
	header->th_win = htons(WINLEN);
	return header;
}





