#include "threads_v3.h"
#include "tpacket_v3.h"
#include "vchannel.h"
#include "block_queue.h"
#include "fid_queue.h"
#include "l2ftp.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>

void (* frame_handler)(struct tpacket3_hdr *ppd);

/* TODO 受信側はfid_queueは使用しないので無駄っちゃ無駄 */

void setup_threads_v3(void){
    setup_blkq();
    setup_fidq();
}

void teardown_threads_v3(void){
   teardown_blkq();
   teardown_fidq();
}

/* 受信したblockをqueueに入れる */
void * master(void){
    int err;
    static unsigned int blocknum = 0;
    socklen_t len;
    struct tpacket_block_desc *pbd;
    struct pollfd pfd;
    struct tpacket_stats_v3 status;

    printf("[master] I will stat a job\n");

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = sockfd;
    pfd.events = POLLIN | POLLERR;
    while(1){
        pbd = (struct tpacket_block_desc*)ring.rb[blocknum].iov_base;

        if((pbd->hdr.bh1.block_status & TP_STATUS_USER) == 0){
            poll(&pfd, 1, -1);
            continue;
        }
        enq_blk(&blk_q, pbd);
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

    pthread_exit((void*)0);
}

/* vchflagがVCH_R、つまり受信側用のthread */
void frame_handler_r(struct tpacket3_hdr *ppd){
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

void * blk_handler(void){
    int i, num_pkts;
    struct tpacket_block_desc *pbd;
    struct tpacket3_hdr *ppd;

    printf("[blok_handler_r] I will stat a job\n");
    while(1){
        deq_blk(&blk_q, &pbd);
        num_pkts = pbd->hdr.bh1.num_pkts;
        ppd = (struct tpacket3_hdr *)((uint8_t *)pbd + pbd->hdr.bh1.offset_to_first_pkt);

        for(i=0; i < num_pkts; i++){
            frame_handler(ppd);
            ppd = (struct tpacket3_hdr *)((uint8_t *)ppd + ppd->tp_next_offset);
        } 
        flush_block(pbd);  
    }
    pthread_exit((void*)0);
}

/* ファイルデータの抜けを調べて要求。全て受信完了したらファイルに保存して終了 */
void *fdata_checker(void){
    uint8_t segid_req, *pdata;
    uint16_t fid;
    struct vchannel_r *pvch;
    struct tpacket3_hdr *ppd;
    struct l2ftp_hdr *phdr;
    unsigned int lostnum;

    printf("[fdata_checker] I will stat a job\n");
    
    while(1){
        lostnum = 0; 
        /* deqしたfidの情報を保持するvchannelを取得 */
        deq_fid(&fid_q, &fid);
        printf("[fdata_checker] checking fid: %u\n", fid);
        pvch = (struct vchannel_r*)(pvch_head + sizeof(struct vchannel_r) * fid);
        
        /* ackを更新できるだけ更新 */
        while(pvch->table[pvch->ack]){
            pvch->ack++;
        }
        
        /* 全て受信完了していた場合　重複ファイルが作られる可能性は? */
        if(pvch->ack == FIN){
            if(save_fdata(fid) == -1){
                printf("save file failed\n");
            }
            printf("[fdata_checker] fdata check passed! : %u\n", fid);
        /* 抜けがあるので要求(tableを探索するのは非効率な気もするが...) */
        }else{
            ppd = getfreeframe();
            phdr = (struct l2ftp_hdr*)((uint8_t*)ppd + OFF);
            pdata = (uint8_t*)build_l2ftp(phdr, fid, 0);

            /* tableを検索して抜けているidでバッファを埋める */
            for(segid_req = pvch->ack; segid_req != FIN; segid_req++){
                if(pvch->table[segid_req]){
                    continue;
                }
                *pdata = segid_req;
                lostnum++; pdata++; 
            }

            printf("[fdata_checker] lost detected fid: %u, lost: %u sending request.\n", fid, lostnum);
            send_frame(ppd, lostnum);
        }
    }
    pthread_exit((void*)0);
}

/* vchflagがVCH_S、つまり送信側用のthread */

/* 要求されたファイルデータを送信 */
void frame_handler_s(struct tpacket3_hdr *ppd){
    uint16_t fid;
    uint8_t *psegid_req;
    size_t reqlen, datalen;
    struct l2ftp_hdr *phdr;
    struct vchannel_s *pvch;
    struct tpacket3_hdr *ppd2send;
    void *pdata;
    
    reqlen = ppd->tp_snaplen - L2FTP_HDRLEN;
    phdr = (struct l2ftp_hdr*)((uint8_t*)ppd + ppd->tp_mac);
    fid = phdr->fid;

    printf("[frame_handler_s] recieved request fid: %u, reqlen: %lu\n", fid, reqlen);

    pvch = (struct vchannel_s*)(pvch_head + sizeof(struct vchannel_s) * fid);

    psegid_req = (uint8_t*)phdr + L2FTP_HDRLEN;

    /* 要求されたidを再送 */
    for(; reqlen; reqlen--){
        datalen = *psegid_req == 68 ? 400 : 1500;
        ppd2send = getfreeframe();
        phdr = (struct l2ftp_hdr*)((uint8_t*)ppd2send + OFF);
        pdata = build_l2ftp(phdr, fid, *psegid_req);
        memcpy(pdata, (void*)(pvch->fdata + 1500 * *psegid_req), datalen);
        send_frame(ppd2send, datalen);
        psegid_req++;
    }
}

/* 指定されたfidのデータを全て送信する */
static void send_all(uint16_t fid){
    int i;
    unsigned int datalen;
    struct tpacket3_hdr *pbd, *ppd;
    struct vchannel_s *pvch;
    struct l2ftp_hdr *phdr;
    void *pdata;
    pvch = (struct vchannel_s*)(pvch_head + sizeof(struct vchannel_s) * fid);
    pbd = getfreeblock(69);
    for(i = 0; i < 69; i++){
        ppd = (struct tpacket3_hdr *)((uint8_t*)pbd + ring.param.tframesiz * i);
        datalen = i == 68? 400 : 1500;
        ppd -> tp_len = datalen + L2FTP_HDRLEN;
        phdr = (struct l2ftp_hdr*)((uint8_t*)ppd + OFF);
        pdata = build_l2ftp(phdr, fid, i);
        memcpy(pdata, (void*)(pvch->fdata + 1500 * i), datalen);
    }
    send_block(pbd, 69);
}

/* ひたすらファイルデータを送り続ける */
void * fdata_sender(void){
    uint16_t fid;
    /*uint8_t segid;
    size_t datalen;
    struct vchannel_s *pvch;
    struct tpacket3_hdr *ppd2send;
    struct l2ftp_hdr *phdr;
    void *pdata;*/
    printf("[fdata_sender] I will stat a job\n");

    /*for(fid = 0; fid != vchnum; fid++){
        pvch = (struct vchannel_s*)(pvch_head + sizeof(struct vchannel_s) * fid);
        for(segid = 0; segid != FIN; segid++){
            datalen = segid == 68? 400 : 1500;
            ppd2send = getfreeframe();
            phdr = (struct l2ftp_hdr*)((uint8_t*)ppd2send + OFF);
            pdata = build_l2ftp(phdr, fid, segid);
            memcpy(pdata, (void*)(pvch->fdata + 1500 * segid), datalen);
            send_frame(ppd2send, datalen);
        }
        printf("[fdata_sender] sent data fid: %u\n", fid);
    }*/
    for(fid = 0; fid != vchnum; fid++){
        send_all(fid);
        printf("[fdata_sender] sent data fid: %u\n", fid);
    }
    pthread_exit((void*)0);
}