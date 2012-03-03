/**********************************************************************
 * file:  sr_router.c 
 * date:  Mon Feb 18 12:50:42 PST 2002  
 * Contact: casado@stanford.edu 
 *
 * Description:
 * 
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <assert.h>
#include <ether.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"



struct icmp_hdr {
    uint8_t  icmp_type;
    uint8_t  icmp_code;
    uint16_t icmp_sum;
    uint32_t icmp_unused;
} __attribute__ ((packed));

struct icmp_echoreply_hdr {
    uint8_t  icmp_type;
    uint8_t  icmp_code;
    uint16_t icmp_sum;
    uint16_t icmp_id;
    uint16_t icmp_seqnum;
} __attribute__ ((packed));

struct arpc_entry {
    uint32_t      arpc_ip;
    unsigned char arpc_mac[ETHER_ADDR_LEN];
    time_t        arpc_timeout;
    struct        arpc_entry *prev;
    struct        arpc_entry *next;
};

struct queued_packet {
    uint8_t *packet;
    unsigned len;
    char     icmp_if_name[/* some length */];
    struct   queued_packet *next;
}

struct packetq {
    struct queued_packet *first;
    struct queued_packet *last;
};

struct arpq_entry {
    uint32_t arpq_ip;
    struct   sr_if *arpq_if;
    struct   packetq arpq_packets;
    time_t   arpq_last_req;
    uint8_t  arpq_num_reqs;
    struct   arpq_entry *prev;
    struct   arpq_entry *next;
};

struct arp_cache {
    struct arpc_entry *first;
    struct arpc_entry *last;
};

struct arp_queue {
    struct arpq_entry *first;
    struct arpq_entry *last;
};

static struct arp_cache sr_arp_cache = {0, 0};

static struct arp_queue sr_arp_queue = {0, 0};

/*--------------------------------------------------------------------- 
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 * 
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr) 
{
    /* REQUIRES */
    assert(sr);

    /* Add initialization code here! */
    // initialize ARP cache
    // initialize ARP queue

} /* -- sr_init -- */

/*--------------------------------------------------------------------- 
 * Method: compute_ip_checksum(uint16_t const ipHeader[], int nWords)
 * Scope:  Local
 *
 * calculates a checksum value for IP headers
 *
 * algorithm taken from www.netrino.com
 *---------------------------------------------------------------------*/
static uint16_t compute_ip_checksum(uint16_t *ip_header, size_t len)
{
    assert(ip_header);
    
    uint16_t sum = 0; // right size?

    while(len--) sum += *(ip_header++);

    if(len%2) sum += *((uint8_t*) ip_header); // ensures even number of bytes
    
    while(sum>>16) sum = (sum >> 16) + (sum & 0xffff);

    return ((uint16_t) ~sum);
}

/*--------------------------------------------------------------------- 
 * Method: if_dst_check
 *
 *---------------------------------------------------------------------*/
static struct sr_if *if_dst_check(struct sr_instance *sr, uint32_t ip)
{
    struct sr_if *current_if = sr->if_list;
    assert(current_if);
    
    while(current_if && (ip != current_if->ip))
        current_if = current_if->next;
    
    if (current_if) return current_if;
    
    return NULL;
}

/*--------------------------------------------------------------------- 
 * Method: if_ip_search
 *
 *---------------------------------------------------------------------*/
struct sr_if *if_ip_search(struct sr_if *iface, uint32_t ip)
{
    asser(iface);
    while(iface && (ip != iface->ip))
        iface = iface->next;
    
    if (iface) return iface;
    
    return NULL;
}

/*--------------------------------------------------------------------- 
 * Method: arp_cache_lookup
 *
 *---------------------------------------------------------------------*/
static struct arpc_entry *arp_cache_lookup(struct arpc_entry *entry, uint32_t ip)
{
    assert(entry);
    while( entry && (ip != entry->arpc_ip) )
        entry = entry->next;
    
    if (entry) return entry;
    
    return NULL;
}

/*--------------------------------------------------------------------- 
 * Method: arp_queue_lookup;
 *
 *---------------------------------------------------------------------*/
static struct arpq_entry *arp_queue_lookup(struct arpq_entry *entry, uint32_t ip)
{
    assert(entry);
    while( entry && (ip != entry->arpq_ip) )
        entry = entry->next;
    
    if (entry) return entry;
    
    return NULL;
}



/*--------------------------------------------------------------------- 
 * Method: encapsulate( ... )
 *
 *---------------------------------------------------------------------*/




/*--------------------------------------------------------------------- 
 * Method: rt_match
 *
 *---------------------------------------------------------------------*/
static struct sr_rt *rt_match(struct sr_instance *sr, uint8_t *addr);


/*--------------------------------------------------------------------- 
 * Method: icmp_create
 * Scope:  Local
 *
 *
 *
 *
 *
 *
 *---------------------------------------------------------------------*/
static void icmp_create(struct ip *ip_header, struct icmp_hdr *icmp_header,
                        uint8_t type, uint8_t code, uint32_t s_ip, uint32_t d_ip,
                        uint8_t *icmp_data, size_t icmp_data_len)
{
    assert(ip_header);
    assert(icmp_header);
    assert(icmp_data);
    
    ip_header->ip_v = 4;
    ip_header->ip_hl = 5;
    ip_header->ip_tos = 0;
    ip_header->ip_id = 0;
    ip_header->ip_off = 0;
    ip_header->ip_ttl = 64;
    ip_header->ip_p = IPPROTO_ICMP;
    ip_header->ip_len - htons(iphdr->ip_hl * WORD_BYTELEN + ICMPHDR_LEN + icmpdat_len);
    (ip_header->ip_src).s_addr = s_ip;
    (ip_header->ip_dst).s_addr = d_ip;
    ip_header->ip_sum = 0;
    ip_header->ip_sum = compute_ip_checksum((uint16_t*) iphdr, iphdr->ip_hl * WORD_BYTELEN);
    
    icmp_header->icmp_type = type;
    icmp_header->icmp_code = code;
    icmp_header->icmp_unused = 0;
    
    memcpy(icmphdr + 1, icmp_data, icmp_data_len);
    icmphdr->icmp_sum = 0;
    icmphdr->icmp_sum = compute_ip_checksum((uint16_t*) icmphdr, ICMPHDR_LEN + icmp_data_len);
}

/*--------------------------------------------------------------------- 
 * Method: arp_cache_update;
 *
 *---------------------------------------------------------------------*/
static void arp_cache_update(struct arpc_entry *entry, unsigned char *mac)
{
    assert(entry);
    assert(mac);
    time_t t;
    
    memcpy(entry->arpc_mac, mac, ETHER_ADDR_LEN);
    entry->arpc_timeout = time(t) + ARPCACHE_TIMEOUT;
    
    return;
}

/*--------------------------------------------------------------------- 
 * Method: arp_cache_add;
 *
 *---------------------------------------------------------------------*/
static struct *arpc_entry arp_cache_add(struct arp_cache *cache, unsigned char *mac, uint32_t ip)
{
    assert(cache);
    assert(mac);
    time_t t;
    
    struct arpc_entry *entry;
    if(entry = (struct arpc_entry*) malloc(sizeof(struct arpc_entry)))
    {
        memcpy(entry->arpc_mac, mac, ETHER_ADDR_LEN);
        entry->arpc_ip = ip;
        entry->arpc_timeout = time(t) + ARPCACHE_TIMEOUT;
        
        entry->next = NULL;
        entry->prev = cache->last;
        
        if (!cache->first) cache->first = entry;
        else cache->last->next = entry;
        cache->last = entry;
    }
    return entry;
}

/*--------------------------------------------------------------------- 
 * Method: arpq_entry_clear;
 *
 *---------------------------------------------------------------------*/
static void arpq_entry_clear(struct sr_instance *sr,
                             struct arp_queue *queue,
                             struct arpq_entry *entry,
                             unsigned char *d_ha)
{
    assert(sr);
    assert(queue);
    assert(entry);
    assert(d_ha);
    
    struct queued_packet *qpacket;
    
    while( qpacket = (entry->arpq_packets).first )
    {
        memcpy(( (struct sr_ethernet_hdr *) qpacket->packet)->ether_shost, (entry->arpq_if)->addr, ETHER_ADDR_LEN);
        memcpy(( (struct sr_ethernet_hdr *) qpacket->packet)->ether_dhost, d_ha, ETHER_ADDR_LEN);
        
        struct ip *s_ip = (struct ip *) ( qpacket->packet + sizeof(struct sr_ethernet_hdr));
        s_ip->ip_sum = 0;
        s_ip->ip_sum = compute_ip_checksum( (uint16_t *) s_ip, s_ip->ip_hl *WORD_BYTELEN);
        
        if( !((entry->arpq_packets).first = qpacket->next) )
            (entry->arpq_packets).last = NULL;
        
        free(qpacket);
    }
    
    if (entry->prev) (entry->prev)->next = entry->next;
    else queue->first = entry->next;
    
    if (entry->next) (entry->next)->prev = entry->prev;
    else queue->last = entry->prev;
    
    free(entry);
    return;
}





/*--------------------------------------------------------------------- 
 * Method: arp_create_reply;
 *
 *---------------------------------------------------------------------*/
static void arp_create_reply(struct sr_ethernet_hdr *en_header, struct sr_arphdr arp_header, struct sr_if *s_if)
{
    assert(en_header);
    assert(arp_header);
    assert(s_if);
    
    memcpy(en_header->ether_dhost, en_header->ether_shost, ETHER_ADDR_LEN);
    memcpy(en_header->ether_shost, s_if->addr, ETHER_ADDR_LEN);
           
    memcpy(arp_header->ar_tha, arp_header->ar_sha, ETHER_ADDR_LEN);
    memcpy(arp_header->ar_sha, s_if->addr, ETHER_ADDR_LEN);
    arp_header->ar_tip = arp_header->ar_sip;
    arp_header->ar_sip = s_if->ip;
    arp_header->ar_op = htons(ARP_REPLY);
    
    return;
}



/*--------------------------------------------------------------------- 
 * Method: sr_send_packet;
 *
 *---------------------------------------------------------------------*/






/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr, 
                     uint8_t * packet/* lent */,
                     unsigned int len,
                     char* interface/* lent */)
{
    /* REQUIRES */
    assert(sr);
    assert(packet);
    assert(interface);
    
    printf("*** -> Received packet of length %d \n",len);
    
    struct sr_ethernet_hdr *recv_frame = (sr_ethernet_hdr *)packet;
    struct sr_ethernet_hdr *send_frame;
    unsigned long our_ip = sr->sr_addr->in_addr->s_addr; //I think this is right, but we may need to account for more than one?
    uint8_t dest_mac[ETHER_ADDR_LEN];
    uint8_t protocol;
    void *recv_datagram;    //a pointer to the incoming IP datagram/ARP packet, may or may not need its own memory
    void *send_datagram;    //a pointer to the outgoing IP datagram/ARP packet, gets its own memory
    struct sr_if *iface;
    
    strip_ether_header(packet, recv_datagram); //Not really going to be a helper function, just pointer arithmetic
    
    /* First, deal with ARP cache and queue timeouts
     * Which I will totally do when I have those data structures
     * If we don't do these first we may have old entries in the cache.
     */
    
    /* Do we only need to cache ARP replies, or src MAC/IP on regular IP packets too, etc? */
    /* Also, do we need to worry about fragmentation? */
    
    /* Then actually handle the packet */
    /* Start by determining protocol and stripping off the ethernet frame */
    if ( recv_frame->ether_type == ETHERTYPE_IP){
        
        /* Strip ethernet header off of packet */
        struct ip *recv_hdr;
        struct ip *send_hdr;
        
        recv_hdr = (struct ip *)recv_datagram;
        
        /* Are we the destination? */
        if (iface = if_dst_check(sr, recv_hdr->ip_dst)){    //I'm assuming we're storing things in instance in host byte order
            /* Is this an ICMP packet? */
            if (ntohl(recv_hdr->ip_p) == IPPROTO_ICMP)
                //check whether it's an echo request, then:
                generate_icmp(sr, recv_datagram, send_datagram, ECHO_REPLY, ECHO_REPLY); //return echo reply encapsulated in IP header if it was an ICMP echo request, otherwise return NULL pointer in send_datagram. Not 100% sure we'll need sr, but probably
            else generate_icmp(sr, recv_datagram, send_datagram, DEST_UNREACH, PORT_UNREACH);    //return appropriate error message encapsulated in IP header. DEST_UNREACH is the type, PORT_UNREACH the code.
            //we're just returning to sender, so just use src mac
            return_to_sender(dest_mac, recv_frame); //fill in dest_mac with src from recv_frame, probably not a helper
        }
        else {
            /* Has it timed out? */
            if (ntohl(recv_datagram->ip_ttl) <= 0){
                generate_icmp(sr, recv_datagram, send_datagram, TIME_EXCEEDED, TIME_INTRANSIT);
                return_to_sender(dest_mac, recv_frame);
            }
            else {
                /* update and forward packet; if necessary, add it to queue */
                int incache;
                //send_datagram is just a copy of recv_datagram with header updated; call to routing table lookup and checksum will be in this function
                //also sets iface
                update_ip_hdr(sr, recv_datagram, send_datagram, iface); 
                incache = arp_cache_lookup(sr, send_datagram, dest_mac); //fills in dest_mac with correct MAC and returns 1 if found, otherwise return 0
                if (!incache){
                    /* put COPY of datagram in queue */
                    add_to_queue(sr, send_datagram);
                    
                    /* make ARP request */
                    generate_arp(sr, send_datagram, ARP_REQUEST); //send datagram will now point to an ARP packet, not to the IP datagram
                    set_to_broadcast(dest_mac); // doesn't need to be a helper
                    }
                }
            }
    }
    
    
    
    
    
    
    
    else if ( (struct sr_ethernet_hdr *)packet)->ether_type == ETHERTYPE_ARP){
        
        struct sr_arphdr *arp_header = (struct sr_arphdr*) recv_datagram;
        if( ntohs(arp_header->ar_hrd) != ARPHDR_ETHER || ntohs(arp_header->ar_pro) != ETHERTYPE_IP)
            return;
        
        uint8_t in_cache = 0;
        
        struct arpc_entry arpc_ent;
        if( arpc_ent = arp_cache_lookup(sr_arp_cache.first, arp_header->ar_sip) )
        {
            arp_cache_update(arpc_ent, arp_header->ar_sha);
            in_cache = 1;
        }
        
        struct sr_if *target_if;
        if( target_if = if_dst_check(sr, arp_header->ar_tip) )
        {
            if( !in_cache ) {
                if( arp_cache_add() ) {
                    struct arpq_entry *new_ent;
                    if( new_ent = arp_queue_lookup() )
                        arpq_entry_clear;
                }
            }
            else perror("ARP request not added to cache");
        }
        
        if( ntohs(arp_header->ar_op == ARP_REQUEST) )
        {
            arp_create_reply;
            sr_send_packet;
        }
    }

    
        
    else perror("Unknown protocol");
        
    //encapsulate and send datagram, if appropriate
    if (send_datagram != NULL) {
        encapsulate(src, send_frame, send_datagram, dest_mac);
        sr_send_packet(sr, send_frame, sizeof(send_frame), iface);
    }
    
    
    // time to deal with the queue
    /*
     * 1) iterate through the queue
     *
     * 2) check whether there is now a reply in the ARP cache, and send everything for that entry if there is
     *
     * 3) if not, check whether stuff in the queue has timed out
     *  - if it has, max retrys?
     *  - clear it out if it has, set retry++ if we haven't, send another ARP request
     *  - if we clear it out, we need to send an ICMP time exceeded message
     *
     * 4) if it hasn't timed out, just ignore it
     */
    
    
    
}/* end sr_handlepacket */























