#include "l2ftp.h"

#include <stdint.h>
#include <arpa/inet.h>

uint8_t dest[ETH_ADDRLEN];

/* 宛先macアドレスを設定 */
void setup_l2ftp(uint8_t * pdest){
    int i;
    for(i = 0; i < ETH_ADDRLEN; i++){
        dest[i] = pdest[i];
    }
}

/* l2ftp_hdrを作成 */
void * build_l2ftp(struct l2ftp_hdr* phdr, uint16_t fid, uint8_t segid){
    int i;

    for(i = 0; i < ETH_ADDRLEN; i++){
        phdr->dest[i] = dest[i];
    }
    phdr->fid = fid;
    phdr->segid = segid;
    phdr->proto = htons(0x88b5);
    return (void*)((uint8_t*)phdr + L2FTP_HDRLEN);
}