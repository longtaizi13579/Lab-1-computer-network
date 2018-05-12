#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define DATA_TIMER  2500
#define ack_timer 150
struct FRAME { 
    unsigned char kind; /* FRAME_DATA */
    unsigned char ack;
    unsigned char seq;
    unsigned char data[PKT_LEN]; 
    unsigned int  padding;//frame填充
};
static int MAX_SEQ = 7;
static unsigned char frame_nr = 0, buffer[8][PKT_LEN], nbuffered;//发送方发送下一帧序号
static unsigned char frame_expected = 0;//接收方期待的帧序号
static int phl_ready = 0;
static unsigned char ack_expected = 0;//发送方期待的ack序号
static int between(unsigned char a, unsigned char b, unsigned char c)//ack_expected , f.ack , frame_nr
{
	if (((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a)))
		return 1;
	else
		return 0;
}
//放帧至物理层
static void put_frame(unsigned char *frame, int len)
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}
//物理层发送数据帧并启动定时器
static void send_data_frame(void)
{
	struct FRAME s;

	s.kind = FRAME_DATA;
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);		// 另一端的ack
	memcpy(s.data, buffer[frame_nr], PKT_LEN);

	dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

	put_frame((unsigned char *)&s, 3 + PKT_LEN);
	start_timer(frame_nr, DATA_TIMER);
	stop_ack_timer();
}


static void send_ack_frame(void)
{
    struct FRAME s;

    s.kind = FRAME_ACK;
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);;
	//ack_expected=(ack_expected+1)%(MAX_SEQ+1);
    dbg_frame("Send ACK  %d\n", s.ack);
    put_frame((unsigned char *)&s, 2);
}

int main(int argc, char **argv)
{
    int event, arg;
    struct FRAME f;
    int len = 0;

    protocol_init(argc, argv); 
    lprintf("Designed by Jiang Yanjun, build: " __DATE__"  "__TIME__"\n");

    disable_network_layer();
	frame_nr = 0;
	nbuffered = 0;
	frame_expected = 0;
	ack_expected=0;
    for (;;) {
        event = wait_for_event(&arg);

        switch (event) {//等待其中一个事件
        case NETWORK_LAYER_READY://网络层就绪
            get_packet(buffer[frame_nr]);//从网络层获取ip包窗口正在使用，发送数据到物理层
            nbuffered++;
			send_data_frame();
			frame_nr = (frame_nr + 1) % (MAX_SEQ + 1);
			break;

		case PHYSICAL_LAYER_READY://物理层就绪
            phl_ready = 1;
            break;

        case FRAME_RECEIVED: //收到帧
			//dbg_frame("Recv frame_nr: %d\n", frame_nr);
			start_ack_timer(ack_timer);
			len = recv_frame((unsigned char *)&f, sizeof f);//把帧放到f中
            if (len < 5 || crc32((unsigned char *)&f, len) != 0) {//若是坏帧
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				send_ack_frame();
                break;
            }
			if (f.kind == FRAME_ACK) //收到ACK
			{
				dbg_frame("Recv ACK  %d\n", f.ack);
				while (between(ack_expected, f.ack, frame_nr)) {
					nbuffered--;
					stop_timer(ack_expected);
					//dbg_frame("helloworld\n");
					ack_expected = (1 + ack_expected) % (MAX_SEQ + 1);
				}
				/*frame_nr = (f.ack + 1) % (MAX_SEQ + 1);
				for (int i = 1; i <= nbuffered; i++)
				{
					send_data_frame();
					frame_nr = (frame_nr + 1) % (MAX_SEQ + 1);
				}*/
			}
			if (f.kind == FRAME_DATA) {//收到数据
                dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
                if (f.seq == frame_expected) {//收到帧序号是需要的帧，打包帧，frame_expected,需要的下一帧，并发送ack帧
                    put_packet(f.data, len - 7);
                    //frame_expected =  (1+frame_expected)% (MAX_SEQ + 1);
					frame_expected = (1 + frame_expected) % (MAX_SEQ + 1);
				}
				while (between(ack_expected, f.ack, frame_nr)) {
					nbuffered--;
					stop_timer(ack_expected);
					//dbg_frame("helloworld\n");
					ack_expected= (1 + ack_expected) % (MAX_SEQ + 1);
				}
            } 
            /*if (f.ack == frame_nr) {//下一帧要传输帧等于接收帧的ack序号
                stop_timer(frame_nr);
                nbuffered--;
                frame_nr = 1 - frame_nr;
            }*/
            break; 

        case DATA_TIMEOUT://超时
			frame_nr = ack_expected;
			dbg_event("---- DATA %d timeout\n", arg); 
			for (int i = 1; i <=nbuffered; i++)
			{
				send_data_frame();
				frame_nr= (frame_nr + 1) % (MAX_SEQ + 1);
			}
			break;
		case ACK_TIMEOUT:
			send_ack_frame();
			break;
        }
        if (nbuffered < MAX_SEQ && phl_ready)//物理层就绪，窗口没有占用
            enable_network_layer();
        else
            disable_network_layer();
   }
}
