#ifndef _TPACKET_V3
#define _TPACKET_V3

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
    unsigned int tframeperblock;
};

/* ring bufferの情報を保持する構造体 */
struct ring{
    struct iovec *rb;
    struct iovec *tb;
    struct ring_param param;
};

extern int sockfd;
extern struct ring ring;

int setup_socket(void);
void teardown_socket(void);

void flush_block(struct tpacket_block_desc *pbd);
struct tpacket3_hdr * getfreeframe(void);
void send_frame(struct tpacket3_hdr* ppd, unsigned int datalen);

#endif 