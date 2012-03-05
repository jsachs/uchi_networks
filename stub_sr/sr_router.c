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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"


#define WORD_SIZE 4
#define ARP_MAX_REQ 5
#define ETHER_ADDR_LEN 6
#define ICMP_HDR_LEN 8
#define ICMP_DATA_LEN 8
#define ARP_CACHE_TIMEOUT 15
#define INIT_TTL 255

/* define some constants for ICMP types/codes and probably some other codes later on */
#define ECHO_REPLY 8
#define DEST_UNREACH 3
#define TIME_EXCEEDED 11

#define HOST_UNREACH 1
#define PORT_UNREACH 3
#define TIME_INTRANSIT 0
#define ECHO_CODE 0

/* for size calculations */
#define WORDTO16BIT 2
#define BYTETO16BIT 2
#define WORDTOBYTE  4


struct icmp_hdr {
    uint8_t  icmp_type;
    uint8_t  icmp_code;
    uint16_t icmp_sum;
    uint32_t icmp_unused;
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
    //char     icmp_if_name[10 /* change later */];
    struct   queued_packet *next;
};

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
 * Method: compute_checksum(uint16_t const ipHeader[], int nWords)
 * Scope:  Local
 *
 * calculates a checksum value for IP headers and ICMP messages
 *
 * algorithm taken from www.netrino.com
 *---------------------------------------------------------------------*/
static uint16_t compute_checksum(uint16_t *ip_header, size_t len)
{
    assert(ip_header);
    
    uint16_t sum = 0; // right size?

    while(len--) sum += *(ip_header++);

    if(len%2) sum += *((uint8_t*) ip_header); // assuming even number of bytes
    
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
    assert(iface);
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
 * Method: encapsulate
 *
 *---------------------------------------------------------------------*/
void encapsulate(struct sr_if *iface, uint16_t prot, void *send_frame, void *send_datagram, size_t gram_size, uint8_t dest_mac[ETHER_ADDR_LEN]){
    send_frame = calloc(gram_size + sizeof(struct sr_ethernet_hdr), sizeof(char));
    struct sr_ethernet_hdr *ether_header = (struct sr_ethernet_hdr *)send_frame;
    memcpy(ether_header->ether_shost, iface->addr, ETHER_ADDR_LEN * sizeof(uint8_t));
    memcpy(ether_header->ether_dhost, dest_mac, ETHER_ADDR_LEN * sizeof(uint8_t));
    ether_header->ether_type = htons(prot);
    memcpy(send_frame + sizeof(struct sr_ethernet_hdr), send_datagram, gram_size);
    return;
}

/*--------------------------------------------------------------------- 
 * Method: rt_match
 *
 *---------------------------------------------------------------------*/
static struct sr_rt *rt_match(struct sr_instance *sr, uint8_t *addr)
{
    assert(sr);
    assert(addr);
    
    struct sr_rt *rt_ent, *rt_def = NULL, *rt_best = NULL;
    uint8_t *addr_b, *rt_b, *mask;
    uint8_t match, mismatch = 0, count = 0, longest = 0;
    
    for( rt_ent = sr->routing_table; rt_ent; rt_ent = rt_ent->next )
    {
        if(!rt_def) {
            if((rt_ent->dest).s_addr == 0)
                rt_def = rt_ent;
        }
        
        addr_b = addr;
        rt_b = (uint8_t *) &((rt_ent->dest).s_addr);
        mask = (uint8_t *) &((rt_ent->mask).s_addr);
        
        for(; addr_b < addr + WORDTOBYTE; addr_b++, rt_b++, mask++)
        {
            if (!(match = (*addr_b)&(*mask))) break;
            
            if(match != *rt_b) {
                mismatch = 1;
                break;
            }
            count += 1;
        }
        
        if (mismatch) mismatch = 0;
        
        else if(count > longest) {
            longest = count;
            rt_best = rt_ent;
        }
        count = 0;
    }
    if (rt_best) return rt_best;
    return rt_def;
}

/*--------------------------------------------------------------------- 
 * Method: arp_cache_update;
 *
 *---------------------------------------------------------------------*/
static void arp_cache_update(struct arpc_entry *entry, unsigned char *mac)
{
    assert(entry);
    assert(mac);
    
    memcpy(entry->arpc_mac, mac, ETHER_ADDR_LEN);
    entry->arpc_timeout = time(NULL) + ARP_CACHE_TIMEOUT;
    
    return;
}

/*--------------------------------------------------------------------- 
 * Method: arp_cache_add;
 *
 *---------------------------------------------------------------------*/
static struct arpc_entry *arp_cache_add(struct arp_cache *cache, unsigned char *mac, uint32_t ip)
{
    assert(cache);
    assert(mac);
    
    struct arpc_entry *entry;
    if( entry = ((struct arpc_entry*) malloc(sizeof(struct arpc_entry))) )
    {
        memcpy(entry->arpc_mac, mac, ETHER_ADDR_LEN);
        entry->arpc_ip = ip;
        entry->arpc_timeout = time(NULL) + ARP_CACHE_TIMEOUT;
        
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
    
    while( qpacket = ((entry->arpq_packets).first) )
    {
        memcpy(( (struct sr_ethernet_hdr *) qpacket->packet)->ether_shost, (entry->arpq_if)->addr, ETHER_ADDR_LEN);
        memcpy(( (struct sr_ethernet_hdr *) qpacket->packet)->ether_dhost, d_ha, ETHER_ADDR_LEN);
        
        struct ip *s_ip = (struct ip *) ( qpacket->packet + sizeof(struct sr_ethernet_hdr));
        s_ip->ip_sum = 0;
        s_ip->ip_sum = compute_checksum( (uint16_t *) s_ip, s_ip->ip_hl * WORD_SIZE);
        
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
 * Method: arp_create;
 *
 *---------------------------------------------------------------------*/
static void arp_create(struct sr_arphdr *arp_header, struct sr_if *s_if, uint32_t ip, unsigned short op)
{
    assert(arp_header);
    assert(s_if);
    
    arp_header->ar_hrd = htons(1);
    arp_header->ar_pro = htons(ETHERTYPE_IP);
    arp_header->ar_hln = ETHER_ADDR_LEN;
    arp_header->ar_pln = 4;
    memcpy(arp_header->ar_tha, arp_header->ar_sha, ETHER_ADDR_LEN); //this will give nonsense for an arp request which is fine 
    memcpy(arp_header->ar_sha, s_if->addr, ETHER_ADDR_LEN);
    arp_header->ar_tip = ip; //this should be passed as an argument
    arp_header->ar_sip = s_if->ip;
    arp_header->ar_op = htons(op);
    
    return;
}

/*--------------------------------------------------------------------- 
 * Method: arpq_add_packet
 *
 *---------------------------------------------------------------------*/
static struct queued_packet *arpq_add_packet(struct arpq_entry *entry, uint8_t *packet, unsigned p_len /*, char *if_name*/)
{
    assert(entry);
    assert(packet);
    //assert(if_name);
    
    struct queued_packet *new_packet;
    
    if(new_packet = (struct queued_packet *)malloc(sizeof(struct queued_packet)))
    {
        new_packet->packet = (uint8_t *)malloc(p_len);
        new_packet->len = p_len;
        
        //memcpy(new_packet->icmp_if_name, if_name, sr_IFACE_NAMELEN);
        memcpy(new_packet->packet, packet, p_len);
        
        new_packet->next = NULL;
        
        if( (entry->arpq_packets).first) ((entry->arpq_packets).last)->next = new_packet;
        else (entry->arpq_packets).first = new_packet;
        (entry->arpq_packets).last = new_packet;
    }
    return new_packet;
}

/*--------------------------------------------------------------------- 
 * Method: arpq_add_entry
 *
 *---------------------------------------------------------------------*/
static struct arpq_entry *arpq_add_entry(struct arp_queue *queue, struct sr_if *iface, /* char *icmp_if, */uint8_t *packet, uint32_t ip, unsigned len)
{
    assert(queue);
    assert(iface);
    //assert(icmp_if);
    assert(packet);
    
    struct arpq_entry *new_entry;
    
    if(new_entry = (struct arpq_entry *)malloc(sizeof(struct arpq_entry)))
    {
        new_entry->arpq_if = iface;
        new_entry->arpq_ip = ip;
        (new_entry->arpq_packets).first = NULL;
        (new_entry->arpq_packets).last = NULL;
        
        if(arpq_add_packet(new_entry, packet, len /*, icmp_if*/))
        {
            new_entry->arpq_num_reqs = 1;
            new_entry->next = NULL;
            new_entry->prev = queue->last;
            
            if (queue->first) (queue->last)->next = new_entry;
            else queue->first = new_entry;
            queue->last = new_entry;
            
            return new_entry;
        }
    }
    return NULL;
}

/*--------------------------------------------------------------------- 
 * Method: void update_arp_queue(struct sr_instance *sr, struct arpq_entry *queue_entry, struct arp_cache *cache) 
 * 
 * time to deal with the queue
 *
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
 *--------------------------------------------------------------------*/
void update_arp_queue(struct sr_instance *sr, struct arp_queue *queue, struct arp_cache *cache){
    assert(queue);
    struct arpq_entry *queue_entry = queue.first;
    while (queue_entry){
        
        /* check whether there's a reply in the cache */
        struct arpc_entry *cache_entry;
        if (cache_entry = arp_cache_lookup(cache->first, queue_entry->arpq_ip)){
            /* send everything in the packet queue */
            send_queued_packets(sr, queue_entry, cache_entry);
            arpq_entry_clear(sr, queue, queue_entry, cache_entry->arpc_mac);
        }
        else{
           
        }
        entry = entry->next;
    }
}

/*---------------------------------------------------------------------
 * Method: void send_queued_packets(struct sr_instance *sr, struct arpq_entry queue_entry, struct arpq_entry cache_entry)
 *
 * Scope: Local
 *
 * This method sends out every packet queued in the given entry to the MAC address specified in the cache
 *--------------------------------------------------------------------*/

void send_queued_packets(struct sr_instance *sr, struct arpq_entry queue_entry, struct arpc_entry cache_entry){
    
    void *send_frame;
    
    struct sr_if *iface = queue_entry->arpq_if;
    
    uint8_t dest_mac[ETHER_ADDR_LEN];
    memcpy(dest_mac, arpc_mac, ETHER_ADDR_LEN);
    
    void *packet = (queue_entry->arpq_packets).first;
    
    while (packet){
        encapsulate(iface, ETHERTYPE_IP, send_frame, packet->packet, packet->len, dest_mac);
        sr_send_packet(sr, (uint8_t *)send_frame, sizeof(send_frame), iface);
        free(send_frame);
        packet = packet->next;
    }
}


/*---------------------------------------------------------------------
 * Method: update_ip_hdr(struct sr_instance *sr, void *recv_datagram,
 *                          void *send_datagram, struct sr_iface *iface)
 * Scope: Local
 * 
 * This method fills in the IP header on send_datagram based on the 
 * parameters given.
 *--------------------------------------------------------------------*/
void update_ip_hdr(struct sr_instance *sr, void *recv_datagram, void *send_datagram, struct sr_if *iface){
    assert(recv_datagram);
    struct ip *recv_header = (struct ip *)recv_datagram;
    size_t ip_len = ntohs(recv_header->ip_len);
    size_t ip_header_len = recv_header->ip_hl * WORDTO16BIT;
    send_datagram = malloc(ip_len);
    struct ip *send_header = (struct ip *)send_datagram;
    memcpy(send_datagram, recv_datagram, ip_len);
    
    //also needs to do routing table lookup and set iface
    struct sr_rt *router_entry = rt_match(sr, ntohl((recv_header->ip_dst).s_addr));
    iface = if_name_search(sr, router_entry->interface);
    
    //update TTL and checksum
    send_header->ip_ttl--;
    send_header->ip_sum = 0;
    send_header->ip_sum = compute_checksum((uint16_t*) send_datagram, ip_header_len); //we want length in 16-bit words
    
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
void ip_header_create(struct sr_instance *sr, struct ip *send_header, struct in_addr dst, struct in_addr src, size_t length)
{
    assert(send_header);
    
    
    send_header->ip_v = 4;
    send_header->ip_hl = 5;
    send_header->ip_tos = 0;
    send_header->ip_id = 0;
    send_header->ip_off = 0;
    send_header->ip_ttl = INIT_TTL;
    send_header->ip_p = IPPROTO_ICMP;
    send_header->ip_len = length;
    send_header->ip_src = dst;
    send_header->ip_dst = src;
    send_header->ip_sum = 0;
    send_header->ip_sum = compute_checksum((uint16_t*) send_header, send_header->ip_hl * WORDTO16BIT); 
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
    assert(recv_datagram);
    
    /* need to get pointers to various things */
    struct ip *recv_header = (struct ip *)recv_datagram;
    struct in_addr ip_dst, ip_src;
    ip_dst.s_addr = recv_header->ip_src.s_addr;
    ip_src.s_addr = recv_header->ip_dst.s_addr;
    
    /* get some useful info about lengths */
    size_t ip_length = ntohs(recv_header->ip_len); //in bytes   
    size_t icmp_length = ip_length / BYTETO16BIT - recv_header->ip_hl * WORDTO16BIT; //in 16-bit words
    
    /* actually create new packet */
    send_datagram = calloc(ip_length, sizeof(char));
    struct ip *send_header = (struct ip *)send_datagram;
    memcpy(send_datagram, recv_datagram, ip_length);
    struct icmp_hdr *icmp_header = (struct icmp_hdr *)(send_datagram + recv_header->ip_hl * WORDTOBYTE);
    
    assert(icmp_header);
    
    icmp_header->icmp_type = 0;
    icmp_header->icmp_code = 0;
    icmp_header->icmp_sum = 0;
    
    //create a copy and pad it if data length is odd
    if (!icmp_length%2)
        icmp_length++;
    
    void *checksum_copy = calloc(icmp_length * BYTETO16BIT, sizeof(char)); 
    memcpy(checksum_copy, icmp_header, icmp_length * BYTETO16BIT); 
    
    icmp_header->icmp_sum = compute_checksum(checksum_copy, icmp_length);
    ip_header_create(sr, send_header, ip_dst, ip_src, ip_length); //this function only create headers for ICMP packets 
    
    free(checksum_copy);
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
    struct in_addr ip_dst, ip_src;
    ip_dst.s_addr = recv_header->ip_src.s_addr;
    ip_src.s_addr = recv_header->ip_dst.s_addr;
    
    /* get some useful info about lengths */
    size_t icmp_data_len = recv_header->ip_hl * WORDTOBYTE + 2 * sizeof(uint32_t);      //length of echo data, in bytes
    size_t icmp_size_16bit = (sizeof(struct icmp_hdr) + icmp_data_len) / BYTETO16BIT;   //length of ICMP header and data, in 16-bit words
    
    /* packet includes IP header, ICMP header, and beginning of original packet */
    send_datagram = malloc(sizeof(struct ip) + sizeof(struct icmp_hdr) + icmp_data_len);  
    struct ip *send_header = (struct ip *)send_datagram;
    struct icmp_hdr *icmp_header = (struct icmp_hdr *)(send_datagram + sizeof(struct ip));
    void *icmp_data = icmp_header + sizeof(icmp_header);
    
    assert (icmp_header);
    assert (icmp_data);
    
    icmp_header->icmp_type = icmp_type;
    icmp_header->icmp_code = icmp_code;
    icmp_header->icmp_unused = 0;
    icmp_header->icmp_sum = 0;
    memcpy(icmp_data, recv_datagram, icmp_data_len);
    icmp_header->icmp_sum = compute_checksum((uint16_t *)icmp_header, icmp_size_16bit);
    ip_header_create(sr, send_header, ip_dst, ip_src, sizeof(struct ip) + sizeof(struct icmp_hdr) + icmp_data_len);
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
                     uint8_t* packet/* lent */,
                     unsigned int len,
                     char* interface/* lent */)
{
    /* REQUIRES */
    assert(sr);
    assert(packet);
    assert(interface);
    
    printf("*** -> Received packet of length %d \n",len);
    
    struct sr_ethernet_hdr *recv_frame = (struct sr_ethernet_hdr *)packet;
    void *send_frame;
    uint8_t dest_mac[ETHER_ADDR_LEN];
    uint16_t ether_prot = ETHERTYPE_IP;
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
        
        recv_hdr = (struct ip *)recv_datagram;
        
        /* Are we the destination? */
        if (iface = if_dst_check(sr, (uint32_t) (recv_hdr->ip_dst).s_addr)){  
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
            if (ntohl(recv_hdr->ip_ttl) <= 0){
                generate_icmp_error(sr, recv_datagram, send_datagram, TIME_EXCEEDED, TIME_INTRANSIT);
                memcpy(dest_mac, recv_frame->ether_shost, ETHER_ADDR_LEN * sizeof(uint8_t));
            }
            else {
                uint32_t send_ip = (recv_hdr->ip_dst).s_addr;
                /* update and forward packet; if necessary, add it to queue */
                struct arpc_entry *incache;
                //send_datagram is just a copy of recv_datagram with header updated; call to routing table lookup and checksum will be in this function
                //also sets iface
                update_ip_hdr(sr, recv_datagram, send_datagram, iface); 

                incache = arp_cache_lookup(sr_arp_cache.first, send_ip);
                
                
                if (!incache){
                    
                    if (arp_queue_lookup(sr_arp_queue.first, send_ip)){ //if we've already sent an ARP request about this IP
                        arpq_add_entry(&sr_arp_queue, iface, (uint8_t *)send_datagram, send_ip, ntohs(recv_hdr->ip_len)); 
                    }
                    else arpq_add_packet(sr_arp_queue.first, (uint8_t *)send_datagram, ntohs (recv_hdr->ip_len));
                
                    free(send_datagram);
                    
                    /* make ARP request */
                    send_datagram = malloc(sizeof(struct sr_arphdr));
                    arp_create((struct sr_arphdr *)send_datagram, iface, send_ip, ARP_REQUEST); //send datagram will now point to an ARP packet, not to the IP datagram--still need to write this
                    memset(dest_mac, 0xFF, ETHER_ADDR_LEN); //set dest mac to broadcast address
                    ether_prot = ETHERTYPE_ARP;
                }
                else
                    //set dest mac appropriately
                    memcpy(dest_mac, incache->arpc_mac, ETHER_ADDR_LEN);
            }
        }
    }
    else if ( ((struct sr_ethernet_hdr *)packet)->ether_type == ETHERTYPE_ARP)
    {
        struct sr_arphdr *arp_header = (struct sr_arphdr*) recv_datagram;
        if( ntohs(arp_header->ar_hrd) != ARPHDR_ETHER || ntohs(arp_header->ar_pro) != ETHERTYPE_ARP)
            return;
        
        uint8_t in_cache = 0;
        
        struct arpc_entry *arpc_ent;
        if( arpc_ent = arp_cache_lookup(sr_arp_cache.first, arp_header->ar_sip) )
        {
            arp_cache_update(arpc_ent, arp_header->ar_sha);
            in_cache = 1;
        }
        
        struct sr_if *target_if;
        if( target_if = if_dst_check(sr, arp_header->ar_tip) )
        {
            if( !in_cache ) {
                if( arp_cache_add(&sr_arp_cache, arp_header->ar_sha, arp_header->ar_sip) ) {
                    struct arpq_entry *new_ent;
                    if( new_ent = arp_queue_lookup(sr_arp_queue.first, arp_header->ar_sip) )
                        arpq_entry_clear(sr, &sr_arp_queue, new_ent, arp_header->ar_sha);
                }
            }
            else perror("ARP request not added to cache");
        }
        if( ntohs(arp_header->ar_op == ARP_REQUEST) )
        {
            arp_create( arp_header, target_if, arp_header->ar_sip, ARP_REPLY);
            /* sr_send_packet ? */
            ether_prot = ETHERTYPE_ARP;
        }
    }

    
        
    else perror("Unknown protocol");
        
    //encapsulate and send datagram, if appropriate
    if (send_datagram != NULL) {
        size_t datagram_size;
        if (ether_prot == ETHERTYPE_IP)
            datagram_size = ((struct ip *)send_datagram)->ip_len;
        else
            datagram_size = sizeof(struct sr_arphdr);
        encapsulate(iface, ether_prot, send_frame, send_datagram, datagram_size, dest_mac);
        sr_send_packet(sr, (uint8_t *)send_frame, sizeof(send_frame), (char *) iface);
    }
    
    update_arp_queue(sr, &sr_arp_queue, &sr_arp_cache); 
    
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


























