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
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"

#define BIGWIN 2048


enum { CSTATE_ESTABLISHED };    /* obviously you should have more states */


/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;

    tcp_seq current_sequence_num;
    tcp_seq current_ack_num;
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);


/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;
    
    STCPHeader header;
    header.th_seq = ctx->initial_sequence_num + 1;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);

    generate_initial_seq_num(ctx);

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
       header.th_off = 5;
       header.th_flags = TH_SYN;
       header.th_win = BIGWIN;
       
       stcp_network_send(sd, &header, sizeof(header));
    	
    	/* wait for a SYN_ACK */
    	stcp_network_recv(sd, &header, sizeof(header));
    	ctx->current_ack_num = header.th_seq + 1;
    	
    	
    	/* send an ACK, change state to established */
    	header.th_ack = ctx->current_ack_num;
    	header.th_off = 5;
    	header.th_flags = TH_ACK;
      header.th_win = BIGWIN;
      
      stcp_network_send(sd, &header, sizeof(header));
    }
    
    if(!is_active)
    {
      /* wait for a SYN packet */
      stcp_network_recv(sd, &header, BIGWIN);
      if(!(header.th_flags & TH_SYN)){
      	/* do some error handling */
      }
      
      /* send a SYN_ACK */
      ctx->current_ack_num = header.th_seq + 1;      
      
      header.th_ack = ctx->current_ack_num;
      header.th_off = 5; /* unless there are options we'll deal with later */
      header.th_flags = TH_SYN|TH_ACK;
      header.th_win = BIGWIN;
      
      stcp_network_send(sd, &header, sizeof(header));
      
      /* wait for ACK, then change state to established */
		stcp_network_recv(sd, &header, BIGWIN);
      if(!(header.th_flags & TH_ACK)){
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
    /*ctx->initial_sequence_num =;*/
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
            /* stcp_app_recv(mysocket_t sd, void *dst, size_t max_len); */
            
            
        }

        if (event & NETWORK_DATA)
        {
        	   /* network data transmission */
            /* see stcp_network_recv() */
            /* stcp_network_recv(mysocket_t sd, void *dst, size_t max_len); */
        }
        
        /* etc */
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



