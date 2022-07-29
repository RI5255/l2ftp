#ifndef _PKT_QUEUE_H
#define _PKT_QUEUE_H

#ifndef PKT_QUEUE_SIZE
#define PKT_QUEUE_SIZE 32 
#endif 

#include "if_packet.h"
#include "pthread.h"

struct packet_queue{
    struct tpacket_hdr* queue[PKT_QUEUE_SIZE];
    int remain;    /* number of elements in the queue */
    int rp, wp; /* used to read/write */
    pthread_mutex_t mutex; /* mutex for queue handling */
    pthread_cond_t not_empty;
    pthread_cond_t not_full ;
};

void enq_pkt(struct packet_queue *q, struct tpacket_hdr *p);
void deq_pkt(struct packet_queue *q, struct tpacket_hdr** v);

#endif /* _PKT_QUEUE_H */


