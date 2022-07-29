#include "id_queue.h"

/* stores the specified value in the queue */
void enq_id(struct id_queue *q, uint8_t v){
    pthread_mutex_lock(&q->mutex);
    /* wait if queue is full */
    while(q->remain == ID_QUEUE_SIZE) pthread_cond_wait(&q->not_full, &q->mutex);
    q->queue[q->wp] = v;
    q->wp = ((q->wp + 1) & (ID_QUEUE_SIZE -1));
    q->remain++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}
/* takes one value from the queue */
void deq_id(struct id_queue *q, uint8_t* v){
    pthread_mutex_lock(&q->mutex);
    /* wait if queue is empty */
    while(q->remain == 0) pthread_cond_wait(&q->not_empty, &q->mutex);
    *v = q->queue[q->rp];
    q->rp = ((q->rp + 1) & (ID_QUEUE_SIZE -1));
    q->remain--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

