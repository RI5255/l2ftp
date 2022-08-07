#ifndef _THREADS_V3_H
#define _THREADS_V3_H

#include <linux/if_packet.h>
#include <signal.h>

extern void (* frame_handler)(struct tpacket3_hdr *ppd);

void setup_threads_v3(void);
void teardown_threads_v3(void);

void * master(void);
void * blk_handler(void);

void frame_handler_r(struct tpacket3_hdr *ppd);
void * fdata_checker(void);

void frame_handler_s(struct tpacket3_hdr *ppd);
void * fdata_sender(void);

#endif 