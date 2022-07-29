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
#include "l2ftp.h"
#include "filter.h"

/* parameters of TX ring */
#define TBLOCK_NO 1
#define TBLOCK_SIZE 8192 
#define TFRAME_SIZE 128 /* TPACKET_HDRLEN(52) + ETH_HDRLEN(14) +  L21FTP_HDRLEN(4)がおさまるサイズ */
#define TFRAME_NO 64

/* parameters of RX ring */
#define RBLOCK_NO 1
#define RBLOCK_SIZE 163840 
#define RFRAME_SIZE 2048 /* TPACKET_HDRLEN(52) + ETH_HDRLEN(14) +  L21FTP_HDRLEN(4) + DATA_LEN(1280)がおさまるサイズ */
#define RFRAME_NO 80

#define RING_SIZE (TBLOCK_SIZE * TBLOCK_NO + RBLOCK_SIZE * RBLOCK_NO)

#define ETH_P_ALL 0x0003

#define OFF (TPACKET_ALIGN(sizeof(struct tpacket_hdr)))

#define FPATH_LEN 20
#define MTU 1500
#define FDATA_LEN 102400

/* common data */
static int sockfd;  /* sockfd */
static void *txring;    /* pointer to TX ring */
static void *rxring;    /* pointer to RX ring */
static int txring_offset;   /* offset of TX ring */ 
static int rxring_offset;   /* offset of RX ring */

static char fpath[FPATH_LEN] = "./hanako/data/data"; 
static void *fdata; /* pointer to file data */
static int fd;  /* file fiscriptor */
struct id_queue send_q;
struct packet_queue pkt_q;

uint8_t dest[ETH_ADDRLEN] = {0x00, 0x15, 0x5d, 0xf8, 0x36, 0x7e};

#define FRAME_NO 69 /* 69 frame per 1 file */
#define RECIEVED 0x01

/* allocate memory space for the file size and creates a file with the specified name */
void init_fdata(uint8_t fno){
    fpath[18] = fno + 48;
    if((fdata = mmap(NULL, FDATA_LEN, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1 ,0)) == MAP_FAILED){
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    if((fd = open(fpath, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR)) == -1){
        perror("open");
        exit(EXIT_FAILURE);
    }
}

/* bind socket to the specified device */
void bind_sock(int sockfd, const char* device){
    struct ifreq ifr;
    struct sockaddr_ll addr;

    /* fill struct to prepare binding */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, device, IFNAMSIZ - 1); 

    /* get interface index */
    if(ioctl(sockfd, SIOCGIFINDEX, &ifr) == -1){
        perror("ioctl");
        exit(EXIT_FAILURE);
    }   

    /* fill sockaddr_ll struct to prepare binding */
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex =  ifr.ifr_ifindex;

    /* bind socket to the device */
    if(bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_ll)) == -1){
        perror("bind");
        exit(EXIT_FAILURE);
    }
}

/* create socket, bind to interface, allocate TX&RX ring and mmap */
void setup_sock(void){
    struct tpacket_req packet_req;
    int flag=1;

    /* create socket */
    if((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1){
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* set options */
    if(setsockopt(sockfd, SOL_PACKET, PACKET_LOSS, &flag, sizeof(flag)) == -1){
        perror("setsockopt: PACKET_LOSS");
        exit(EXIT_FAILURE);
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf)) == -1){
        perror("setsockopt: SO_ATTACH_FILTER");
        exit(EXIT_FAILURE);
    }

    /* prepare RX ring request */
    packet_req.tp_block_size = RBLOCK_SIZE;
    packet_req.tp_block_nr = RBLOCK_NO;
    packet_req.tp_frame_size = RFRAME_SIZE;
    packet_req.tp_frame_nr = RFRAME_NO;

    /* Allocate RX ring buffer */
    if((setsockopt(sockfd, SOL_PACKET, PACKET_RX_RING, &packet_req, sizeof(packet_req))) == -1){
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    /* prepare TX ring request */
    packet_req.tp_block_size = TBLOCK_SIZE; 
    packet_req.tp_block_nr = TBLOCK_NO;
    packet_req.tp_frame_size = TFRAME_SIZE;
    packet_req.tp_frame_nr = TFRAME_NO;

    /* send TX ring request */
    if(setsockopt(sockfd, SOL_PACKET, PACKET_TX_RING, (void*)&packet_req, sizeof(packet_req)) == -1){
        perror("setsockopt: PACKET_RX_RING");
        exit(EXIT_FAILURE);
    }

    /* bind */
    bind_sock(sockfd, "eth0");

    /* map to user space */
    if((rxring = mmap(NULL, RING_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, sockfd, 0)) == NULL){
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    /* calcurale head of RX ring */
    txring = (void*)((uint8_t*)rxring + RBLOCK_SIZE * RBLOCK_NO);
}

/* wait until there is space in the TX ring and return a pointer to the frame.*/
struct tpacket_hdr * handle_txring(int sockfd){
    struct tpacket_hdr *head;
    struct pollfd pfd;
  
    head = (struct tpacket_hdr*)((uint8_t*)txring + TFRAME_SIZE * txring_offset);
   
    /* TP_STATUS_AVAILABLE means there is space in TX ring */
    if(head->tp_status != TP_STATUS_AVAILABLE){
        /* fill struct to prepare poll */
        pfd.fd = sockfd;
        pfd.events = POLLOUT;
        pfd.revents =0;
        if(poll(&pfd, 1, -1) == -1){
            perror("poll");
            exit(EXIT_FAILURE);
        }        
    }   

    /* update offset. if offset == TFRAME_NO, set 0*/
    txring_offset = (txring_offset + 1) % TFRAME_NO;

    /* return pointer to frame*/
    return head;
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


void pkt_handler(void){
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
            enq_id(&send_q, id_req);
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
    printf("file transmission complited!\n");
    /* save in file */
    if(write(fd, fdata, FDATA_LEN) == -1){
        perror("write");
        exit(-1);
    }
}

/* handle captured frame */
static void sender(void){
    struct tpacket_hdr *head;
    struct l2ftp_hdr *hdr;
    uint8_t id_req;
    int i;
    
    printf("[Sender] I will start a job\n");
    while(1){
        deq_id(&send_q, &id_req);
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
    printf("[Sender] File transmission complited!\n");
}

/* free up resources */ 
static void cleanup(void){
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
    pthread_t m, pkt_h, s;
    uint8_t fno = 0;

    init_fdata(fno);
    pthread_mutex_init(&send_q.mutex, NULL);
    pthread_cond_init(&send_q.not_full, NULL);
    pthread_cond_init(&send_q.not_empty, NULL);
    pthread_mutex_init(&pkt_q.mutex, NULL);
    pthread_cond_init(&pkt_q.not_full, NULL);
    pthread_cond_init(&pkt_q.not_empty, NULL);
    setup_sock();

    /* create threads */
    if(pthread_create(&m, NULL, (void *(*)(void *))master, NULL) != 0){
        perror("pthread_create");
        return EXIT_FAILURE;
    }
    if(pthread_create(&pkt_h, NULL, (void *(*)(void *))pkt_handler, NULL) != 0){
        perror("pthread_create");
        return EXIT_FAILURE;
    }
    if(pthread_create(&s, NULL, (void *(*)(void *))sender, NULL) != 0){
        perror("pthread_create");
        return EXIT_FAILURE;
    }
    pthread_join(pkt_h,NULL);
    pthread_exit(NULL);
    cleanup();
    return EXIT_SUCCESS;
}
