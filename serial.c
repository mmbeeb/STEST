/* BeebEm IP232 tester		*/
/* serial.c					*/
/* (c) 2021 Martin Mather	*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
//#include <malloc.h>
#include <time.h>

#include "serial.h"

#define PORT	25232	//BeebEm IP232 default
#define BACKLOG	1	//how many pending connections queue will hold

static struct sockaddr_in si_me, si_other;
static int l_sock = 0, c_sock = 0, slen = sizeof(si_other), connected = 0;

#define SBUF_SIZE	200

#define TXBUF_SIZE	200
#define RXBUF_SIZE	1000

static uint8_t txbuf[TXBUF_SIZE + 1], rxbuf[RXBUF_SIZE];
static int txcount = 0, not_clear_to_send = 1, not_request_to_send = 1, flag_byte = 0;
static int clock_divide = 0, trigger1 = 0, trigger2 = 0, baud_rate;

int s_bytes_in = 0, s_bytes_out = 0;

static struct sbuf_t {
	uint8_t buf[SBUF_SIZE];
	int free, count, tail, head, overflow;
} sbufs[2], *sbuf_output = &sbufs[0], *sbuf_input = &sbufs[1];

static void die(char *s) {
	perror(s);
	exit(1);
}

int serial_connected(void) {
	return connected;
}

static void _trigger_reset(void) {
	trigger1 = 0;
	trigger2 = 0;
}

static void _trigger_set(void) {
	static unsigned int t1 = 0, t2 = 0;
	unsigned int n1, n2;
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	n1 = ts.tv_nsec / 10000000;		//centiseconds
	n2 = (ts.tv_nsec / (100000000/192)) >> clock_divide;
	trigger1 = trigger1 || (n1 != t1);
	trigger2 = trigger2 || (n2 != t2);
	t1 = n1;
	t2 = n2;
}

static void _send_nrts_state(void) {
	if (connected) {
		uint8_t buf[2];
		buf[0] = 0xff;
		buf[1] = not_request_to_send;
		if (send(c_sock, &buf, 2, 0) == -1)
			die("Serial: send rts");
		printf("[nRTS=%d]\n", buf[1]);
	}
}

void serial_set_nrts(int nrts) {
	not_request_to_send = nrts;
	_send_nrts_state();
}

static void _sbuf_flush(struct sbuf_t *p) {
	memset(p, 0, sizeof(struct sbuf_t));
	p->free = SBUF_SIZE;
}

static void _sbuf_reset(void) {
	_sbuf_flush(sbuf_input);//input buffer
	_sbuf_flush(sbuf_output);//output buffer
	not_request_to_send = 1;
	_send_nrts_state();
	txcount = 0;
}

static int _sbuf_put(struct sbuf_t *p, uint8_t *c) {
	if (p->free == 0) {
		printf("Serial: overflow\n");
		p->overflow = 1;
		return -1;
	}

	p->buf[p->tail++] = *c;
	p->tail %= SBUF_SIZE;
	p->count++;
	p->free--;
	return 0;
}

static int _sbuf_get(struct sbuf_t *p, uint8_t *c) {
	if (p->count == 0)
		return -1;

	*c = p->buf[p->head++];
	p->head %= SBUF_SIZE;
	p->count--;
	p->free++;
	p->overflow = 0;
	return 0;
}

int serial_put_free(void) {
	return sbuf_output->free;
}

int serial_put(uint8_t *c, int len) {
	if (len > sbuf_output->free)
		return -1;

	for (int i = 0; i < len; i++)
		_sbuf_put(sbuf_output, c++);

	return 0;
}

int serial_get_count(void) {
	return sbuf_input->count;
}

int serial_get(uint8_t *c, int len) {
	if (len > sbuf_input->count)
		return -1;

	for (int i = 0; i < len; i++)
		_sbuf_get(sbuf_input, c++);

	return 0;
}

int serial_rate(int b_rate) {
	const int bauds[] = {75, 150, 300, 1200, 2400, 4800, 9600, 19200};

	if (b_rate > 0 && b_rate <= 8) {
		baud_rate = bauds[b_rate - 1];
		clock_divide = (8 - b_rate);
		if (b_rate < 4)
				clock_divide++;
		printf("Serial: rate = %d , approx bytes/sec = %d , clk_div = %d\n", baud_rate, baud_rate / 10, clock_divide);
		_trigger_reset();
	} else
		printf("Serial: invalid rate\n");
	
}

int serial_open(int b_rate) {
	//create a TCP socket
        if ((l_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
            die("Serial: socket");

	//zero out the structure
	memset((char *)&si_me, 0, sizeof(si_me));

        si_me.sin_family = AF_INET;
        si_me.sin_port = htons(PORT);
        si_me.sin_addr.s_addr = INADDR_ANY;

	//bind socket to port
        if (bind(l_sock, (struct sockaddr *)&si_me, sizeof(si_me)) == -1)
            die("Serial: bind");

	//make socket non-blocking
	int flags = fcntl(l_sock, F_GETFL, 0);
	if (flags == -1)
		die("Serial: get flags");

	if (fcntl(l_sock, F_SETFL, flags | O_NONBLOCK) == -1)
		die("Serial: set flags");

	//listen for incoming connection
	printf("Serial: listening...\n");
        if (listen(l_sock, BACKLOG) == -1)
            die("Serial: listen");

	serial_rate(b_rate);
}

int serial_close(void) {
	close(c_sock);
	close(l_sock);
}

int serial_poll(void) {
	if (!connected) {
		c_sock = accept(l_sock, (struct sockaddr *)&si_other, &slen);

		if (c_sock == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				die("Serial: accept()");
		} else {
			fcntl(c_sock, F_SETFL, O_NONBLOCK);
			connected = 1;
			printf("Serial: CONNECTED\n");

			_sbuf_reset();
			_trigger_reset();
		}
	} else {
		int n = recv(c_sock, rxbuf, RXBUF_SIZE, 0);

		if (n < 0) { 
			if (errno != EWOULDBLOCK)
				die("Serial: recvfrom()");
		} else if (n == 0) {
			connected = 0;
			close(c_sock);
			c_sock = 0;
			printf("Serial: DISCONNECTED\n");
		} else {
			for (int i = 0; i < n; i++) {
				uint8_t c = rxbuf[i];
				if (flag_byte) {
					flag_byte = 2;

					if (c == 0) {
						not_clear_to_send = 0;
						printf("[nCTS=0]\n");
					} else if (c == 1) {
						not_clear_to_send = 1;
						printf("[nCTS=1]\n");
					} else
						flag_byte = 0;
				}
				else if (c == 0xff)
					flag_byte = 1;

				if (!flag_byte) {
					_sbuf_put(sbuf_input, &c); 
					s_bytes_in++;
				} else if (flag_byte == 2)
					flag_byte = 0;
			}

		}
	}
	
	if (connected && !not_clear_to_send) {
		_trigger_set();
		
		if (trigger2) {
			if (sbuf_output->count > 0 && txcount < TXBUF_SIZE) {
				//move byte to txbuf
				_sbuf_get(sbuf_output, &txbuf[txcount]);
				if (txbuf[txcount++] == 0xff)
					txbuf[txcount++] = 0xff;
			}
		}

		if (trigger1) {
			if (txcount > 0) {
				if (send(c_sock, txbuf, txcount, 0) == -1)
					die("Serial: send");

				s_bytes_out += txcount;
				txcount = 0;
			}
		}
		
		_trigger_reset();
	}
}

