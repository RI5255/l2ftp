#include "packet_queue.h"

/* stores the specified value in the queue */
void enq_pkt(struct packet_queue *q, struct tpacket_hdr *p){
    pthread_mutex_lock(&q->mutex);
    /* wait if queue is full */
    while(q->remain == PKT_QUEUE_SIZE) pthread_cond_wait(&q->not_full, &q->mutex);
    q->queue[q->wp] = p;
    q->wp = ((q->wp + 1) & (PKT_QUEUE_SIZE -1));
    q->remain++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}
/* takes one value from the queue */
void deq_pkt(struct packet_queue *q, struct tpacket_hdr** v){
    pthread_mutex_lock(&q->mutex);
    /* wait if queue is empty */
    while(q->remain == 0) pthread_cond_wait(&q->not_empty, &q->mutex);
    *v = q->queue[q->rp];
    q->rp = ((q->rp + 1) & (PKT_QUEUE_SIZE -1));
    q->remain--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

