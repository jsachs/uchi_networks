/* a module to compute RTO for STCP */

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

#define DEBUG(x, args...) printf(x, ## args)
#define max(a, b)  ( (a > b) ? (a) : (b) )

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

#define ALPHA 0.125
#define BETA  0.25
#define K     4
#define G     0.1

static void update_rto(context_t *ctx, packet_t *packet)
{
    static int init = 0;
    
    /* check to see if the packet has been retransmitted
     * if so, return and do nothing to the RTO
     */
    if(packet->retry_count) return;
    
    /* start by getting the RTT of the acked packet */
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    double diff = difftime(tp.tv_sec, packet->start_time.tv_sec);
    time_t rtt = (time_t) diff;
    
    /* update the values of SRTT and RTTVAR */
    if(!init)
    {
        ctx->srtt   = rtt;
        ctx->rttvar = rtt/2;
        init = 1;
    }
    else
    {
        ctx->rttvar = (1 - BETA)*ctx->rttvar + BETA*abs(ctx->srtt - rtt);
        ctx->srtt   = (1 - ALPHA)*ctx->srtt  + ALPHA*rtt;
    }
    /* then the value of RTO is updated */
    ctx->rto = ctx->srtt + max(G, K*(ctx->rttvar)); // need to figure out clock granularity
    
    return;
}
 
 
 
 
 
 
 
 



