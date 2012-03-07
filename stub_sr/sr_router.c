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
#include <netinet/in.h>
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
#define ARP_REQUEST_TIMEOUT 1
#define INIT_TTL 255

/* define some constants for ICMP types/codes and probably some other codes later on */
#define ECHO_REPLY 0
#define ECHO_REQUEST 8
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

/* for frame_t */
#define IN 0
#define OUT 1


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
    uint8_t from_MAC[ETHER_ADDR_LEN];
    struct  sr_if *from_iface;
    struct  frame_t *outgoing;
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

struct frame_t {
    void *frame; //the actual frame
    struct sr_ethernet_hdr *ether_header; //pointer to ethernet header (same actual pointer as frame)
    struct ip *ip_header; //pointer to ip header--NULL if not IP
    struct sr_arphdr *arp_header; //pointer to arp header--NULL if not ARP
    struct icmp_hdr *icmp_header; //pointer to ICMP header--NULL if not ICMP
    int in_or_out; //flag for whether this is an incoming or outgoing packet
    uint8_t from_MAC[ETHER_ADDR_LEN];
    uint8_t to_MAC[ETHER_ADDR_LEN];
    int MAC_set; //flag for whether MAC has been determined yet, for outgoing packets
    uint32_t from_ip;
    uint32_t to_ip;
    struct sr_if *iface;
    size_t len; //total size of frame
    size_t ip_hl; //in bytes!
    size_t ip_len; //these two zero unless it's an IP datagram
};

static struct arp_cache sr_arp_cache = {0, 0};

static struct arp_queue sr_arp_queue = {0, 0};


/*--------------------------------------------------------------------- 
 * Method: get_interface
 *
 *---------------------------------------------------------------------*/
static struct sr_if *get_interface(struct sr_instance *sr, const char *if_name)
{
    assert(sr);
    assert(if_name);
    
    struct sr_if *iface = sr->if_list;
    
    while(iface)
    {
        if(!strncmp(iface->name, if_name, sr_IFACE_NAMELEN))
            return iface;
        iface = iface->next;
    }
    return NULL;
}

/*---------------------------------------------------------------------
 * Method: static struct frame_t *create_frame_t(struct sr_instance *sr, 
 *                                               void *frame, size_t len,
 *                                               char *if_name)
 * 
 * This method sets up a new frame_t structure based on an incoming packet
 *---------------------------------------------------------------------*/

static struct frame_t *create_frame_t(struct sr_instance *sr, void *frame, size_t len, char *if_name)
{
    struct frame_t *new_frame = (struct frame_t *)malloc(sizeof(struct frame_t));
    
    assert(new_frame);
    
    new_frame->frame = malloc(len);
    
    assert(new_frame->frame);
    
    memcpy(new_frame->frame, frame, len); //we make a copy of the frame so we can keep it around in queue, etc
    new_frame->len = len;
    new_frame->ether_header = (struct sr_ethernet_hdr *)new_frame->frame;
    new_frame->in_or_out = IN;
    new_frame->MAC_set = 1;
    memcpy(new_frame->from_MAC, new_frame->ether_header->ether_shost, ETHER_ADDR_LEN);
    memcpy(new_frame->to_MAC, new_frame->ether_header->ether_dhost, ETHER_ADDR_LEN);
    new_frame->icmp_header = NULL;
    new_frame->ip_len = 0;
    new_frame->ip_hl = 0;
    
    if (ntohs(new_frame->ether_header->ether_type)==ETHERTYPE_IP){
        new_frame->ip_header = (struct ip *)(new_frame->frame + sizeof(struct sr_ethernet_hdr));
        new_frame->arp_header = NULL;
        
        assert (new_frame->ip_header);
        
        new_frame->ip_len = ntohs(new_frame->ip_header->ip_len);
        new_frame->ip_hl = ntohl(new_frame->ip_header->ip_hl) * WORDTOBYTE;
        new_frame->from_ip = new_frame->ip_header->ip_src.s_addr;
        new_frame->to_ip = new_frame->ip_header->ip_dst.s_addr;
        if (new_frame->ip_header->ip_p == IPPROTO_ICMP)
            new_frame->icmp_header = (struct icmp_hdr *)(new_frame->ip_header + new_frame->ip_hl);
    }
    else if(ntohs(new_frame->ether_header->ether_type)==ETHERTYPE_ARP){
        new_frame->arp_header = (struct sr_arphdr *) (new_frame->frame + sizeof(struct sr_ethernet_hdr));
        new_frame->ip_header = NULL;
        
        assert(new_frame->arp_header);
        
        new_frame->from_ip = new_frame->arp_header->ar_sip;
        new_frame->to_ip = new_frame->arp_header->ar_tip;
    }
    else{
        perror("Unrecognized protocol");
        free(new_frame->frame);
        free(new_frame);
        return NULL;
    }
    new_frame->iface = get_interface(sr, if_name);
    
    return new_frame;
}

/*---------------------------------------------------------------------
 * Method: void destroy_frame_t(struct frame_t *frame)
 *
 * Memory cleanup
 *----------------------------------------------------------------------*/
void destroy_frame_t(struct frame_t *frame){
    free(frame->frame);
    free(frame);
}

/*---------------------------------------------------------------------
 * Method: void destroy_arpq_entry(struct arpq_entry *entry)
 *
 * Memory cleanup--assuming it's no longer linked to things
 *----------------------------------------------------------------------*/
void destroy_arpq_entry(struct arpq_entry *entry){
    struct queued_packet *packet = entry->arpq_packets.first;
    struct queued_packet *nextpacket;
    while (packet){
        destroy_frame_t(packet->outgoing);
        nextpacket = packet->next;
        free(packet);
        packet = nextpacket;
    }
    free(entry);
}

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
    struct arpc_entry *empty_arpc_entry = (struct arpc_entry *)malloc(sizeof(struct arpc_entry));
    empty_arpc_entry->arpc_ip = 0;
    empty_arpc_entry->arpc_timeout = 0;
    empty_arpc_entry->prev = NULL;
    empty_arpc_entry->next = NULL;
    for(int i = 0; i < ETHER_ADDR_LEN; i++)
        empty_arpc_entry->arpc_mac[i] = 0xff;
    assert(empty_arpc_entry);
    sr_arp_cache.first = empty_arpc_entry;
    
    // initialize ARP queue
    struct arpq_entry *empty_arpq_entry = (struct arpq_entry *)malloc(sizeof(struct arpq_entry));
    empty_arpq_entry->arpq_ip = 0;
    empty_arpq_entry->arpq_if = NULL;
    empty_arpq_entry->arpq_last_req = 0;
    empty_arpq_entry->arpq_num_reqs = 0;
    empty_arpq_entry->prev = NULL;
    empty_arpq_entry->next = NULL;

    assert(empty_arpq_entry);
    sr_arp_queue.first = empty_arpq_entry;
    
} /* -- sr_init -- */

/*--------------------------------------------------------------------- 
 * Method: compute_icmp_checksum
 * Scope:  Local
 *
 * calculates a checksum value for ICMP headers
 *
 * 
 *---------------------------------------------------------------------*/
static void compute_icmp_checksum(struct frame_t *frame)
{
    struct icmp_hdr *icmp_header = frame->icmp_header;
    uint8_t *packet = (uint8_t *) icmp_header;
    int len = frame->len - sizeof(struct sr_ethernet_hdr) - frame->ip_hl;
    
    if (!len%2) len++;
    
    uint32_t sum = 0;
    icmp_header->icmp_sum = 0;
    
    uint16_t *tmp = (uint16_t *) packet;
    
    int i;
    for (i = 0; i < len / 2; i++) {
        sum += tmp[i];
    }
    
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    
    icmp_header->icmp_sum = ~sum;
}

/*--------------------------------------------------------------------- 
 * Method: compute_ip_checksum
 * Scope:  Local
 *
 * calculates a checksum value for IP headers
 *
 * 
 *---------------------------------------------------------------------*/
static void compute_ip_checksum(struct frame_t *frame)
{
    struct ip *ip_header = frame->ip_header;
    
    uint32_t sum = 0;
    ip_header->ip_sum = 0;
    
    uint16_t *temp = (uint16_t *) ip_header;
    
    if (!ip_header->ip_hl%2) ip_header->ip_hl++;
    
    int i;
    for (i = 0; i < ip_header->ip_hl * 2; i++)
        sum += temp[i];
    
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    
    ip_header->ip_sum = ~sum;
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
    while( entry && (ip != entry->arpq_ip) )
        entry = entry->next;
    
    if (entry) return entry;
    
    return NULL;
}

/*--------------------------------------------------------------------- 
 * Method: encapsulate(struct frame_t *outgoing)
 *
 * This method fills in the ethernet header fields for the packet
 *---------------------------------------------------------------------*/
void encapsulate(struct frame_t *outgoing){
    assert(outgoing);
    struct sr_ethernet_hdr *ether_header = outgoing->ether_header;
    memcpy(ether_header->ether_shost, outgoing->from_MAC, ETHER_ADDR_LEN);
    memcpy(ether_header->ether_dhost, outgoing->to_MAC, ETHER_ADDR_LEN);
    if (outgoing->ip_header)
        ether_header->ether_type = htons(ETHERTYPE_IP);
    else
        ether_header->ether_type = htons(ETHERTYPE_ARP);
    return;
}

/*--------------------------------------------------------------------- 
 * Method: rt_match
 *
 *---------------------------------------------------------------------*/
static struct sr_rt *rt_match(struct sr_instance *sr, uint32_t addr)
{
    struct sr_rt *curr = sr->routing_table;
    struct sr_rt *best = NULL;
    while (curr)
    {
        uint32_t subnet = curr->dest.s_addr & curr->mask.s_addr;
        if (subnet == (addr & curr->mask.s_addr))
        {
            if (!best) best = curr;
            else
            {
                /* compare number of bits in mask: more bits -> bigger number */
                if (ntohl(curr->mask.s_addr) > ntohl(best->mask.s_addr))
                    best = curr;
            }
        }
        curr = curr->next;
    }
    return best;
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
 * Method: ip_header_create(struct frame_t *outgoing)
 * Scope: Local
 * 
 * This method fills in the IP header on send_datagram based on the 
 * parameters given.
 *--------------------------------------------------------------------*/
void ip_header_create(struct frame_t *outgoing)
{
    assert(outgoing);
    
    struct ip *send_header = outgoing->ip_header;
    
    assert(send_header);
    
    //yeah, this is totally lame.
    unsigned int four = 4;
    unsigned int five = 5;
    
    send_header->ip_v = htonl(four);
    send_header->ip_hl = htonl(five);
    send_header->ip_tos = 0;
    send_header->ip_id = 0;
    send_header->ip_off = 0;
    send_header->ip_ttl = INIT_TTL;
    send_header->ip_p = IPPROTO_ICMP;
    send_header->ip_len = htons(outgoing->ip_len);
    send_header->ip_src.s_addr = outgoing->to_ip;
    send_header->ip_dst.s_addr = outgoing->to_ip;
    send_header->ip_sum = 0;
    compute_ip_checksum(outgoing); 
    return;
}

/*--------------------------------------------------------------------- 
 * Method: generate_icmp_echo(struct frame_t *incoming, 
 *                            struct frame_t *outgoing)
 * Scope: Local
 *
 * This method takes an IP datagram containing an ICMP echo request
 * as an argument and returns an ICMP echo reply.
 *---------------------------------------------------------------------*/

static struct frame_t *generate_icmp_echo(struct frame_t *incoming){
    
    assert(incoming);
    
    
    /* create and fill out frame_t */
    struct frame_t *outgoing = malloc(sizeof(struct frame_t));
    
    assert(outgoing);
    
    outgoing->frame = malloc(incoming->len);
    
    assert(outgoing->frame);
    
    memcpy(outgoing->frame, incoming->frame, incoming->len);
    outgoing->ether_header = (struct sr_ethernet_hdr *)outgoing->frame;
    outgoing->ip_header = (struct ip *)(outgoing->frame + sizeof(struct sr_ethernet_hdr));
    outgoing->arp_header = NULL;
    
    assert(outgoing->ip_header);
    
    outgoing->icmp_header = (struct icmp_hdr *)(outgoing->ip_header + outgoing->ip_hl);
    
    assert(outgoing->icmp_header);
    
    outgoing->in_or_out = OUT;
    memcpy(outgoing->from_MAC, incoming->to_MAC, ETHER_ADDR_LEN);
    memcpy(outgoing->to_MAC, incoming->from_MAC, ETHER_ADDR_LEN);
    outgoing->MAC_set = 1;
    outgoing->from_ip = incoming->to_ip;
    outgoing->to_ip = incoming->from_ip;
    outgoing->iface = incoming->iface;
    outgoing->len = incoming->len;
    outgoing->ip_len = incoming->ip_len;
    outgoing->ip_hl = 5;
    
    
    /* fill out icmp header */
    outgoing->icmp_header->icmp_type = ECHO_REPLY;
    outgoing->icmp_header->icmp_sum = 0;
    
    /* fill out other headers too */
    compute_icmp_checksum(outgoing); //again, this will need updating, ideally it'll only take outgoing as argument
    
    ip_header_create(outgoing); //this function only create headers for ICMP packets 
    encapsulate(outgoing); //memory and such already allocated, just fills in fields appropriately
    
    return outgoing;
    
}

/*---------------------------------------------------------------------
 * Method: generate_icmp_error(struct sr_instance *sr, 
 *                             void *recv_datagram, void *send_datagram, 
 *                             uint16_t type, uint16_t code)
 * Scope: Local
 * This method takes an IP datagram and generates an ICMP error message
 * for it according to the given type and code.
 *---------------------------------------------------------------------*/

static struct frame_t *generate_icmp_error(struct frame_t *incoming, uint16_t icmp_type, uint16_t icmp_code){
    
    assert (incoming);
    
    /* need to get pointers to various things */
    //struct ip *recv_header = (struct ip *)recv_datagram;
    //struct in_addr ip_dst, ip_src;
    //ip_dst.s_addr = recv_header->ip_src.s_addr;
    //ip_src.s_addr = recv_header->ip_dst.s_addr;
    
    /* get some useful info about lengths */
    size_t icmp_data_len = incoming->ip_hl + 2 * sizeof(uint32_t);      //length of echo data, in bytes
    
    /* create and fill out frame_t */
    struct frame_t *outgoing = malloc(sizeof(struct frame_t));
    
    assert(outgoing);
    
    outgoing->len = sizeof(struct sr_ethernet_hdr) + sizeof(struct ip) + sizeof(struct icmp_hdr) + icmp_data_len;
    outgoing->frame = malloc(outgoing->len);
    outgoing->ether_header = (struct sr_ethernet_hdr *)outgoing->frame;
    
    assert(outgoing->frame);
    
    outgoing->ip_header = (struct ip *)(outgoing->frame + sizeof(struct ip));
    outgoing->arp_header = NULL;
    
    assert(outgoing->ip_header);
    
    outgoing->icmp_header = (struct icmp_hdr *)(outgoing->ip_header + sizeof(struct icmp_hdr));
    
    assert(outgoing->icmp_header);
    
    outgoing->in_or_out = OUT;
    outgoing->MAC_set = 1;
    outgoing->from_ip = incoming->to_ip;
    outgoing->to_ip = incoming->from_ip;
    memcpy(outgoing->from_MAC, incoming->to_MAC, ETHER_ADDR_LEN);
    memcpy(outgoing->to_MAC, incoming->from_MAC, ETHER_ADDR_LEN);
    outgoing->iface = incoming->iface;
    outgoing->ip_len = outgoing->len - sizeof(struct sr_ethernet_hdr);
    outgoing->ip_hl = 5;
    
    /* fill out icmp header */
    outgoing->icmp_header->icmp_type = icmp_type;
    outgoing->icmp_header->icmp_code = icmp_code;
    outgoing->icmp_header->icmp_unused = 0;
    outgoing->icmp_header->icmp_sum = 0;
    
    /* copy data into header: packet includes IP header, ICMP header, and beginning of original packet */
    void *icmp_data = outgoing->icmp_header + sizeof(outgoing->icmp_header);
    assert(icmp_data);
    memcpy(icmp_data, incoming->ip_header, icmp_data_len);
    
    compute_icmp_checksum(outgoing);
    ip_header_create(outgoing);
    encapsulate(outgoing);
    return outgoing; 
    
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
        struct frame_t *outgoing = qpacket->outgoing;
        memcpy(outgoing->from_MAC, (entry->arpq_if)->addr, ETHER_ADDR_LEN);
        memcpy(outgoing->to_MAC, d_ha, ETHER_ADDR_LEN);
        
        encapsulate(outgoing);
        sr_send_packet(sr, (uint8_t *)outgoing->frame, outgoing->len, outgoing->iface->name); 
        
        //struct ip *s_ip = (struct ip *) ( qpacket->packet + sizeof(struct sr_ethernet_hdr));
        //s_ip->ip_sum = 0;
        //s_ip->ip_sum = compute_checksum( (uint16_t *) s_ip, s_ip->ip_hl * WORD_SIZE);
        
        if( !((entry->arpq_packets).first = qpacket->next) ){
            (entry->arpq_packets).first = NULL;
            (entry->arpq_packets).last = NULL; //need to set both so destroy_arpq_entry works right
        }
        
        destroy_frame_t(outgoing);
        free(qpacket);
    }
    
    if (entry->prev) (entry->prev)->next = entry->next;
    else queue->first = entry->next;
    
    if (entry->next) (entry->next)->prev = entry->prev;
    else queue->last = entry->prev;
    
    destroy_arpq_entry(entry);
    return;
}

/*--------------------------------------------------------------------- 
 * Method: arp_create;
 *
 *---------------------------------------------------------------------*/
static struct frame_t *arp_create(struct sr_instance *sr, struct frame_t *incoming, struct sr_if *iface, unsigned short op)
{
    assert(incoming);
    
    //create and fill out frame_t
    struct frame_t *outgoing = malloc(sizeof(struct frame_t));
    
    assert(outgoing);
    
    outgoing->len = sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_arphdr);
    outgoing->frame = malloc(outgoing->len);
    outgoing->ether_header = (struct sr_ethernet_hdr *)outgoing->frame;
    
    outgoing->arp_header = (struct sr_arphdr *)(outgoing->frame + sizeof(struct sr_ethernet_hdr));
    outgoing->ip_header = NULL;
    outgoing->icmp_header = NULL;
    outgoing->ip_len = 0;
    outgoing->ip_hl = 0;
    outgoing->iface = iface;
    outgoing->MAC_set = 1;
    
    //fill out constant parts of arp_header
    outgoing->arp_header->ar_hrd = htons(1);
    outgoing->arp_header->ar_pro = htons(ETHERTYPE_IP);
    outgoing->arp_header->ar_hln = ETHER_ADDR_LEN;
    outgoing->arp_header->ar_pln = 4;
    
    //fill out MAC and IP addresses
    memcpy(outgoing->arp_header->ar_tha, incoming->from_MAC, ETHER_ADDR_LEN); //this will give nonsense for an arp request which is fine--unless we need a broadcast address, but I don't think so 
    memcpy(outgoing->arp_header->ar_sha, iface->addr, ETHER_ADDR_LEN);
    memcpy(outgoing->from_MAC, iface->addr, ETHER_ADDR_LEN);
    
    if (op == ARP_REQUEST){
        struct sr_rt *route_entry = rt_match(sr, ntohl(iface->ip));
        outgoing->arp_header->ar_tip = route_entry->dest.s_addr; //incoming is an IP datagram, we want to know MAC of next hop in routing table
        outgoing->to_ip = outgoing->arp_header->ar_tip;
        memset(outgoing->to_MAC, 0xFF, ETHER_ADDR_LEN); //set outgoing MAC to broadcast address 
    }
    else{
        outgoing->arp_header->ar_tip = incoming->from_ip; //incoming is an ARP request, we want to send back to that IP
        outgoing->to_ip = incoming->from_ip;
        memcpy(outgoing->to_MAC, incoming->from_MAC, ETHER_ADDR_LEN);
    }
    
    outgoing->arp_header->ar_sip = outgoing->iface->ip;
    outgoing->from_ip = outgoing->iface->ip;
    
    outgoing->arp_header->ar_op = htons(op);
    
    encapsulate(outgoing);
    assert(outgoing);
    
    return outgoing;
}
                                 

/*--------------------------------------------------------------------- 
 * Method: arpq_add_packet
 *
 *---------------------------------------------------------------------*/
static struct queued_packet *arpq_add_packet(struct arpq_entry *entry, struct frame_t *packet, unsigned p_len, uint8_t old_MAC[ETHER_ADDR_LEN], struct sr_if *old_iface)
{
    assert(entry);
    assert(packet);
    //assert(if_name);
    entry->arpq_num_reqs++; //this only gets called when we're about to send an ARP request
    
    struct queued_packet *new_packet;
    
    if(new_packet = (struct queued_packet *)malloc(sizeof(struct queued_packet)))
    {
        new_packet->outgoing = malloc(sizeof(struct frame_t));
        memcpy(new_packet->outgoing, packet, sizeof(struct frame_t));
        new_packet->outgoing->frame = malloc(packet->len);
        memcpy(new_packet->outgoing->frame, packet->frame, packet->len);
        new_packet->from_iface = old_iface;
        memcpy(new_packet->from_MAC,  old_MAC, ETHER_ADDR_LEN);
        
        
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
static struct arpq_entry *arpq_add_entry(struct arp_queue *queue, struct sr_if *iface, struct frame_t *packet, uint32_t ip, unsigned len, uint8_t old_MAC[ETHER_ADDR_LEN], struct sr_if *old_iface)
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
        
        if(arpq_add_packet(new_entry, packet, len, old_MAC, old_iface))
        {
            new_entry->arpq_num_reqs = 0;
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
 * Method: arpq_packets_icmpsend(struct sr_instance *sr,
 *                               struct packetq *arpq_packets)
 * 
 * Send time_exceeded messages in response to all the packets in the packet queue
 *---------------------------------------------------------------------*/

void arpq_packets_icmpsend(struct sr_instance *sr, struct packetq *arpq_packets){
    struct queued_packet *current_packet = arpq_packets->first;
    struct frame_t *ICMP_err;
    
    while(current_packet){
        //get back old info so we can send it back the way it came
        current_packet->outgoing->iface = current_packet->from_iface;
        memcpy(current_packet->outgoing->from_MAC, current_packet->from_MAC, ETHER_ADDR_LEN);
        
        ICMP_err = generate_icmp_error(current_packet->outgoing, DEST_UNREACH, HOST_UNREACH);
        
        sr_send_packet(sr, (uint8_t *)current_packet->outgoing->frame, 
                       current_packet->outgoing->len, 
                       current_packet->from_iface->name);
        
        destroy_frame_t(ICMP_err);
        current_packet = current_packet->next;
    }
}

/*--------------------------------------------------------------------- 
 * Method: arp_queue_flush
 *
 *---------------------------------------------------------------------*/
static void arp_queue_flush(struct sr_instance *sr, struct arp_queue *queue)
{
    assert(queue);
    struct arpq_entry *entry = queue->first;
    struct arpq_entry *temp = NULL;
    struct frame_t *arp_req;
    
    while(entry) {
        if( time(NULL) - 1 > entry->arpq_last_req)
        {
            if(entry->arpq_num_reqs >= ARP_MAX_REQ) {
                temp = entry;
                arpq_packets_icmpsend(sr, &entry->arpq_packets);
            }
            else {
                struct queued_packet *old_packet = entry->arpq_packets.first;
                arp_req = arp_create(sr, old_packet->outgoing, old_packet->from_iface, ARP_REQUEST);
                sr_send_packet(sr, (uint8_t *)arp_req->frame, arp_req->len, old_packet->from_iface->name);
                destroy_frame_t(arp_req);
            }
        }
        entry = entry->next;
        
        if(temp)
        {
            if (temp->prev) (temp->prev)->next = temp->next;
            else queue->first = temp->next;
            
            if (temp->next) (temp->next)->prev = temp->prev;
            else queue->last = temp->prev;
            
            free(temp);
            temp = NULL;
        }
    }
}

/*--------------------------------------------------------------------- 
 * Method: arp_cache_flush
 *
 *---------------------------------------------------------------------*/
static void arp_cache_flush(struct arp_cache *cache)
{
    assert(cache);
    
    struct arpc_entry *entry = cache->first;
    struct arpc_entry *temp;
    
    while(entry) {
        if( time(NULL) > entry->arpc_timeout)
        {
            if (entry->prev) (entry->prev)->next = entry->next;
            else cache->first = entry->next;
            
            if (entry->next) (entry->next)->prev = entry->prev;
            else cache->last = entry->prev;
            
            temp = entry;
        }
        entry = entry->next;
        if(temp) {
            free(temp);
            temp = NULL;
        }
    }
}


/*---------------------------------------------------------------------
 * Method: update_ip_hdr(struct sr_instance *sr, struct frame_t incoming,
 *                          struct frame_t outgoing)
 * Scope: Local
 * 
 * This method fills in the IP header and frame_t for outgoing based on the 
 * parameters given.
 *--------------------------------------------------------------------*/
void update_ip_hdr(struct sr_instance *sr, struct frame_t *incoming, struct frame_t *outgoing){
    assert(incoming);
    //struct ip *recv_header = (struct ip *)recv_datagram;
    //size_t ip_len = ntohs(recv_header->ip_len);
    //size_t ip_header_len = recv_header->ip_hl * WORDTO16BIT;
    //send_datagram = malloc(ip_len);
    //struct ip *send_header = (struct ip *)send_datagram;
    //memcpy(send_datagram, recv_datagram, ip_len);
    
    /* create and fill out frame_t */
    outgoing = (struct frame_t *)malloc(sizeof(struct frame_t));
    
    assert(outgoing);
    
    outgoing->frame = malloc(incoming->len);
    memcpy(outgoing->frame, incoming->frame, incoming->len);
    
    assert(outgoing->frame);
    
    outgoing->ether_header = (struct sr_ethernet_hdr *)outgoing->frame;
    outgoing->ip_header = (struct ip *)(outgoing->frame + sizeof(struct sr_ethernet_hdr));
    outgoing->arp_header = NULL;
    outgoing->icmp_header = NULL; //maybe it's an ICMP packet, but we don't care
    outgoing->in_or_out = IN;
    outgoing->MAC_set = 0;
    outgoing->from_ip = incoming->from_ip;
    outgoing->to_ip = incoming->to_ip;
    outgoing->ip_hl = incoming->ip_hl;
    outgoing->len = incoming->len;
    
    //also needs to do routing table lookup and set iface
    struct sr_rt *router_entry = rt_match(sr, ntohl(incoming->to_ip));
    outgoing->iface = get_interface(sr, router_entry->interface);
    memcpy(outgoing->from_MAC, outgoing->iface->addr, ETHER_ADDR_LEN);
    outgoing->ip_header->ip_sum = 0;
    
    //update TTL and checksum
    outgoing->ip_header->ip_ttl--;
    outgoing->ip_header->ip_sum = 0;
    compute_ip_checksum(outgoing); //we want length in 16-bit words
    
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
    
    struct sr_if *iface;
    
    struct frame_t *incoming = create_frame_t(sr, packet, len, interface);
    struct frame_t *outgoing = NULL;
    
    /* First, deal with ARP cache timeouts */
    arp_cache_flush(&sr_arp_cache);
    
    /* Do we only need to cache ARP replies, or src MAC/IP on regular IP packets too, etc? */
    /* Also, do we need to worry about fragmentation? */
    
    /* Then actually handle the packet */
    /* Start by determining protocol */
    if (incoming->ip_header){
        
        compute_ip_checksum(incoming);
        
        /* Check the checksum */
        if( incoming->ip_header->ip_sum != 0xffff ) { 
            fprintf(stderr, "IP checksum incorrect, packet was dropped\n");
            return;
        }
        

        /* Are we the destination? */
        if (iface = if_dst_check(sr, incoming->to_ip)){  //we could change this to just take incoming and then get to_ip
            printf("received IP datagram\n");
            /* Is this an ICMP packet? */
            if (incoming->icmp_header){
                printf("received ICMP datagram\n");
                if(incoming->icmp_header->icmp_type == ECHO_REQUEST){
                    outgoing = generate_icmp_echo(incoming); 
                    printf("received ICMP echo request");
                }
            }
            else{
                outgoing = generate_icmp_error(incoming, DEST_UNREACH, PORT_UNREACH);
                printf("A packet for me! Flattering, but wrong.\n");
            }
        }
        else {
            /* Has it timed out? */
            if (incoming->ip_header->ip_ttl <= 0){
                outgoing = generate_icmp_error(incoming, TIME_EXCEEDED, TIME_INTRANSIT);
                printf("Slowpoke. TTL exceeded.\n");
            }
            else {

                /* update and forward packet; if necessary, add it to queue */
                struct arpc_entry *incache;

                update_ip_hdr(sr, incoming, outgoing);
                printf("packet to forward");
                
                incache = arp_cache_lookup(sr_arp_cache.first, outgoing->to_ip);
                
                
                if (!incache){
                    
                    if (arp_queue_lookup(sr_arp_queue.first, outgoing->to_ip)){ //if we've already sent an ARP request about this IP
                        arpq_add_entry(&sr_arp_queue, outgoing->iface, outgoing, outgoing->to_ip, outgoing->ip_len, incoming->from_MAC, incoming->iface); 
                    }
                    else arpq_add_packet(sr_arp_queue.first, outgoing, outgoing->ip_len, incoming->from_MAC, incoming->iface);
                    struct sr_if *out_interface = outgoing->iface;
                
                    destroy_frame_t(outgoing);
                    
                    /* make ARP request */
                    outgoing = arp_create(sr, incoming, out_interface, ARP_REQUEST); //send datagram will now point to an ARP packet, not to the IP datagram 
                    printf("sending ARP request\n");
                }
                else{
                    printf("got the MAC, can actually send this packet\n");
                    memcpy(outgoing->to_MAC, incache->arpc_mac, ETHER_ADDR_LEN);
                    encapsulate(outgoing);
                }
            }
        }
    }
    else if ( incoming->arp_header )
    {
        printf("received ARP packet\n");
        
        struct sr_arphdr *arp_header = incoming->arp_header;
        
        //if( ntohs(arp_header->ar_hrd) != ARPHDR_ETHER || ntohs(arp_header->ar_pro) != ETHERTYPE_IP) return;
        
        uint8_t in_cache = 0;
        
        struct arpc_entry *arpc_ent = arp_cache_lookup(sr_arp_cache.first, arp_header->ar_sip);
        printf("checking the cache\n");
        if( arpc_ent )
        {
            arp_cache_update(arpc_ent, arp_header->ar_sha);
            printf("updated cache\n");
            in_cache = 1;
        }
        
        struct sr_if *target_if = if_dst_check(sr, arp_header->ar_tip);
        printf("checking the target\n");
        if( target_if )
        {
            printf("It's for us\n");
            if( !in_cache ) {
                if( arp_cache_add(&sr_arp_cache, arp_header->ar_sha, arp_header->ar_sip) ) {
                    struct arpq_entry *new_ent;
                    if( new_ent = arp_queue_lookup(sr_arp_queue.first, arp_header->ar_sip) )
                        arpq_entry_clear(sr, &sr_arp_queue, new_ent, arp_header->ar_sha);
                }
                else perror("ARP request not added to cache");
            }
            if( ntohs(arp_header->ar_op) == ARP_REQUEST ){
                outgoing = arp_create(sr, incoming, incoming->iface, ARP_REPLY);
                printf("created ARP reply\n");
                
                assert(outgoing);
            }
        }
    }
    else perror("Unknown protocol");
        
    //encapsulate and send datagram, if appropriate
    if (outgoing != NULL){ 
        sr_send_packet(sr, (uint8_t *)outgoing->frame, outgoing->len, outgoing->iface->name);
        printf("sent packet of length %d\n", outgoing->len);
    }
    
    arp_queue_flush(sr, &sr_arp_queue);
    
    
}/* end sr_handlepacket */


























