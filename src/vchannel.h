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
    uint8_t *fdata; /* file data */
    uint8_t table[TABLE_SIZ];
    uint8_t ack; /* どこまで受信できているか */
};

struct vchannel_s{
    uint8_t *fdata; /* file data */
};

extern unsigned char vchflag; /* 受信側か送信側か */
extern unsigned int vchnum; /* channelの数 (やり取りするファイル数と同じ)*/
extern uint16_t fid_recent; /* 直近のfid */
extern uint8_t *pvch_head; /* vchannle構造体の配列へのポインタ */
extern char fpath_base[20]; /* file pathのベース(連番の部分を抜いたもの) */

void sigterm(int num);
int activate_vchannel(const unsigned char flag, const uint16_t fno, const char* base);

#endif 