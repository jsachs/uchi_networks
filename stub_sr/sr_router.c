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
    /*
     * IP headers always contain an even number of bytes.
     */
    while(len--) sum += *(ip_header++);
    /*
     * Use carries to compute 1's complement sum.
     */
    if(len%2) sum += *((uint8_t*) ip_header);
    
    while(sum>>16) sum = (sum >> 16) + (sum & 0xffff);
    /*
     * Return the inverted 16-bit result.
     */
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
    
    if(current_if) return current_if;
    
    return NULL;
}

/*--------------------------------------------------------------------- 
 * Method: if_ip_search
 *
 *---------------------------------------------------------------------*/
struct sr_if *if_ip_search(struct sr_if *iface, uint32_t ip);



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
    ip_header->ip_len - htons(iphdr->ip_hl * WORD_SIZE + ICMPHDR_LEN + icmpdat_len);
    (ip_header->ip_src).s_addr = s_ip;
    (ip_header->ip_dst).s_addr = d_ip;
    ip_header->ip_sum = 0;
    ip_header->ip_sum = compute_ip_checksum((uint16_t*) iphdr, iphdr->ip_hl * WORD_SIZE);
    
    icmp_header->icmp_type = type;
    icmp_header->icmp_code = code;
    icmp_header->icmp_unused = 0;
    
    memcpy(icmphdr + 1, icmp_data, icmp_data_len);
    icmphdr->icmp_sum = 0;
    icmphdr->icmp_sum = compute_ip_checksum((uint16_t*) icmphdr, ICMPHDR_LEN + icmp_data_len);
}

/*---------------------------------------------------------------------
 * Method: update_ip_hdr(struct sr_instance *sr, void *recv_datagram,
 *                          void *send_datagram, struct sr_iface *iface)
 * Scope: Local
 * 
 * This method fills in the IP header on send_datagram based on the 
 * parameters given.
 * Is there a reason we need iface? I don't think so...
 *--------------------------------------------------------------------*/
void update_ip_hdr(struct sr_instance *sr, void *recv_datagram, void *send_datagram, struct sr_iface *iface){
    assert(recv_datagram);
    struct ip *recv_header = (struct ip *)recv_datagram;
    size_t ip_len = ntohs(recv_header->ip_len) * WORD_SIZE;
    send_datagram = malloc(ip_len);
    struct ip *send_header = (struct ip *)send_datagram;
    memcpy(send_datagram, recv_datagram, ip_len);
    
    //update TTL and checksum
    send_header->ip_ttl--;
    send_header->ip_sum = 0;
    send_header->ip_sum = compute_ip_checksum((uint16_t*) send_datagram, ip_len);
    
    return;
}

/*---------------------------------------------------------------------
 * Method: ip_header_create(struct sr_instance *sr, void *send_datagram,
 *                          struct in_addr dst, struct in_addr src, 
 *                          size_t length)
 * Scope: Local
 * 
 * This method fills in the IP header on send_datagram based on the 
 * parameters given.
 *--------------------------------------------------------------------*/

void ip_header_create(struct sr_instance *sr, void *send_datagram, struct in_addr dst, struct in_addr src, size_t length){
    assert(send_datagram);
    
    struct ip *ip_header = (struct ip *)send_datagram;
    
    ip_header->ip_v = 4;
    ip_header->ip_hl = 5;
    ip_header->ip_tos = 0;
    ip_header->ip_id = 0;
    ip_header->ip_off = 0;
    ip_header->ip_ttl = 64;
    ip_header->ip_p = IPPROTO_ICMP;
    ip_header->ip_len = length;
    ip_header->ip_src = s_ip;
    ip_header->ip_dst = d_ip;
    ip_header->ip_sum = 0;
    ip_header->ip_sum = compute_ip_checksum((uint16_t*) iphdr, iphdr->ip_hl * WORD_SIZE);
    return;
}

/*--------------------------------------------------------------------- 
 * Method: generate_icmp_echo(struct sr_instance *sr, void *recv_datagram, 
 *                       void *send_datagram, int ICMPtype, int ICMPcode)
 * Scope: Local
 *
 * This method takes an IP datagram containing an ICMP echo request
 * as an argument and returns an ICMP echo reply.
 *---------------------------------------------------------------------*/

void generate_icmp_echo(struct sr_instance *sr, void *recv_datagram, void *send_datagram){
    assert(recv_datagram)
    
    /* need to get pointers to various things */
    struct ip *recv_header = (struct ip *)recv_datagram;
    struct in_addr ip_dst, ip_src;
    ip_dst.s_addr = recv_header->ip_src->s_addr;
    ip_src.s_addr = recv_header->ip_dst->s_addr;
    
    /* get some useful info about lengths */
    size_t ip_length = ntohs(recv_header->ip_len) * WORD_SIZE;
    size_t icmp_length = ip_length - sizeof(struct ip);
    
    /* actually create new packet */
    send_datagram = malloc(ip_len);
    struct ip *send_header = (struct ip *)send_datagram;
    memcpy(send_datagram, recv_datagram, ip_len);
    struct icmp_hdr *icmp_header = (struct icmp_hdr *)(send_datagram + sizeof(struct ip));
    
    assert(icmp_header);
    
    icmp_header->icmp_type = 0;
    icmp_header->checksum = 0;
    compute_icmp_checksum(icmp_header, icmp_length);
    ip_header_create(sr, send_datagram, ip_dst, ip_length); //this function only create headers for ICMP packets 
    return;
    
}

/*---------------------------------------------------------------------
 * Method: generate_icmp_error(struct sr_instance *sr, 
 *                             void *recv_datagram, void *send_datagram, 
 *                             uint16_t type, uint16_t code)
 * Scope: Local
 * This method takes an IP datagram and generates an ICMP error message
 * for it according to the given type and code.
 *---------------------------------------------------------------------*/

void generate_icmp_error(struct sr_instance *sr, void *recv_datagram, void *send_datagram, uint16_t icmp_type, uint16_t icmp_code){
    assert (recv_datagram);
    
    /* need to get pointers to various things */
    struct ip *recv_header = (struct ip *)recv_datagram;
    struct in_addr ip_dst;
    ip_dst.s_addr = recv_header->ip_src->s_addr;
    ip_src.s_addr = recv_header->ip_dst->s_addr;
    
    /* get some useful info about lengths */
    size_t ip_len = ntohs(recv_header->ip_len) * WORD_SIZE;
    size_t icmp_data_len = sizeof(struct ip) + 2 * sizeof(uint32_t);
    
    /* packet includes IP header, ICMP header, and beginning of original packet */
    send_datagram = malloc(sizeof(struct ip) + sizeof(struct icmp_hdr) + icmp_data_len);  
    struct ip *send_header = (struct ip *)send_datagram;
    struct icmp_hdr *icmp_header = (struct icmp_hdr *)(send_datagram + sizeof(struct ip));
    void *icmp_data = icmp_header + sizeof(icmp_hdr);
    
    assert(icmp_header);
    assert (icmp_data);
    
    icmp_header->type = icmp_type;
    icmp_header->code = icmp_code;
    memcpy(icmp_data, recv_datagram, icmp_data_len);
    compute_icmp_checksum(icmp_header, sizeof(struct icmp_hdr) + icmp_data_len);
    ip_header_create(sr, send_datagram, sizeof(struct ip) + sizeof(struct icmp_header) + icmp_data_len);
    return; 
    
}

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
    
    recv_datagram = packet + sizeof(struct sr_ethernet_hdr);
    
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
        if (iface = if_dst_check(sr, recv_hdr->ip_dst)){  
            /* Is this an ICMP packet? */
            if (ntohl(recv_hdr->ip_p) == IPPROTO_ICMP)
                //check whether it's an echo request, then:
                generate_icmp_echo(sr, recv_datagram, send_datagram); 
            else generate_icmp_error(sr, recv_datagram, send_datagram, DEST_UNREACH, PORT_UNREACH);
            //we're just returning to sender, so just use src mac
            memcpy(dest_mac, recv_frame->ether_shost, ETHER_ADDR_LEN * sizeof(uint8_t)); //fill in dest_mac with src from recv_frame, 
        }
        else {
            /* Has it timed out? */
            if (ntohl(recv_datagram->ip_ttl) <= 0){
                generate_icmp_error(sr, recv_datagram, send_datagram, TIME_EXCEEDED, TIME_INTRANSIT);
                memcpy(dest_mac, recv_frame->ether_shost, ETHER_ADDR_LEN * sizeof(uint8_t));
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
        struct sr_arphdr *recv_header = (struct sr_arphdr *) recv_datagram;
        is_it_for_us();
        if( recv_header->ar_op == ARP_REQUEST ) {
            // branch for requests
            // is it for us? if so:
            generate_arp(sr, send_datagram, ARP_REPLY);
            memcpy(dest_mac, recv_frame->ether_shost, ETHER_ADDR_LEN * sizeof(uint8_t));
            // else drop it
        }
        
        else if ( recv_header->ar_op == ARP_REPLY ){
            // branch for replies
            
            // add it to the ARP cache
            arpc_entry_add();
            // the cache will be dealt with at the begining
        }
        
        else send_datagram = NULL;
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


























