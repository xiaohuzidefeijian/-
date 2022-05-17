#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "datalink.h"
#define DATA_TIMER 1100
#define MAX_SEQ 5
#define ACK_LIMIT_TIME 50

#define increase(k) \
 if (k < MAX_SEQ) \
  k = k + 1; \
 else \
  k = 0;
typedef struct
{
	unsigned char kind; /* FRAME_DATA */
	unsigned char ack;
	unsigned char seq;
	unsigned char data[PKT_LEN];
	unsigned int padding;
} frame;
static unsigned char buffer[MAX_SEQ + 1][PKT_LEN], nbuffered = 0;
static unsigned char frame_expected = 0, ack_expected = 0, next_frame_to_send = 0;
// frame_expected���մ���
// ack_expected���ʹ��ڵĿ�ʼ
// next_frame_to_send���ʹ�����Ҫ���͵���һ֡
static int phl_ready = 0;
int no_nak = 1;

int isInside(unsigned char x, unsigned char lb, unsigned char ub)
{
	if (lb<=ub)
	{
		if (x >= lb && x < ub)
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		if (x >= lb || x < ub)
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}
}
static void put_frame(unsigned char* frame, int len) //���CRC������
{
	*(unsigned int*)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}
static void send_data_frame(void) //��һ������֡���͸�����㣬��ʼ��ʱ����ʱ
{
	frame s;
	s.kind = FRAME_DATA;
	s.seq = next_frame_to_send;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	memcpy(s.data, buffer[next_frame_to_send], PKT_LEN);
	dbg_frame("Send DATA %d %d, ID %d,buffer-sum %d\n", s.seq, s.ack, *(short*)s.data, nbuffered);
	put_frame((unsigned char*)&s, 3 + PKT_LEN);
	start_timer(next_frame_to_send, DATA_TIMER);
	stop_ack_timer();
}
static void send_ack_frame(void) //��һ��ACK֡���͸������
{
	frame s;
	s.kind = FRAME_ACK;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	dbg_frame("Send ACK %d\n", s.ack);
	put_frame((unsigned char*)&s, 2);
}
static void send_nak_frame(void) //��һ��nak֡���͸������
{
	frame s;
	s.kind = FRAME_NAK;
	s.ack = frame_expected % (MAX_SEQ + 1);
	no_nak = 0;
	dbg_frame("Send NAK %d\n", s.ack);
	put_frame((unsigned char*)&s, 2);
}
int main(int argc, char** argv) {
	int event, arg;
	frame f;
	int len = 0;
	int i;
	protocol_init(argc, argv);
	disable_network_layer(); //��ʼ״̬��������ֹ�ݽ�����
	while (1)
	{
		event = wait_for_event(&arg); //argΪ��ʱ�����
		switch (event)
		{
		case NETWORK_LAYER_READY: //������з���Ҫ����
			stop_ack_timer();
			nbuffered++;
			get_packet(buffer[next_frame_to_send]);
			send_data_frame();
			increase(next_frame_to_send);
			break;
		case PHYSICAL_LAYER_READY: //�����׼������
			phl_ready = 1;
			break;
		case FRAME_RECEIVED: //һ������֡����
			len = recv_frame((unsigned char*)&f, sizeof f);
			if (len < 5 || crc32((unsigned char*)&f, len) != 0)
			{
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				if (no_nak && (f.seq == frame_expected))
				{
					send_nak_frame();
				}
				break;
			}
			if (f.kind == FRAME_NAK)
			{
				stop_ack_timer();
				dbg_event("----Recv NAK DATA %d timeout\n", f.ack);
				next_frame_to_send = f.ack;
				f.ack = (f.ack + MAX_SEQ) % (MAX_SEQ + 1);
				while (isInside(f.ack, ack_expected, next_frame_to_send))
				{
					nbuffered = nbuffered - 1;
					stop_timer(ack_expected);
					increase(ack_expected);
				}
					for (i = 1; i <= nbuffered; i++)
					{
						send_data_frame();
						dbg_frame("RESEND %d\n", next_frame_to_send);
						increase(next_frame_to_send);
					}
			}
			else
			{
				if (f.kind == FRAME_DATA)
				{
					dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short*)f.data);
					if (f.seq == frame_expected)
					{
						put_packet(f.data, len - 7);
						start_ack_timer(ACK_LIMIT_TIME); //��ʼһ��ack��ʱ������ʱ���͵�����ack֡����������֡���ͣ���ֹͣ��ʱ
							increase(frame_expected);
						no_nak = 1;
					}
					else
					{
						dbg_frame("Not expected, abort\n");
					}
				}
				if (f.kind == FRAME_ACK)
				{
					dbg_frame("Recv ACK %d\n", f.ack);
				}
				while (isInside(f.ack, ack_expected, next_frame_to_send))
				{
					nbuffered = nbuffered - 1;
					stop_timer(ack_expected);
					increase(ack_expected);
				}
			}
			break;
		case DATA_TIMEOUT: //TIMER��ʱ���ش����ʹ���������֡
			dbg_event("---- DATA %d timeout\n", arg);
			stop_ack_timer();
			next_frame_to_send = ack_expected;
			for (i = 1; i <= nbuffered; i++)
			{
				send_data_frame();
				increase(next_frame_to_send);
			}
			break;
		case ACK_TIMEOUT: //ACK��ʱ��ACK_LIMIT_TIME��û�еȵ����Ӵ�������֡����������ACK֡
			stop_ack_timer();
			send_ack_frame();
			break;
		}
		if (nbuffered < MAX_SEQ && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}