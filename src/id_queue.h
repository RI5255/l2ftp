#ifndef _ID_QUEUE_H
#define _ID_QUEUE_H

#include <stdint.h>
#include <pthread.h>

#ifndef ID_QUEUE_SIZE
#define ID_QUEUE_SIZE 32
#endif 

#define FIN 69

struct id_queue{
    uint8_t queue[ID_QUEUE_SIZE];
    int remain;    /* number of elements in the queue */
    int rp, wp; /* used to read/write */
    pthread_mutex_t mutex; /* mutex for queue handling */
    pthread_cond_t not_empty;
    pthread_cond_t not_full ;
};

void enq_id(struct id_queue*, uint8_t);
void deq_id(struct id_queue*, uint8_t*);

#endif /* _ID_QUEUE_H */