#ifndef _THREADS_V3_H
#define _THREADS_V3_H

#include <linux/if_packet.h>
#include <signal.h>

void setup_threads_v3(void);
void teardown_threads_v3(void);
void * fdata_checker(void);
void * fdata_sender(void);

#endif 