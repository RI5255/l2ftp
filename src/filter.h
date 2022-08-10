#ifndef _FILTER_H
#define _FILTER_H

#include <linux/filter.h>

/* tcpdump -dd ether proto 0x88b5 */
struct sock_filter code[]={
    { 0x28, 0, 0, 0x0000000c },
    { 0x15, 0, 1, 0x000088b5 },
    { 0x6, 0, 0, 0x00040000 },
    { 0x6, 0, 0, 0x00000000 }
};

struct sock_fprog bpf = {
        .len = sizeof(code) / sizeof(code[0]),  // array size
        .filter = code,
};

#endif 



