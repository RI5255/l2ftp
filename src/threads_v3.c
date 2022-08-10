#include "threads_v3.h"
#include "tpacket_v3.h"
#include "vchannel.h"
#include "fid_queue.h"
#include "l2ftp.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>


/* TODO 受信側はfid_queueは使用しないので無駄っちゃ無駄 */
void setup_threads_v3(void){
    setup_fidq();
}

void teardown_threads_v3(void){
   teardown_fidq();
}

/* ファイルデータを保存(for fdata_checker difined in thread_v3.c)*/
static int save_fdata(uint16_t fid){
    int fd;
    char fpath[30];
    struct vchannel_r *pvch;
    void *fdata;

    pvch = (struct vchannel_r*)(pvch_head + sizeof(struct vchannel_r) * fid);
    fdata = (void*)pvch->fdata;

    snprintf(fpath, 30, "%s%d", fpath_base, fid);
    fd = open(fpath, O_CREAT|O_RDWR,S_IRUSR|S_IWUSR);
    if(fd == -1){
        perror("open");
        return -1;
    }
    if(write(fd, fdata, FDATALEN) == -1){
        perror("write");
        return -1;
    }
    if(close(fd) == -1){
        perror("close");
        return -1;
    }    
    return 0;
}

/* ファイルデータの抜けを調べて要求。全て受信完了したらファイルに保存して終了 */
void fdata_checker(void){
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
}

/* 指定されたfidのデータを全て送信する */
static void send_all(uint16_t fid){
    unsigned int i, offs, datalen, head;
    struct tpacket3_hdr *ppd;
    struct vchannel_s *pvch;
    struct l2ftp_hdr *phdr;
    void *pdata;

    pvch = (struct vchannel_s*)(pvch_head + sizeof(struct vchannel_s) * fid);
    offs = getfreeblock(69);
    head = offs;

    for(i = 0; i < 69; i++){
        ppd = (struct tpacket3_hdr *)(txring + ring.param.tframesiz * offs);
        datalen = i == 68? 400 : 1500;
        ppd -> tp_len = datalen + L2FTP_HDRLEN;
        phdr = (struct l2ftp_hdr*)((uint8_t*)ppd + OFF);
        pdata = build_l2ftp(phdr, fid, i);
        memcpy(pdata, (void*)(pvch->fdata + 1500 * i), datalen);
        offs = (offs + 1) % ring.param.tframenum;
    }
    send_block(head, 69);
}

/* ひたすらファイルデータを送り続ける */
void fdata_sender(void){
    uint16_t fid;
    unsigned int usec = 15000;

    printf("[fdata_sender] I will start a job\n");

    while(1){
        for(fid = 0; fid < vchnum; fid++){
            send_all(fid);
            usleep(usec);
            //printf("[fdata_sender] sent data fid: %u\n", fid);
        }
    }

    /* ここには到達しない */
    pthread_exit((void*)0);
}