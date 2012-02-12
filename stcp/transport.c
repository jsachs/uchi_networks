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

#define MAXLEN 536
#define BIGWIN 2048
#define OFFSET 5
#define HEADERSIZE 20
#define WINLEN 3072
#define RAND_MAX 256


enum { CSTATE_ESTABLISHED,
       CSTATE_SYN_SENT,
       CSTATE_SYN_RECEIVED,
       CSTATE_FIN_WAIT1,
       CSTATE_FIN_WAIT2,
       CSTATE_CLOSE_WAIT,
       CSTATE_LAST_ACK };    /* obviously you should have more states */



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
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);
void send_packet(int sd, uint8_t flags, context_t *ctx, uint16_t winsize, void *payload, size_t psize);


/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    STCPHeader *header = (STCPHeader *)malloc(HEADERSIZE);

    context_t *ctx;
    uint8_t flags;
    uint16_t winsize = BIGWIN;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);
    
    generate_initial_seq_num(ctx);
    ctx->seq_num_outgoing = ctx->initial_sequence_num;
    
    /* XXX: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.  after the handshake completes, unblock the
     * application with stcp_unblock_application(sd).  you may also use
     * this to communicate an error condition back to the application, e.g.
     * if connection fails; to do so, just set errno appropriately (e.g. to
     * ECONNREFUSED, etc.) before calling the function.
     */
    if(is_active)
    {
        /* send a SYN packet */
        flags = TH_SYN;
        
        send_packet(sd, flags, ctx, winsize, NULL, 0);
        ctx->connection_state = CSTATE_SYN_SENT;
        
    	/* wait for a SYN_ACK */
    	stcp_network_recv(sd, header, sizeof(header));
    	ctx->seq_num_incoming = header->th_seq;
        ctx->ack_num_incoming = header->th_ack;
        ctx->ack_num_outgoing = ctx->seq_num_incoming + 1;
        ctx->seq_num_outgoing = ctx->ack_num_incoming;

    	/* send an ACK, change state to established */
        flags = TH_ACK;
        send_packet(sd, flags, ctx, winsize, NULL, 0);
    }
    
    if(!is_active)
    {
        /* wait for a SYN packet */
        stcp_network_recv(sd, header, BIGWIN);
        if(!(header->th_flags & TH_SYN)){
            /* do some error handling */
        }
        ctx->seq_num_incoming = header->th_seq;
        ctx->ack_num_incoming = header->th_ack;
        ctx->connection_state = CSTATE_SYN_RECEIVED;
        ctx->ack_num_outgoing = header->th_seq + 1;      
        
        /* send a SYN_ACK */
        flags = TH_SYN|TH_ACK;
        
        send_packet(sd, flags, ctx, winsize, NULL, 0);
        
        /* wait for ACK, then change state to established */
	    stcp_network_recv(sd, header, BIGWIN);
        if(!(header->th_flags & TH_ACK)){
            /* do some error handling */
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
    ctx->initial_sequence_num = rand();
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
    uint16_t winsize = BIGWIN;
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
            
            send_packet(sd, flags, ctx, winsize, payload, sizeof(&payload));
        }
        
        if (event & NETWORK_DATA)
        {
            /* network data transmission */
            /* see stcp_network_recv() */
            void *payload;
            stcp_network_recv(sd, payload, MAXLEN);
            
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
    free(packet);
    return;
}






