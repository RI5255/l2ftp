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
#include "pktsock.h"
#include "global.h"
#include "l2ftp.h"

#define FPATH_LEN 17
#define FDATA_LEN 102400
#define MTU 1500

/* common data */
char fpath[FPATH_LEN] = "./taro/data/data"; 
void *fdata; /* pointer to file data */
int fd;  /* file fiscriptor */
struct id_queue id_queue;

#define FRAME_NO 69 /* 69 frame per 1 file */
#define RECIEVED 0x90

uint8_t dest[ETH_ADDRLEN] = {0x00, 0x15, 0x5d, 0xf8, 0x36, 0x7e};

/* open file and mmap*/
void init_fdata(uint8_t fno){
    fpath[16] = fno + 48;
    if((fd = open(fpath, O_RDONLY)) == -1){
        perror("open");
        exit(EXIT_FAILURE);
    }
    if((fdata = mmap(NULL, FDATA_LEN, PROT_READ, MAP_SHARED, fd ,0)) == MAP_FAILED){
        perror("mmap");
        exit(EXIT_FAILURE);
    }
}

/* send file data */
void sender(void){
    struct tpacket_hdr *head;
    struct l2ftp_hdr *hdr;
    uint8_t i,j, id_req; 
    unsigned int datasize;

    printf("[Sender] I will start a job\n");
    for(i=0; i<FRAME_NO; i++){
        /* send fdata */
        datasize =  i==68? 400 : MTU;
        head = handle_txring(sockfd);
        head->tp_len = L2FTP_HDRLEN + datasize;
      
        /* build l2ftp_hdr */
        hdr = (struct l2ftp_hdr*)((uint8_t*)head + OFF);
        for(j=0; j<ETH_ADDRLEN; j++){
            hdr->dest[j] = dest[j];
        }
        hdr->fid = 0;
        hdr->segid = i;
        hdr->proto = htons(0x88b5);

        /* copy file data */
        memcpy((uint8_t*)hdr + L2FTP_HDRLEN, (uint8_t*)fdata + MTU * i, datasize);

         /* updata status */
        head->tp_status = TP_STATUS_SEND_REQUEST;
    }
    /* send frame */
    if(send(sockfd, NULL, 0, 0) == -1){
        perror("send");
        exit(EXIT_FAILURE);
    }
    
    while(1){
        deq_id(&id_queue, &id_req);
        if(id_req == FIN) break;
        datasize =  id_req==68? 400 : MTU;
        head = handle_txring(sockfd);
        head->tp_len = L2FTP_HDRLEN + datasize;

        /* build l2ftp_hdr */
        hdr = (struct l2ftp_hdr*)((uint8_t*)head + OFF);
        for(j=0; j<ETH_ADDRLEN; j++){
            hdr->dest[j] = dest[j];
        }
        hdr->fid = 0;
        hdr->segid = id_req;
        hdr->proto = htons(0x88b5);
        
        /* copy file data */
        memcpy((uint8_t*)hdr + L2FTP_HDRLEN, (uint8_t*)fdata + MTU * id_req, datasize);

        /* updata status */
        head->tp_status = TP_STATUS_SEND_REQUEST;

        /* send frame */
        if(send(sockfd, NULL, 0, 0)  == -1){
            perror("send");
            exit(EXIT_FAILURE);
        }
        printf("[Sender] sent requested data id: %hu\n", id_req);
    }
}

/* recieve request id and push to id_queue */
void reciever(void){
    struct tpacket_hdr *head;
    struct l2ftp_hdr *hdr;
    struct pollfd pfd;
    uint8_t id_req;
    printf("[Reciever] I will start a job\n");
    while(1){
        head = (struct tpacket_hdr*)((uint8_t*)rxring + RFRAME_SIZE * rxring_offset);

        /* TP_STATUS_KERNEL means threre is no redable data */
        if(head->tp_status == TP_STATUS_KERNEL){
            /* fill struct to prepare poll */
            pfd.fd = sockfd;
            pfd.events = POLLIN;
            pfd.revents =0;
            if(poll(&pfd, 1, -1) == -1){
                perror("poll");
                exit(EXIT_FAILURE);
            }        
        }   
        hdr = (struct l2ftp_hdr*)((uint8_t*)head + head->tp_net);
        id_req = hdr->segid;
        printf("[Sender] requested id: %hu\n", id_req);
        enq_id(&id_queue, id_req);
        if(id_req == FIN) break;
        /* updata flag */
        head->tp_status = TP_STATUS_KERNEL;
        /* update offset. if offset == RFRAME_NO, set 0 */
        rxring_offset = (rxring_offset+1) % RFRAME_NO;
     }
    printf("[Sender] File transmission complited!\n");
}

/* free up resources */ 
void cleanup(void){
    /* close socket */
    if(close(sockfd) == -1){
        perror("close");
        exit(EXIT_FAILURE);
    }

    /* close file discriptor */
    if(close(fd) == -1){
        perror("close");
        exit(EXIT_FAILURE);
    }

    /* free memory space */
    if(munmap(fdata, FDATA_LEN) == -1){
        perror("munmap");
        exit(EXIT_FAILURE);
    }
}

int main(void){
    pthread_t r, s;
    uint8_t fno=0;
    
    /* set params */
    TBLOCK_NO = 1;
    TBLOCK_SIZE = 163840;
    TFRAME_NO = 80;
    TFRAME_SIZE = 2048;
    RBLOCK_NO = 1;
    RBLOCK_SIZE = 8192;
    RFRAME_NO = 64;
    RFRAME_SIZE = 128;
    RING_SIZE = TBLOCK_NO * TBLOCK_SIZE + RBLOCK_NO * RBLOCK_SIZE;
    
    if(setup_sock() == -1){
        printf("setup_sock");
        return -1;
    }
    init_fdata(fno);
    pthread_mutex_init(&id_queue.mutex, NULL);
    pthread_cond_init(&id_queue.not_full, NULL);
    pthread_cond_init(&id_queue.not_empty, NULL);
    setup_sock();

    /* create threads */
    if(pthread_create(&r, NULL, (void *(*)(void *))reciever, NULL) != 0){
        perror("pthread_create");
        return EXIT_FAILURE;
    }
    if(pthread_create(&s, NULL, (void *(*)(void *))sender, NULL) != 0){
        perror("pthread_create");
        return EXIT_FAILURE;
    }

    pthread_join(s,NULL);
    cleanup();
    return EXIT_SUCCESS;
}