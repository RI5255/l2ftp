/* common data */
int sockfd;  /* sockfd */
void *rxring;    /* pointer to RX ring */
void *txring;    /* pointer to TX ring */
int rxring_offset;   /* offset of RX ring */
int txring_offset;   /* offset of TX ring */ 

/* params of TX ring */
unsigned int TBLOCK_NO ;
unsigned int TBLOCK_SIZE; 
unsigned int  TFRAME_SIZE;
unsigned int TFRAME_NO;

/* parameters of RX ring */
unsigned int RBLOCK_NO;
unsigned int RBLOCK_SIZE;
unsigned int RFRAME_SIZE;
unsigned int RFRAME_NO;
unsigned int RING_SIZE;