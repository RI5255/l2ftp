#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "if_packet.h"
#include <linux/if.h>

#include <unistd.h> /* for close */
#include <arpa/inet.h> /* for byteorder */
#include <poll.h>
#include <fcntl.h>
#include <pthread.h>

#include "id_queue.h"
#include "packet_queue.h"
#include "pktsock.h"
#include "global.h"
#include "l2ftp.h"

#define FPATH_LEN 20
#define MTU 1500
#define FDATA_LEN 102400


char fpath[FPATH_LEN] = "./hanako/data/data"; 
void *fdata; /* pointer to file data */
int fd;  /* file fiscriptor */
struct id_queue id_q;
struct packet_queue pkt_q;

uint8_t dest[ETH_ADDRLEN] = {0x00, 0x15, 0x5d, 0xf8, 0x36, 0x7e};

#define FRAME_NO 69 /* 69 frame per 1 file */
#define RECIEVED 0x01

uint8_t table[FRAME_NO]; /* control table */

/* allocate memory space for the file size and creates a file with the specified name */
int init_fdata(uint8_t fno){
    fpath[18] = fno + 48;
    fdata = calloc(1, FDATA_LEN);
    if(fdata == NULL){
        perror("calloc");
        return -1;
    }
    return 0;
}

/* frameを受信してqueueに入れる */
void* master(void){
    struct tpacket_hdr *head;
    struct pollfd pfd;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = sockfd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    printf("[Master] I will start a job\n");
    while(1){
        head = (struct tpacket_hdr*)((uint8_t*)rxring + RFRAME_SIZE * rxring_offset);
        /* TP_STATUS_KERNEL means threre is no readable data */
        while(head->tp_status == TP_STATUS_KERNEL){
            if(poll(&pfd, 1, -1) == -1){
                perror("poll");
                pthread_exit((void*)-1);
            }        
        }  

        enq_pkt(&pkt_q, head);

        /* update offset. if offset == RFRAME_NO, set 0*/
        rxring_offset = (rxring_offset+1) % RFRAME_NO;
    }
}

/* ファイルデータを受信。ackの算出はidを保存すれば後で出来る。rx ringを早く解放したいのでidの計算は分離する。仮説。受信処理が早すぎてdeqした後、statusを更新するまでの間に再びenqされてしまう説*/
void* reciever(void){
    struct tpacket_hdr *head;
    struct l2ftp_hdr *hdr;
    uint8_t segid;
    unsigned int datasize; 

    printf("[Reciever] I will start a job\n");

    while(1){
        deq_pkt(&pkt_q, &head);
        hdr = (struct l2ftp_hdr*)((uint8_t*)head + head->tp_mac);
        datasize = head->tp_snaplen - L2FTP_HDRLEN;
        segid = hdr->segid;   
        printf("[Reciver] recieved id: %hu size; %d\n", segid, datasize);

        /* 受信済みか調べる */    
        if(table[segid] == RECIEVED)  
            goto end;

        /* ファイルデータをコピーして受信完了をマーク */
        memcpy((uint8_t*)fdata + MTU * segid, (uint8_t*)hdr + L2FTP_HDRLEN, datasize);
        table[segid] = RECIEVED;

        /* idをキューに入れる */
        enq_id(&id_q, segid);

        end:
        /* updata status */
        head->tp_status = TP_STATUS_KERNEL;
    }
}

#define NORMAL 0
#define RECOVER1 1
#define RECOVER2 2

/* ackの算出+抜けているデータの再送要求 */
void* sender(void){
    struct tpacket_hdr *head;
    struct l2ftp_hdr *hdr;
    static uint8_t state, ack, id_req, id_recv;
    uint8_t i, j;

    printf("[Sender] I will start a job\n");
    while(1){
        deq_id(&id_q, &id_recv);
        switch(state){
            case NORMAL:
                if(id_recv != ack){
                    id_req = ack;
                    state = RECOVER1;
                    printf("[Sender]change state to RECOEVER1\n");
                }else{
                    /* ackを更新 */
                    ack++;
                    break;
                }

            case RECOVER1:
                if(id_recv < id_req){
                    state = RECOVER2;
                    printf("[Sender]change state to RECOEVER2\n");
                }else{
                     /* lostした全てのidを要求 */
                    for(j=id_req; j!=id_recv; j++){    
                        head = handle_txring(sockfd);
                        head->tp_len = L2FTP_HDRLEN;

                        /* build l2ftp_hdr */
                        hdr = (struct l2ftp_hdr*)((uint8_t*)head + OFF);
                        for(i=0; i<ETH_ADDRLEN; i++){
                            hdr->dest[i] = dest[i];
                        }
                        hdr->fid = 0;
                        hdr->segid = j;
                        hdr->proto = htons(0x88b5);
                        
                        /* updata status */
                        head->tp_status = TP_STATUS_SEND_REQUEST;

                        /* send frame */
                        if(send(sockfd, NULL, 0, MSG_DONTWAIT) == -1){
                            perror("send");
                            pthread_exit((void*)-1);
                        }
                        printf("[Sender] sent request %hu\n", j);
                    }
                    id_req = id_recv + 1 ;
                    break;
                }

            case RECOVER2:
                if(id_recv == ack){
                    /* ackを更新できるだけ更新する。 */
                    while(table[ack] == RECIEVED) ack++;
                    printf("ack: %d\n", ack);
                }
                else{
                    /* 受け取ったidまでで、まだ受信していないidを要求 */
                    for(j=id_req; j!=id_recv; j = (j+1) % 68){    
                        if(table[j] == RECIEVED) continue;
                        head = handle_txring(sockfd);
                        head->tp_len = L2FTP_HDRLEN;

                        /* build l2ftp_hdr */
                        hdr = (struct l2ftp_hdr*)((uint8_t*)head + OFF);
                        for(i=0; i<ETH_ADDRLEN; i++){
                            hdr->dest[i] = dest[i];
                        }
                        hdr->fid = 0;
                        hdr->segid = j;
                        hdr->proto = htons(0x88b5);
                        
                        /* updata status */
                        head->tp_status = TP_STATUS_SEND_REQUEST;

                        /* send frame */
                        if(send(sockfd, NULL, 0, MSG_DONTWAIT) == -1){
                            perror("send");
                            pthread_exit((void*)-1);
                        }
                        printf("[Sender] sent request %hu\n", j);
                    }
                    id_req = id_recv + 1;
                }
                break;
        }
        if(ack == 69){
            head = handle_txring(sockfd);
            head->tp_len = L2FTP_HDRLEN;

            /* build l2ftp_hdr */
            hdr = (struct l2ftp_hdr*)((uint8_t*)head + OFF);
            for(i=0; i<ETH_ADDRLEN; i++){
                hdr->dest[i] = dest[i];
            }
            hdr->fid = 0;
            hdr->segid = 69;
            hdr->proto = htons(0x88b5);
            
            /* updata status */
            head->tp_status = TP_STATUS_SEND_REQUEST;

            /* send frame */
            if(send(sockfd, NULL, 0, MSG_DONTWAIT) == -1){
                perror("send");
                pthread_exit((void*)-1);
            }
            printf("[Sender] send FIN\n");
            break;
        }
    }
    /* ファイルデータを保存して終了 */
    fd = open(fpath, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
    if(fd == -1){
        perror("open");
        pthread_exit((void*)-1);
    }
    if(write(fd, fdata, FDATA_LEN) == -1){
        perror("write");
        pthread_exit((void*)-1);
    }
    pthread_exit((void*)0);
}

/* free up resources */ 
int cleanup(void){
    /* close file discriptor */
    if(close(fd) == -1){
        perror("close");
        return -1;
    }
    /* free memory space */
    free(fdata);

    /* queueの変数は削除しなくていいのか疑問。これが呼ばれる前にexitしているからいいのか？*/
    
    return 0;
}

int main(void){
    pthread_t m, r, s;
    uint8_t fno = 0;
    int retvalue;

    /* set params */
    TBLOCK_NO = 1;
    TBLOCK_SIZE = 8192;
    TFRAME_NO = 64;
    TFRAME_SIZE = 128;
    RBLOCK_NO = 1;
    RBLOCK_SIZE = 163840;
    RFRAME_NO = 80;
    RFRAME_SIZE = 2048;
    RING_SIZE = TBLOCK_NO * TBLOCK_SIZE + RBLOCK_NO * RBLOCK_SIZE;

    if(setup_sock() == -1){
        printf("setup_sock");
        return -1;
    }
    if(init_fdata(fno) == -1){
        printf("init_fdata() failed\n");
        return -1;
    }
    pthread_mutex_init(&id_q.mutex, NULL);
    pthread_cond_init(&id_q.not_full, NULL);
    pthread_cond_init(&id_q.not_empty, NULL);
    pthread_mutex_init(&pkt_q.mutex, NULL);
    pthread_cond_init(&pkt_q.not_full, NULL);
    pthread_cond_init(&pkt_q.not_empty, NULL);

    /* create threads */
    if(pthread_create(&m, NULL, (void *(*)(void *))master, NULL) != 0){
        perror("pthread_create");
        return -1;
    }
    if(pthread_create(&r, NULL, (void *(*)(void *))reciever, NULL) != 0){
        perror("pthread_create");
        return -1;
    }
    if(pthread_create(&s, NULL, (void *(*)(void *))sender, NULL) != 0){
        perror("pthread_create");
        return -1;
    }
    pthread_join(s, (void*)&retvalue);
    if(retvalue != 0){
        printf("something went wrong\n");
        return -1;
    }
    printf("file transmission complited!\n");
    if(cleanup() == -1){
        printf("cleanup failed\n");
        return -1;
    }
    destroy_sock();
    return 0;
}
