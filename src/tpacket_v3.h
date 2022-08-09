#ifndef _TPACKET_V3
#define _TPACKET_V3

#include <stdint.h>
#include <linux/if_packet.h>
#include <sys/uio.h>

#define OFF TPACKET_ALIGN(sizeof(struct tpacket3_hdr))

/* ring bufferの設定。*/
struct ring_param{
    unsigned int rblocksiz; 
    unsigned int rblocknum;
    unsigned int rframesiz;
    unsigned int tblocksiz; 
    unsigned int tblocknum;
    unsigned int tframesiz;
    unsigned int tframesperblock;
    unsigned int tframenum;
};

/* ring bufferの情報を保持する構造体 */
struct ring{
    struct iovec *rb;
    struct iovec *tb;
    struct ring_param param;
};

extern int sockfd;
extern struct ring ring;
extern uint8_t *txring;

int setup_socket(void);
void teardown_socket(void);

void flush_block(struct tpacket_block_desc *pbd);
struct tpacket3_hdr * getfreeframe(void);
unsigned int getfreeblock(unsigned int num_pkts);
void send_frame(struct tpacket3_hdr* ppd, unsigned int datalen);
void send_block(unsigned int offs, unsigned int num_pkts);

#endif 