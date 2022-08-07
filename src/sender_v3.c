#include "tpacket_v3.h"
#include "vchannel.h"
#include <stdio.h>
#include <signal.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <unistd.h>
#include <stdlib.h>

static void sighandler(){
    int err;
    socklen_t len;
    struct tpacket_stats_v3 status;
    len = sizeof(status);
    err = getsockopt(sockfd, SOL_PACKET, PACKET_STATISTICS, &status, &len);
    if (err < 0) {
        perror("getsockopt");
    }
    printf("Recieved: %u frames, %u dropped, freeze %u\n", status.tp_packets, status.tp_drops, status.tp_freeze_q_cnt);
    close(sockfd);
    exit(0);
}

int main(){
    int err;
     signal(SIGINT, sighandler);
    /* ringにparameterを設定してからsetup_socketを呼ぶ */
    ring.param.rblocksiz = 4096; ring.param.rframesiz = 256; ring.param.rblocknum = 2; 
    ring.param.tblocksiz = 143360; ring.param.tframesiz = 2048; ring.param.tblocknum = 2;
    if(setup_socket() == -1){
        printf("socket setup failed\n");
        return -1;
    }
    err = activate_vchannel(VCH_S, 2, "./taro/data/data");
    teardown_socket();
    if(err == -1){
        return -1;
    }
    return 0;
}