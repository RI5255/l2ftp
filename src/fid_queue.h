#ifndef _FID_QUEUE_H
#define _FID_QUEUE_H

#include <stdint.h>
#include <pthread.h>

#ifndef FID_QUEUE_SIZE
#define FID_QUEUE_SIZE 32
#endif 

struct fid_queue{
    uint16_t queue[FID_QUEUE_SIZE];
    int remain;    /* number of elements in the queue */
    int rp, wp; /* used to read/write */
    pthread_mutex_t mutex; /* mutex for queue handling */
    pthread_cond_t not_empty;
    pthread_cond_t not_full ;
};

extern struct fid_queue fid_q;

void setup_fidq(void);
void teardown_fidq(void);

void enq_fid(struct fid_queue*, uint16_t fid);
void deq_fid(struct fid_queue*, uint16_t* pfid);

#endif /* _ID_QUEUE_H */