#include "vchannel.h"
#include "threads_v3.h"
#include "tpacket_v3.h"
#include "fid_queue.h"
#include "l2ftp.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>

/* 公開データ */
unsigned char vchflag; /* 受信側か送信側か */
unsigned int vchnum; /* channelの数 */
uint16_t fid_recent;
uint8_t *pvch_head;
char fpath_base[20]; 

/* 内部データ */
static sig_atomic_t running = 1;
static void (* frame_handler)(struct tpacket3_hdr *ppd);
static void (* worker)(void);

void sigterm(int num){
    running = 0;
}

static int setup_fdata(void);
static void teardown_fdata(void);
static void frame_handler_r(struct tpacket3_hdr *ppd);
static void frame_handler_s(struct tpacket3_hdr *ppd);
static int setup_vchannel(const unsigned char flag, const uint16_t fno, const char* base);
static void teardown_vchannel(void);

/* vchannelを起動。終了を指示されるまで動く */
int activate_vchannel(const unsigned char flag, const uint16_t fno, const char* base){
    int i, err;
    unsigned int blocknum = 0, num_pkts;
    struct tpacket_block_desc *pbd;
    struct tpacket3_hdr *ppd;
    struct pollfd pfd;
    struct tpacket_stats_v3 status;
    socklen_t len;
    pthread_t worker_t;

    err = setup_vchannel(flag, fno, base);
    if(err == -1){
        printf("vchannle setup failed\n");
        return -1;
    }
    printf("vchannel activated! starting transmission\n");

    setup_threads_v3();

    if(pthread_create(&worker_t, NULL, (void *(*)(void *))worker, NULL) != 0){
        perror("pthread_create");
        return -1;
    }

    /* poll用の構造体を準備 */
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = sockfd;
    pfd.events = POLLIN | POLLERR;
    pfd.revents = 0;


   while(__glibc_likely(running)){
        pbd = (struct tpacket_block_desc*)ring.rb[blocknum].iov_base;

        if((pbd->hdr.bh1.block_status & TP_STATUS_USER) == 0){
            poll(&pfd, 1, -1);
            continue;
        }

        num_pkts = pbd->hdr.bh1.num_pkts;
        ppd = (struct tpacket3_hdr *)((uint8_t *)pbd + pbd->hdr.bh1.offset_to_first_pkt);

        for(i=0; i < num_pkts; i++){
            frame_handler(ppd);
            ppd = (struct tpacket3_hdr *)((uint8_t *)ppd + ppd->tp_next_offset);
        } 
        flush_block(pbd);  
        blocknum = (blocknum + 1) % ring.param.rblocknum;
    }

    /* 統計情報を表示 */
    len = sizeof(status);
    err = getsockopt(sockfd, SOL_PACKET, PACKET_STATISTICS, &status, &len);
    if (err < 0) {
        perror("getsockopt");
        pthread_exit((void*)-1);
    }
    printf("Recieved: %u frames, %u dropped\n", status.tp_packets, status.tp_drops);

    pthread_cancel(worker_t);

    /* 後片付け */
    teardown_threads_v3();
    teardown_vchannel();

    return 0;
}


static int setup_fdata(void){
    int i, fd[vchnum];
    char fpath[30];
    struct vchannel_r * pvch;   

    if(vchflag == VCH_R){
        for(i = 0; i < vchnum; i++){
            pvch = (struct vchannel_r*)(pvch_head + sizeof(struct vchannel_r) * i);
            pvch->fdata = calloc(1, FDATALEN);
            if(pvch->fdata == NULL){
                perror("calloc");
                return -1;
            }
        }
    }else{
        for(i = 0; i < vchnum; i++){
            pvch = (struct vchannel_r*)(pvch_head + sizeof(struct vchannel_s) * i);
            snprintf(fpath, 30, "%s%d", fpath_base, i);
            
            fd[i] = open(fpath, O_RDONLY);
            if(fd[i] == -1){
                perror("open");
                return -1;
            }

            pvch->fdata = mmap(NULL, FDATALEN, PROT_READ, MAP_SHARED, fd[i] ,0);
            if(pvch->fdata == MAP_FAILED){
                perror("mmap");
                return -1;
            }

            if(close(fd[i]) == -1){
                perror("close");
                return -1;
            }
        }
    }

    return 0;
}

static void teardown_fdata(void){
    int i;
    struct vchannel_r * pvch;

    if(vchflag == VCH_R){
        for(i = 0; i < vchnum; i++){
            pvch = (struct vchannel_r*)(pvch_head + sizeof(struct vchannel_r) * i);
            free(pvch->fdata);
        }
    }else{
        for(i = 0; i < vchnum; i++){
            pvch = (struct vchannel_r*)(pvch_head + sizeof(struct vchannel_s) * i);
            munmap(pvch->fdata, FDATALEN);
        }
    }
}

static void frame_handler_r(struct tpacket3_hdr *ppd){
    uint16_t fid;
    uint8_t segid;
    size_t datalen;
    struct l2ftp_hdr *phdr;
    struct vchannel_r *pvch;

    phdr = (struct l2ftp_hdr*)((uint8_t*)ppd + ppd->tp_mac);
    datalen = ppd->tp_snaplen - L2FTP_HDRLEN;
    fid = phdr->fid;
    segid = phdr->segid;

    printf("Recieved fid: %u, segid: %u\n", fid, segid);
    pvch = (struct vchannel_r*)(pvch_head + sizeof(struct vchannel_r) * fid);

    /* 既に受信済みなら何もしない */
    if(pvch->table[segid]){
        return;
    }

    /* データをコピーして受信完了をマーク */
    memcpy((void*)(pvch->fdata + 1500 * segid), (void*)((uint8_t*)phdr + L2FTP_HDRLEN), datalen);
    pvch->table[segid] = RECIEVED;

    /* fidがfid_recentとは異なっていたら別のchannelに対する通信に切り替わったことが分かる。 */
    if(fid != fid_recent){
        enq_fid(&fid_q, fid_recent);
        fid_recent = fid;
    }
}

static int setup_vchannel(const unsigned char flag, const uint16_t fno, const char* base){
    /* パラメータを設定 */
    vchflag = flag;
    vchnum = fno;
    strcpy(fpath_base, base);

    /* vchannel構造体の配列を作成。関数ポインタに値をセット */
    if(vchflag == VCH_R){
        pvch_head = calloc(vchnum, sizeof(struct vchannel_r));
        frame_handler = frame_handler_r;
        worker = fdata_checker;
    }
    else{
        pvch_head = calloc(vchnum, sizeof(struct vchannel_s));
        frame_handler = frame_handler_s;
        worker = fdata_sender;
    }

    if(pvch_head == NULL){
        perror("calloc");
        return -1;
    }   

    /* file dataを準備 */
    if(setup_fdata() == -1){
        printf("setup_fdata() Failed\n");
        return -1;
    }

    /* handlerを登録 */
    signal(SIGINT, sigterm);
    signal(SIGTERM, sigterm);
    
    return 0;
}

static void teardown_vchannel(void){
    teardown_fdata();
    free(pvch_head);
}

/* 要求されたファイルデータを再送 */
static void frame_handler_s(struct tpacket3_hdr *ppd){
    uint16_t fid;
    uint8_t *psegid_req;
    size_t reqlen, datalen;
    struct l2ftp_hdr *phdr;
    struct vchannel_s *pvch;
    struct tpacket3_hdr *ppd2send;
    void *pdata;
    int i;
    unsigned int offs, head;
    
    reqlen = ppd->tp_snaplen - L2FTP_HDRLEN;
    phdr = (struct l2ftp_hdr*)((uint8_t*)ppd + ppd->tp_mac);
    fid = phdr->fid;

    printf("[frame_handler_s] recieved request fid: %u, reqlen: %lu\n", fid, reqlen);

    pvch = (struct vchannel_s*)(pvch_head + sizeof(struct vchannel_s) * fid);

    psegid_req = (uint8_t*)phdr + L2FTP_HDRLEN;

    offs = getfreeblock(reqlen);
    head = offs;

    /* 要求されたidを再送 */
    for(i = 0 ; i < reqlen; i++){
        ppd2send = (struct tpacket3_hdr*)(txring + ring.param.tframesiz * offs);
        datalen = *psegid_req == 68 ? 400 : 1500;
        ppd2send -> tp_len = datalen + L2FTP_HDRLEN;
        phdr = (struct l2ftp_hdr*)((uint8_t*)ppd2send + OFF);
        pdata = build_l2ftp(phdr, fid, *psegid_req);
        memcpy(pdata, (void*)(pvch->fdata + 1500 * *psegid_req), datalen);
        psegid_req++;
        offs = (offs + 1) % ring.param.tframenum;
    }
    send_block(head, reqlen);
}
