#include "block_queue.h"
#include <pthread.h>

struct block_queue blk_q;

void setup_blkq(void){
    pthread_mutex_init(&blk_q.mutex, NULL);
    pthread_cond_init(&blk_q.not_empty, NULL);
    pthread_cond_init(&blk_q.not_empty, NULL);
}

void teardown_blkq(void){
    pthread_mutex_destroy(&blk_q.mutex);
    pthread_cond_destroy(&blk_q.not_full);
    pthread_cond_destroy(&blk_q.not_empty);
}

void enq_blk(struct block_queue *q, struct tpacket_block_desc *pbd){
    pthread_mutex_lock(&q->mutex);
    /* wait if queue is full */
    while(q->remain == BLOCK_QUEUE_SIZE) pthread_cond_wait(&q->not_full, &q->mutex);
    q->queue[q->wp] = pbd;
    q->wp = (q->wp  + 1) % BLOCK_QUEUE_SIZE;
    q->remain++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

void deq_blk(struct block_queue *q, struct tpacket_block_desc** ppbd){
    pthread_mutex_lock(&q->mutex);
    /* wait if queue is empty */
    while(q->remain == 0) pthread_cond_wait(&q->not_empty, &q->mutex);
    *ppbd = q->queue[q->rp];
    q->rp = (q->rp  + 1) % BLOCK_QUEUE_SIZE;
    q->remain--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

