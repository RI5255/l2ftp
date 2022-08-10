#ifndef _L2FTP_H
#define _L2FTP_H

#include <stdint.h>

#define ETH_ADDRLEN 6

extern uint8_t dest[ETH_ADDRLEN];

/* header */
struct l2ftp_hdr{
    uint8_t dest[ETH_ADDRLEN];
    uint16_t fid; /* file id */
    uint8_t segid; /* segment id */    
    uint8_t reserved[3];
    uint16_t proto;
};
#define L2FTP_HDRLEN sizeof(struct l2ftp_hdr)

void setup_l2ftp(uint8_t * pdest);
void * build_l2ftp(struct l2ftp_hdr* phdr, uint16_t fid, uint8_t segid);

#endif