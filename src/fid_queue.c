#include "fid_queue.h"
#include <pthread.h>

struct fid_queue fid_q;

void setup_fidq(void){
    pthread_mutex_init(&fid_q.mutex, NULL);
    pthread_cond_init(&fid_q.not_empty, NULL);
    pthread_cond_init(&fid_q.not_empty, NULL);
}

void teardown_fidq(void){
    pthread_mutex_destroy(&fid_q.mutex);
    pthread_cond_destroy(&fid_q.not_full);
    pthread_cond_destroy(&fid_q.not_empty);
}

void enq_fid(struct fid_queue *q, uint16_t fid){
    pthread_mutex_lock(&q->mutex);
    while(q->remain == FID_QUEUE_SIZE) pthread_cond_wait(&q->not_full, &q->mutex);
    q->queue[q->wp] = fid;
    q->wp = (q->wp + 1) % FID_QUEUE_SIZE;
    q->remain++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
}

void deq_fid(struct fid_queue *q, uint16_t* pfid){
    pthread_mutex_lock(&q->mutex);
    while(q->remain == 0) pthread_cond_wait(&q->not_empty, &q->mutex);
    *pfid = q->queue[q->rp];
    q->rp = (q->rp + 1) % FID_QUEUE_SIZE;
    q->remain--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

