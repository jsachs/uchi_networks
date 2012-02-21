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

#define ALPHA .125
#define BETA  .25
#define K     4

static void update_rto(context_t *ctx, packet_t *packet)
{
    static int init = 0;
    
    /* check to see if the packet has been retransmitted
     * if so, return and do nothing to the RTO
     */
    
    /* start by getting the RTT of the acked packet */
    
    /* update the values of SRTT and RTTVAR */
    
    /* then the value of RTO is updated */
    
    
}
 
 
 
 
 
 
 
 



