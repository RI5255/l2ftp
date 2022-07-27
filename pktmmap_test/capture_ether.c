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
#define FRAME_SIZE 128  /* if FRAME_SIZE is not an integral of the PAGE_SIZE, padding will be added, making the code more complicated */
#define FRAME_NO 32
#define RING_SIZE (BLOCK_SIZE * BLOCK_NO)

#define ETH_HDRLEN 14 
#define ETH_ADDRLEN 6
#define ETH_P_ALL 0x0003

struct ethhdr{
    uint8_t dest[ETH_ADDRLEN];
    uint8_t src[ETH_ADDRLEN];
    uint16_t proto;
};

int sockfd;
void *rxring;
int rxring_offset;

/* bind socket to the specified device */
int bind_sock(int sockfd, const char* device){
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

/* create socket, attach filter , allocate RX ring buffer and bind socket to "eth2" */
void setup_sock(void){
     struct tpacket_req packet_req;
    /* make socket */
    if((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1){
        perror("socket");
        exit(-1);
    }   
    /* prepare RX ring requese */
    packet_req.tp_block_size = BLOCK_SIZE; 
    packet_req.tp_block_nr = BLOCK_NO;
    packet_req.tp_frame_size = FRAME_SIZE;
    packet_req.tp_frame_nr = FRAME_NO;

     /* send RX ring request */
    if(setsockopt(sockfd, SOL_PACKET, PACKET_RX_RING, (void*)&packet_req, sizeof(packet_req)) == -1){
        perror("setsockopt: PACKET_RX_RING");
        exit(-1);
    }

    /* attach filter */
    if(setsockopt(sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf)) == -1){
        perror("setsockopt: SO_ATTACH_FILTER");
        exit(-1);
    }

    /* bind */
    if(bind_sock(sockfd, "eth0") == -1){
        exit(-1);
    }

    /* map to user space */
    if((rxring = mmap(NULL, RING_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, sockfd, 0)) == NULL){
        perror("mmap");
        exit(EXIT_FAILURE);
    }
}

/* decode captured frame*/ 
void decode_frame(struct tpacket_hdr * head){
    struct ethhdr * ethhdr; /* defined in linux/if_ether.h */
    char *data;
    int i;

    printf("Recieved %d bytes\n",head->tp_snaplen);

    /* be careful here! 
    If you write ethhdr = (struct ethhdr *)(head + head->tp_mac), it is interpreted as head + sizeof(struct tpacket_hdr ) * head->tp_mac. */
    ethhdr = (struct ethhdr *)((uint8_t*)head + head->tp_mac);

    printf("dest: %02x", ethhdr->dest[0]);
    for (i=1; i<ETH_ADDRLEN; i++){
        printf(":%02x", ethhdr->dest[i]);
    }

    printf("\tsource: %02x",ethhdr->src[0]);
    for (i=1; i<ETH_ADDRLEN; i++){
        printf(":%02x", ethhdr->src[i]);
    }
   
    printf("\t type: 0x%04x\n",ntohs(ethhdr->proto));

    data = (char*)((uint8_t*)ethhdr + ETH_HDRLEN);

    printf("Data: %s\n", data); 

    /* clear status */
    head->tp_status = TP_STATUS_KERNEL;
}

/* wait until readable data arrives and return a pointer to captured frame */
struct tpacket_hdr * handle_rxring(int sockfd){
    struct tpacket_hdr *head;
    struct pollfd pfd;

    head = (struct tpacket_hdr*)(rxring + FRAME_SIZE * rxring_offset);

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

    /* update offset. if offset == FRAME_NO, set 0*/
    rxring_offset ++;
    if(rxring_offset == FRAME_NO) rxring_offset = 0;

    /* return pointer to frame*/
    return head;
}

int main(void){
    struct tpacket_hdr *head;
    
    setup_sock();

    head = handle_rxring(sockfd);
    decode_frame(head);
    return 0;
}