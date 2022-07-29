#ifndef _L2FTP_H
#define _L2FTP_H

#include <stdint.h>
#define ETH_ADDRLEN 6

/* header */
struct l2ftp_hdr{
    uint8_t dest[ETH_ADDRLEN];
    uint16_t fid; /* file id */
    uint8_t segid; /* segment id */    
    uint8_t reserved[3];
    uint16_t proto;
};

#define L2FTP_HDRLEN sizeof(struct l2ftp_hdr)

#endif /* _L2FTP_H */