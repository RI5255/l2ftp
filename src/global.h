#ifndef _GLOBAL_H
#define _GLOBAL_H

/* common data */
extern int sockfd;  /* sockfd */
extern void *rxring;    /* pointer to RX ring */
extern void *txring;    /* pointer to TX ring */
extern int rxring_offset;   /* offset of RX ring */
extern int txring_offset;   /* offset of TX ring */ 

/* params of TX ring */
extern unsigned int TBLOCK_NO ;
extern unsigned int TBLOCK_SIZE; 
extern unsigned int  TFRAME_SIZE;
extern unsigned int TFRAME_NO;

/* parameters of RX ring */
extern unsigned int RBLOCK_NO;
extern unsigned int RBLOCK_SIZE;
extern unsigned int RFRAME_SIZE;
extern unsigned int RFRAME_NO;
extern unsigned int RING_SIZE;

#endif 