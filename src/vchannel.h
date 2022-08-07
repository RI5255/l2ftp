#ifndef _VCHANNEL_H
#define _VCHANNEL_H 

#include <stdint.h>

#define VCH_R 0
#define VCH_S  1
#define TABLE_SIZ 70
#define FDATALEN 102400

#define FIN 69 /* 1ファイルにつき69file */
#define RECIEVED 0x1 /* table用 */

struct vchannel_r{
    void *fdata; /* file data */
    uint8_t table[TABLE_SIZ];
    uint8_t ack; /* どこまで受信できているか */
};

struct vchannel_s{
    void *fdata; /* file data */
};

extern uint16_t fid_recent; /* 直近のfid */
extern struct vchannel_r *vch_head; /* vchannle構造体の配列へのポインタ */

int activate_vchannel(const unsigned char flag, const uint16_t fno, const char* base);
int save_fdata(uint16_t fid);

#endif 