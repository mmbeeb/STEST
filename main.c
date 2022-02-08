/* BeebEm IP232 tester		*/
/* main.c 					*/
/* (c) 2021 Martin Mather	*/

#define _POSIX_C_SOURCE 1

#include <ctype.h>
#include <stdio.h> 
#include <stdint.h>
#include <stdlib.h> 
#include <string.h> 
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/socket.h> //cygwin

#include "serial.h"


void set_no_buffer() {
	struct termios term;
	tcgetattr(0, &term);
	term.c_lflag &= ~ICANON;
	tcsetattr(0, TCSANOW, &term);
}

int charsWaiting(int fd) {
	int count;
	
	if (ioctl(fd, FIONREAD, &count) == -1)
		exit (EXIT_FAILURE);
	
	return count;
}


static void die(char *s) {
	perror(s);
	exit(1);
}

int main() {
	clock_t timeout1 = time(0);

	serial_open(4);
	int bytes_in = 0, bytes_out = 0, lineout=0;
	char buf[200];

	int q = 0, c, connected = 0;
	set_no_buffer();
	while(!q) {

		if (time(0) > timeout1) {//second timer
			if (s_bytes_in || s_bytes_out)
				printf("loop timeout : bytes rx = %d  tx = %d\n", s_bytes_in, s_bytes_out);

			s_bytes_in = 0;
			s_bytes_out = 0;
			timeout1 = time(0);
		}

		serial_poll();

		if (serial_get_count() > 0) {
			uint8_t c;
			serial_get(&c , 1);
			bytes_in++;
			if ((c >= 0x20 && c < 0x7f) || c == 0xa || c == 0xd)
				printf("%c", c);
			else
				printf("[%02x]", c);
		}

		c = 0;
		int count = charsWaiting(fileno(stdin));
		if (count != 0) {
			c = tolower(getchar());
			if (c >= '1' && c <= '8') {
				serial_rate((int) c - '0');
			}
			switch (c) {
				case 'q':
					if (serial_connected())
						printf("STILL CONNECTED!\n");//Close connection using BeebEm first.
					else {
						printf("Quit\n");
						q=1;
					}
					break;
				case 'x':
					sprintf(buf, "%04x *** HELLO WORLD HELLO WORLD ***\r", lineout);
					if (serial_put(buf, strlen(buf)) == 0) {
						printf("%s\n", buf);
						lineout++;
					}
					break;
				case 'z':
					printf("\n");
					break;
				case 'e':
					serial_set_nrts(0);
					break;
				case 'd':
					serial_set_nrts(1);
					break;
			}
		}
	}

	serial_close();
}
 
