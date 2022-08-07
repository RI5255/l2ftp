#ifndef _BLOCK_QUEUE_H
#define _BLOCK_QUEUE_H

#ifndef BLOCK_QUEUE_SIZE
#define BLOCK_QUEUE_SIZE 32
#endif 

#include <linux/if_packet.h>
#include <pthread.h>

struct block_queue{
    struct tpacket_block_desc* queue[BLOCK_QUEUE_SIZE];
    int remain;    /* number of elements in the queue */
    int rp, wp; /* used to read/write */
    pthread_mutex_t mutex; /* mutex for queue handling */
    pthread_cond_t not_empty;
    pthread_cond_t not_full ;
};

extern struct block_queue blk_q;

void setup_blkq(void);
void teardown_blkq(void);

void enq_blk(struct block_queue *q, struct tpacket_block_desc *pbd);
void deq_blk(struct block_queue *q, struct tpacket_block_desc** ppbd);

#endif 


