CC := gcc
CFLAGS := -Wall -pthread -g

sender_v3: tpacket_v3.c fid_queue.c vchannel.c l2ftp.c threads_v3.c sender_v3.c
reciever_v3: tpacket_v3.c fid_queue.c vchannel.c l2ftp.c threads_v3.c reciever_v3.c
clean: 
	rm reciever_v3 sender_v3