#ifndef _PKTSOCK_H
#define _PKTSOCK_H

#include "if_packet.h"

#define ETH_P_ALL 0x0003
#define OFF (TPACKET_ALIGN(sizeof(struct tpacket_hdr)))

int bind_sock(int sockfd, const char* device);
int setup_sock(void);
struct tpacket_hdr * handle_txring(int sockfd);
int destroy_sock(void);

#endif 
