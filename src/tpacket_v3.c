#include "tpacket_v3.h"
#include "filter.h"
#include "l2ftp.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if.h>

/* 内部データ */
static unsigned int ringsiz;
static uint8_t *rxring;
static pthread_mutex_t tx_mutex;

/* 公開データ */
int sockfd;
struct ring ring;
uint8_t *txring;

/* socketを指定されたdeviceにbindする */
static int bind_sock(int sockfd, const char* devname){
    struct ifreq ifr;
    struct sockaddr_ll addr;

    /* fill struct to prepare binding */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, devname, IFNAMSIZ - 1); 

    /* get interface index */
    if(ioctl(sockfd, SIOCGIFINDEX, &ifr) == -1){
        perror("ioctl");
        return -1;
    }   

    /* fill sockaddr_ll struct to prepare binding */
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex =  ifr.ifr_ifindex;

    /* bind socket to the device */
    if(bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_ll)) == -1){
        perror("bind");
        return -1;
    }
    
    return 0;
}

int setup_socket(void){
    int err, i, v = TPACKET_V3;
    struct tpacket_req3 req;

    /* socketを作成 */
    sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if(sockfd == -1){
        perror("socket");
        return -1;
    }

    /* versionを指定 */
    err = setsockopt(sockfd, SOL_PACKET, PACKET_VERSION, &v, sizeof(v));
    if(err == -1){
        perror("setsockopt: PACKET_VERSION");
        return -1;
    }

    /* filterを設定 */
    err = setsockopt(sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf));
    if(err == -1){
        perror("setsockopt: SO_ATTACH_FILTER");
        return -1;
    }

    /* RX RINGを確保する準備 */
    memset(&req, 0, sizeof(req));
    req.tp_block_size = ring.param.rblocksiz;
    req.tp_frame_size = ring.param.rframesiz;
    req.tp_block_nr = ring.param.rblocknum;
    req.tp_frame_nr = (ring.param.rblocksiz * ring.param.rblocknum) / ring.param.rframesiz;
    req.tp_retire_blk_tov = 60; // ブロックが埋まっていなくても返るタイムアウト。ミリ秒

    /* RX RINGを確保  */
    err = setsockopt(sockfd, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req));
    if(err == -1){
        perror("setsockopt: PACKET_RX_RING");
        return -1;
    }

    ring.param.tframenum = (ring.param.tblocksiz * ring.param.tblocknum) / ring.param.tframesiz;

    /* TX RINGを確保する準備 */
    memset(&req, 0, sizeof(req));
    req.tp_block_size = ring.param.tblocksiz;
    req.tp_frame_size = ring.param.tframesiz;
    req.tp_block_nr = ring.param.tblocknum;
    req.tp_frame_nr =  ring.param.tframenum;

    /* TX RINGを確保 */
    err = setsockopt(sockfd, SOL_PACKET, PACKET_TX_RING, &req, sizeof(req));
    if(err == -1){
        perror("setsockopt: PACKET_TX_RING");
        return -1;
    }   

    /* socketをbind */ 
    err = bind_sock(sockfd, "eth0");
    if(err == -1){
        return -1;
    }

    /* ユーザースペースにmap。MAP_LOCKEDを入れてもいいかも? */
    ringsiz = ring.param.rblocksiz * ring.param.rblocknum + ring.param.tblocksiz * ring.param.tblocknum;
    rxring = (uint8_t*)mmap(NULL, ringsiz, PROT_READ|PROT_WRITE, MAP_SHARED, sockfd, 0);
    if(rxring == MAP_FAILED){
        perror("mmap");
        return -1;
    }

    /* txringの先頭を計算 */
    txring = rxring + ring.param.rblocksiz * ring.param.rblocknum;

    /* blockへのポインタ配列を作成 */
    ring.rb = malloc(ring.param.rblocknum * sizeof(*ring.rb));
    if(ring.rb == NULL){
        perror("malloc");
        return -1;
    }
    for(i = 0; i < ring.param.rblocknum; i++){
        ring.rb[i].iov_base = rxring + (i * ring.param.rblocksiz);
        ring.rb[i].iov_len = ring.param.rblocksiz;
    }

    pthread_mutex_init(&tx_mutex, NULL);
    return 0;
}

void teardown_socket(void){
    munmap(rxring, ringsiz);
    free(ring.rb);
    close(sockfd);
    pthread_mutex_destroy(&tx_mutex);
}

/* RX RINGのblockをKERNELに戻す */
void flush_block(struct tpacket_block_desc *pbd){
    pbd->hdr.bh1.block_status = TP_STATUS_KERNEL;
    //printf("flushed\n");
}

/* TX RINGの空いているframeを返す */
struct tpacket3_hdr * getfreeframe(void){
    struct tpacket3_hdr *ppd; 
    struct pollfd pfd;
    static unsigned int tx_off = 0;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = sockfd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    while(1){
        ppd = (struct tpacket3_hdr*)(txring + ring.param.tframesiz * tx_off);
        if(ppd->tp_status != TP_STATUS_AVAILABLE){
            poll(&pfd, 1, -1);
            continue;
        }
        break;
    }
    
    tx_off = (tx_off + 1) % ring.param.tframenum;

    return ppd;    
}

/* TX RINGから指定されたframeを確保する。point: 本質的な情報はoffset。 */
unsigned int getfreeblock(unsigned int num_pkts){
    struct tpacket3_hdr *ppd; 
    struct pollfd pfd;
    unsigned int num = num_pkts;
    static unsigned int tx_offs = 0, head;
   
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = sockfd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    pthread_mutex_lock(&tx_mutex);

    head = tx_offs;

    while(num){
        ppd = (struct tpacket3_hdr*)(txring + ring.param.tframesiz * tx_offs);
        if(ppd -> tp_status != TP_STATUS_AVAILABLE){
            poll(&pfd, 1, -1);
            continue;
        }
        tx_offs = (tx_offs + 1) % ring.param.tframenum;
        num--;
    }
    pthread_mutex_unlock(&tx_mutex);

    return  head;

}

/* 指定されたframeを送信する */
void send_frame(struct tpacket3_hdr* ppd, unsigned int datalen){
    int err;
    ppd -> tp_len = L2FTP_HDRLEN + datalen;
    ppd -> tp_status = TP_STATUS_SEND_REQUEST;
    
    err = send(sockfd, NULL, 0, 0);
    if(err == -1){
        perror("send");
    }
}

/* 指定されたoffsetから、指定された数のframeを送信 */
void send_block(unsigned int offs, unsigned int num_pkts){
    int err, i;
    struct tpacket3_hdr *ppd;
    unsigned int tx_offs, nbytes = 0, nframes = 0, nerr = 0;

    tx_offs = offs;
    for(i = 0; i < num_pkts; i++){
        ppd = (struct tpacket3_hdr*)(txring + ring.param.tframesiz * tx_offs);
        ppd -> tp_status = TP_STATUS_SEND_REQUEST;
        tx_offs = (tx_offs + 1) % ring.param.tframenum;
    }

    err = send(sockfd, NULL, 0, 0);

    if(err == -1){
        perror("send");
        nerr++;
    }

    for(i = 0; i < num_pkts; i++){
        ppd = (struct tpacket3_hdr*)(txring + ring.param.tframesiz * offs);
        if(ppd -> tp_status == TP_STATUS_AVAILABLE){
            nframes ++;
            nbytes += ppd->tp_len;
        }
    }    

    //printf("sent %u frames, %u err, data: %u bytes\n", nframes, nerr, nbytes);
}

