#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <unistd.h> /* for close */
#include <arpa/inet.h> /* for byteorder */
#include <poll.h>

#include <linux/if.h>
#include "if_packet.h"

#include "filter.h"


#define BLOCK_NO 1
#define BLOCK_SIZE 4096   /* BLOCK_SIZE must be a multiple of the PAGE_SIZE */
#define FRAME_SIZE 128 /* if FRAME_SIZE is not an integral of the PAGE_SIZE, padding will be added, making the code more complicated */
#define FRAME_NO 32
#define RING_SIZE (BLOCK_SIZE * BLOCK_NO)

#define ETH_HDRLEN 14 
#define ETH_ADDRLEN 6
#define ETH_P_ALL 0x0003
#define ETH_OFF (TPACKET_ALIGN(sizeof(struct tpacket_hdr)))

struct ethhdr{
    uint8_t dest[ETH_ADDRLEN];
    uint8_t src[ETH_ADDRLEN];
    uint16_t proto;
};

char data[1024];

/* ethhdr for transmission */
struct ethhdr ethhdr= {
    {0xb8,0x27,0xeb,0x5e,0x45,0x0a}, /* dest: adder of hanako:eth2 */
    {0x00,0x14,0xd1,0xda,0x89,0xaf}, /* src: adder of taro:eth2 */
    0xb588 /* protocol number 0x88b5 is reserved for experimental and private use.*/
};

int sockfd;
void *txring;
int txring_offset;

/* bind socket to the specified device */
int bind_sock(int sockfd, char* device){
    struct ifreq ifr;
    struct sockaddr_ll addr;

    /* fill struct to prepare binding */
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, device, IFNAMSIZ - 1); 

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

/* create socket, attach filter , allocate TX ring buffer and bind socket to "eth2" */
void setup_sock(void){
    struct tpacket_req packet_req;
    /* create socket */
    if((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1){
        perror("socket");
        exit(-1);
    }   

    /* prepare TX ring requese */
    packet_req.tp_block_size = BLOCK_SIZE; 
    packet_req.tp_block_nr = BLOCK_NO;
    packet_req.tp_frame_size = FRAME_SIZE;
    packet_req.tp_frame_nr = FRAME_NO;

    /* send TX ring request */
    if(setsockopt(sockfd, SOL_PACKET, PACKET_TX_RING, (void*)&packet_req, sizeof(packet_req)) == -1){
        perror("setsockopt: PACKET_TX_RING");
        exit(-1);
    }

    /* bind */
    if(bind_sock(sockfd, "eth2") == -1){
        exit(-1);
    }

    /* map to user space */
    if((txring = mmap(0, RING_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, sockfd, 0)) == NULL){
        perror("mmap");
        exit(EXIT_FAILURE);
    }
}
/* wait until there is space in the TX ring and return a pointer to the frame.*/
struct tpacket_hdr * handle_txring(int sockfd){
    struct tpacket_hdr *head;
    struct pollfd pfd;
  
    head = (struct tpacket_hdr*)(txring + FRAME_SIZE * txring_offset);
   
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

    /* update offset. if offset == FRAME_NO, set 0*/
    txring_offset ++;
    if(txring_offset == FRAME_NO) txring_offset = 0;
   
    /* return pointer to frame*/
    return head;
}

int main(void){
    struct tpacket_hdr *head;
    char data[] = "hello";

    setup_sock();

    head = handle_txring(sockfd);
    head->tp_len = ETH_HDRLEN + sizeof(data);
    /* build ethhdr  */
    memcpy((uint8_t*)head + ETH_OFF, &ethhdr, sizeof(ethhdr));
    /* copy file data */
    memcpy((uint8_t*)head + ETH_OFF + ETH_HDRLEN, data , sizeof(data));
    /* set status */
    head->tp_status = TP_STATUS_SEND_REQUEST;

    if(send(sockfd, NULL, 0, 0) == -1){
        perror("send");
        exit(-1);
    }

    /* clean up */
    close(sockfd);

    return 0;
}