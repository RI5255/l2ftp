#include <stdio.h>
#include <stdint.h>
#include <poll.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h> /* for close */
#include <arpa/inet.h> /* for byteorder */
#include <string.h>

#include "filter.h"
#include "if_packet.h"
#include "global.h"
#include "pktsock.h"

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

/* create socket, bind to interface, allocate TX&RX ring and mmap */
int setup_sock(void){
    struct tpacket_req packet_req;

    /* create socket */
    if((sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1){
        perror("socket");
        return -1;
    }

    /* set options 
    if(setsockopt(sockfd, SOL_PACKET, PACKET_LOSS, &flag, sizeof(flag)) == -1){
        perror("setsockopt: PACKET_LOSS");
        exit(EXIT_FAILURE);
    }*/

    if(setsockopt(sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &bpf, sizeof(bpf)) == -1){
        perror("setsockopt: SO_ATTACH_FILTER");
        return -1;
    }

    /* prepare RX ring request */
    packet_req.tp_block_size = RBLOCK_SIZE;
    packet_req.tp_block_nr = RBLOCK_NO;
    packet_req.tp_frame_size = RFRAME_SIZE;
    packet_req.tp_frame_nr = RFRAME_NO;

    /* Allocate RX ring buffer */
    if((setsockopt(sockfd, SOL_PACKET, PACKET_RX_RING, &packet_req, sizeof(packet_req))) == -1){
        perror("setsockopt");
        return -1;
    }

    /* prepare TX ring request */
    packet_req.tp_block_size = TBLOCK_SIZE; 
    packet_req.tp_block_nr = TBLOCK_NO;
    packet_req.tp_frame_size = TFRAME_SIZE;
    packet_req.tp_frame_nr = TFRAME_NO;

    /* send TX ring request */
    if(setsockopt(sockfd, SOL_PACKET, PACKET_TX_RING, (void*)&packet_req, sizeof(packet_req)) == -1){
        perror("setsockopt: PACKET_RX_RING");
        return -1;
    }

    /* bind */
    if(bind_sock(sockfd, "eth0") == -1)
        return -1;

    /* map to user space */
    if((rxring = mmap(NULL, RING_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, sockfd, 0)) == NULL){
        perror("mmap");
        return -1;
    }

    /* calcurale head of RX ring */
    txring = (void*)((uint8_t*)rxring + RBLOCK_SIZE * RBLOCK_NO);

    return 0;
}

/* wait until there is space in the TX ring and return a pointer to the frame.*/
struct tpacket_hdr * handle_txring(int sockfd){
    struct tpacket_hdr *head;
    struct pollfd pfd;
  
    head = (struct tpacket_hdr*)((uint8_t*)txring + TFRAME_SIZE * txring_offset);
    
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = sockfd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    /* TP_STATUS_AVAILABLE means there is space in TX ring */
    while(head->tp_status != TP_STATUS_AVAILABLE){
        /* fill struct to prepare poll */
        if(poll(&pfd, 1, -1) == -1){
            perror("poll");
            return NULL;
        }        
    }   

    /* update offset. if offset == TFRAME_NO, set 0*/
    txring_offset = (txring_offset + 1) % TFRAME_NO;

    /* return pointer to frame*/
    return head;
}

int destroy_sock(void){
    if(close(sockfd) == -1){
        perror("close");
        return -1;
    }
    return 0;
}