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
    ring.param.rblocksiz = 143360; ring.param.rframesiz = 2048; ring.param.rblocknum = 64;
    ring.param.tblocksiz = 8192; ring.param.tframesiz = 128; ring.param.tblocknum = 2; 
    if(setup_socket() == -1){
        printf("socket setup failed\n");
        return -1;
    }
    err = activate_vchannel(VCH_R, 1000, "./hanako/data/data");
    teardown_socket();
    if(err == -1){
        return -1;
    }
    return 0;
}
/* 現状、threadは返らないので、リソースの解法処理が行われていない。何らかの方法でmasterに終了を通知する必要がある。*/