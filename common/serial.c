#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#include "utils.h"
#include "errors.h"

static void
set_parity(struct termios *tty, int parity)
{
	switch (parity) {
	case 'n':
		tty->c_cflag &= ~(PARENB | PARODD);
		break;
	case 'e':
		tty->c_cflag |= PARENB;
		break;
	case 'o':
		tty->c_cflag |= (PARENB | PARODD);
		break;
	default:
		assert(0);
	}
}

static void
set_databits(struct termios *tty, int databits)
{
	switch (databits) {
	case 6:
		tty->c_cflag |= CS6;
		break;
	case 7:
		tty->c_cflag |= CS7;
		break;
	case 8:
		tty->c_cflag |= CS8;
		break;
	default:
		assert(0);
	}
}


static void
set_stopbits(struct termios *tty, int stopbits)
{
	switch (stopbits) {
	case 1:
		tty->c_cflag &= ~CSTOPB;
		break;
	case 2:
		tty->c_cflag |= CSTOPB;
		break;
	default:
		assert(0);
	}
}

static void
set_baudrate(struct termios *tty, int brate)
{
	static struct {
		int rate;
		speed_t baud;
	} baudrates[] = {
		{50,	B50},
		{75,	B75},
		{110,	B110},
		{134,	B134},
		{150,	B150},
		{200,	B200},
		{300,	B300},
		{600,	B600},
		{1200,	B1200},
		{1800,	B1800},
		{2400,	B2400},
		{4800,	B4800},
		{9600,	B9600},
		{19200,	B19200},
		{38400,	B38400},
		{57600,	B57600},
		{115200,B115200},
		{230400,B230400}
	};

	int i;

	for (i = 0; i < ARRAY_SIZE(baudrates); ++i)
		if (baudrates[i].rate == brate) {
			cfsetspeed(tty, baudrates[i].baud);
			break;
		}
}

int
open_serial(const char *path, int brate, int dbits, int pctrl, int sbits)
{
	struct termios tty;
	int fd;

	fd = open(path, O_RDWR | O_NOCTTY | O_NDELAY);
	if (-1 == fd) {
		set_system_error("open");
		goto fail;
	}
	if (-1 == tcgetattr(fd, &tty)) {
		set_system_error("tcgetattr");
		goto close_fd;
	}

	tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
	tty.c_oflag &= ~(OPOST|OCRNL|ONLCR|ONLRET);
	tty.c_lflag &= ~(ICANON|ECHO|ECHONL|ISIG|IEXTEN);
	tty.c_cflag &= ~(CSIZE|PARENB|PARODD|CSTOPB|CRTSCTS);
	tty.c_cflag |= (CLOCAL|CREAD);

	set_baudrate(&tty, brate);
	set_databits(&tty, dbits);
	set_parity(&tty, pctrl);
	set_stopbits(&tty, sbits);

	if (-1 == tcsetattr(fd, TCSANOW, &tty)) {
		set_system_error("tcsetattr");
		goto close_fd;
	}

	return fd;
close_fd:
	close(fd);
fail:
	return -1;
}

