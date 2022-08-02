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

/* allocate memory space for the file size and creates a file with the specified name */
void init_fdata(uint8_t fno){
    fpath[18] = fno + 48;
    fdata = calloc(1, FDATA_LEN);
    if(fdata == NULL){
        perror("calloc");
        exit(-1);
    }
}

void master(void){
    struct tpacket_hdr *head;
    struct pollfd pfd;

    printf("[Master] I will start a job\n");
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
        enq_pkt(&pkt_q, head);

        /* update offset. if offset == RFRAME_NO, set 0*/
        rxring_offset = (rxring_offset+1) % RFRAME_NO;
    }
}

void* reciever(void){
    struct tpacket_hdr *head;
    struct l2ftp_hdr *hdr;
    /* control table */
    uint8_t table[FRAME_NO] = {};
    uint8_t id_req = 0, segid;
    unsigned int datasize; 

    printf("[Pkt_Handler] I will start a job\n");

    /* id_reqが更新できるのは連続したデータが届いている間。*/
    while(1){
        deq_pkt(&pkt_q, &head);
        hdr = (struct l2ftp_hdr*)((uint8_t*)head + head->tp_mac);
      
        datasize = head->tp_snaplen - L2FTP_HDRLEN;
        segid = hdr->segid;   
        printf("[Reciver] recieved id: %hu size; %d\n", segid, datasize);

        /* stege2以降に有効。 */
        if(table[segid] == RECIEVED)  
            goto end;

        /* memcpy file data */
        memcpy((uint8_t*)fdata + MTU * segid, (uint8_t*)hdr + L2FTP_HDRLEN, datasize);
        table[segid] = RECIEVED;

        if(segid != id_req){
            enq_id(&id_q, id_req);
            goto end;
        }

        /* 想定通りのidを受信 */
        id_req++;
        while(table[id_req] == RECIEVED) id_req++;

        if(id_req == FIN) break;
 
        end:
        /* updata flag */
        head->tp_status = TP_STATUS_KERNEL;

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

/* handle captured frame */
void sender(void){
    struct tpacket_hdr *head;
    struct l2ftp_hdr *hdr;
    uint8_t id_req;
    int i;
    
    printf("[Sender] I will start a job\n");
    while(1){
        deq_id(&id_q, &id_req);
        head = handle_txring(sockfd);
        head->tp_len = L2FTP_HDRLEN;

        /* build l2ftp_hdr */
        hdr = (struct l2ftp_hdr*)&head + OFF;
        for(i=0; i<ETH_ADDRLEN; i++){
            hdr->dest[i] = dest[i];
        }
        hdr->fid = 0;
        hdr->segid = htons(id_req);
        
        /* updata status */
        head->tp_status = TP_STATUS_SEND_REQUEST;

        /* send frame */
        if(send(sockfd, NULL, 0, MSG_DONTWAIT) == -1){
            perror("send");
            exit(-1);
        }
        printf("[Sender] sent request %hu\n", id_req);
        if(id_req == FIN) break;
    }
}

/* free up resources */ 
void cleanup(void){
    /* close file discriptor */
    if(close(fd) == -1){
        perror("close");
        exit(EXIT_FAILURE);
    }
    /* free memory space */
    free(fdata);
    /* queueの変数はサボる。(再利用することはないので問題ない。はず。)*/
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
    init_fdata(fno);
    pthread_mutex_init(&id_q.mutex, NULL);
    pthread_cond_init(&id_q.not_full, NULL);
    pthread_cond_init(&id_q.not_empty, NULL);
    pthread_mutex_init(&pkt_q.mutex, NULL);
    pthread_cond_init(&pkt_q.not_full, NULL);
    pthread_cond_init(&pkt_q.not_empty, NULL);

    /* create threads */
    if(pthread_create(&m, NULL, (void *(*)(void *))master, NULL) != 0){
        perror("pthread_create");
        return EXIT_FAILURE;
    }
    if(pthread_create(&r, NULL, (void *(*)(void *))reciever, NULL) != 0){
        perror("pthread_create");
        return EXIT_FAILURE;
    }
    if(pthread_create(&s, NULL, (void *(*)(void *))sender, NULL) != 0){
        perror("pthread_create");
        return EXIT_FAILURE;
    }
    pthread_join(r, (void*)&retvalue);
    if(retvalue != 0){
        printf("something went wrong\n");
        return -1;
    }
    printf("file transmission complited!\n");
    cleanup();
    destroy_sock();
    return EXIT_SUCCESS;
}
