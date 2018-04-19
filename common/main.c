#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <assert.h>
#include <ctype.h>
#include <iconv.h>

#include "device.h"
#include "errors.h"
#include "db.h"


static int  parse_argv(struct device *, int, char **);
static int  print_error(struct device *);
static void print_usage(FILE *, const char *, int);


int
main(int argc, char **argv)
{
	struct device dev;
	int ret;

	dev_init(&dev);
	if (0 == parse_argv(&dev, argc, argv)) {
		ret = dev_connect(&dev);
		if (0 == ret) {
			ret = dev_run(&dev);
			dev_close(&dev);
		}
		if (-1 == ret)
			ret = print_error(&dev);
		dev_free(&dev);
	} else {
		ret = 1;
	}
	return ret;
}

static int
conv_timeout(const char *str, int *save)
{
	int tmp;

	if (1 != sscanf(str, "%d", &tmp))
		return -1;
	if (tmp < 0)
		return -1;

	*save = tmp;
	return 0;
}

static int
conv_baudrate(const char *str, int *save)
{
	int tmp;

	if (1 != sscanf(str, "%d", &tmp)) {
		fprintf(stderr, "Error: cannot convert to integer: %s\n", str);
		return -1;
	}
	switch (tmp) {
	case 50: case 75:
	case 110: case 134: case 150:
	case 200: case 300: case 600:
	case 1200: case 1800: case 2400:
	case 4800: case 9600:
	case 19200: case 38400: case 57600:
	case 115200: case 230400:
		*save = tmp;
		break;
	default:
		fprintf(stderr, "Error: %d baudrate isn't supported.\n", tmp);
		return -1;
	}
	return 0;
}

static int
conv_databits(const char *str, int *save)
{
	int tmp;

	if (-1 == sscanf(str, "%d", &tmp)) {
		fprintf(stderr, "Error: cannot convert %s to number.\n", str);
		return -1;
	}
	switch (tmp) {
	case 6: case 7: case 8:
		*save = tmp;
		return 0;
	default:
		fprintf(stderr, "Error: valid vales for databits is 6, 7 and 8.\n");
		return -1;
	}
}

static int
conv_parity(const char *str, int *save)
{
	if (0 == strcmp(str, "none")) {
		*save = 'n';
	} else if (0 == strcmp(str, "even")) {
		*save = 'e';
	} else if (0 == strcmp(str, "odd")) {
		*save = 'o';
	} else {
		fprintf(stderr, "Error: Valid values is \"none\", \"even\", \"odd\"\n");
		return -1;
	}
	return 0;
}

static int
conv_stopbits(const char *str, int *save)
{
	int tmp;

	if (1 != sscanf(str, "%d", &tmp)) {
		fprintf(stderr, "Error: Cannot convert %s to number.\n", str);
		return -1;
	}
	switch (tmp) {
	case 1: case 2:
		*save = tmp;
		return 0;
	default:
		fprintf(stderr, "Error: Valid values for stopbits is 1 and 2.\n");
		return -1;
	}
}

static int
conv_devaddr(const char *str, uint8_t *save)
{
	int tmp;

	if (1 != sscanf(str, "%d", &tmp)) {
		fprintf(stderr, "Error: Cannot convert %s to number.\n", str);
		return -1;
	} else if (tmp < 0 || tmp > 0xFF) {
		fprintf(stderr, "Error: Invalid address.\n");
		return -1;
	} else {
		*save = (uint8_t)(tmp & 0xFF);
		return 0;
	}
}

static int
conv_param(const char *str, struct device *dev)
{
	int ret;

	if (*str == '\0') {
		fprintf(stderr, "Error: Empty argument isn't valid parameter.\n");
		ret = -1;
	} else if (-1 == dev_add_expr(dev, str)) {
		fprintf(stderr, "Error: Syntax error: invalid expression: %s.\n", str);
		ret = -1;
	} else {
		ret = 0;
	}
	return ret;
}

static int
conv_cmd(const char *str, uint8_t *save)
{
	int tmp;

	if (1 != sscanf(str, "%x", &tmp)) {
		fprintf(stderr, "Error: cannot convert %s to number.\n", str);
		return -1;
	}
	if (tmp < 0 || tmp > 0xFF) {
		fprintf(stderr, "Error: number is out of bound.\n");
		return -1;
	}
	*save = (uint8_t)(tmp & 0xFF);
	return 0;
}

/* */
extern void dumb_get_operations(struct vk_operations **);
extern void ek270_get_operations(struct vk_operations **);
extern void tekon17_get_operations(struct vk_operations **);
extern void tekon19_get_operations(struct vk_operations **);
extern void spg761_get_operations(struct vk_operations **);
extern void spg741_get_operations(struct vk_operations **);
extern void vkg2_get_operations(struct vk_operations **);
extern void im2300_get_operations(struct vk_operations **);
extern void spg742_get_operations(struct vk_operations **);

static int
conv_vktype(const char *str, struct vk_operations **save)
{
	int ret = 0;

	if (0 == strcmp("dumb", str)) {
		dumb_get_operations(save);
	} else if (0 == strcmp("ek260", str)) {
		ek270_get_operations(save);
	} else if (0 == strcmp("ek88", str)) {
		ek270_get_operations(save);
	} else if (0 == strcmp("ek270", str)) {
		ek270_get_operations(save);
	} else if (0 == strcmp("tekon17", str)) {
		tekon17_get_operations(save);
	} else if (0 == strcmp("tekon19", str)) {
		tekon19_get_operations(save);
	} else if (0 == strcmp("spg761", str)) {
		spg761_get_operations(save);
	} else if (0 == strcmp("spg762", str)) {
		spg761_get_operations(save);
	} else if (0 == strcmp("spg741", str)) {
		spg741_get_operations(save);
	} else if (0 == strcmp("spg742", str)) {
		spg742_get_operations(save);
	} else if (0 == strcmp("vkg2", str)) {
		vkg2_get_operations(save);
	} else if (0 == strcmp("im2300", str)) {
		im2300_get_operations(save);
	} else {
		fprintf(stderr, "Error: no such VK: %s\n", optarg);
		ret = -1;
	}

	return ret;
}

static void
conv_path(const char *arg, const char **save)
{
	char buf[1024];
	int i, sz;

	sz = strlen(arg);
	for (i = 0; i < sz && i < sizeof(buf) - 1; ++i)
		buf[i] = tolower(arg[i]);
	buf[i] = '\0';

	if (0 == strcmp(buf, "rs-232"))
		*save = "/dev/ttyUSB2";
	else if (0 == strcmp(buf, "rs-232-db9"))
		*save = "/dev/ttyUSB0";
	else if (0 == strcmp(buf, "rs-485"))
		*save = "/dev/ttyUSB1";
	else
		*save = arg;
}

static int 
parse_argv(struct device *dev, int argc, char **argv)
{
	static const char *sopts =
		"a:b:D:c:s:S:K:p:t:T:G:g:R:H:P:d:k:y:rqh";
	int n, ret;

	for (ret = 0; ret != -1 ; ) {
		n = getopt(argc, argv, sopts);
		if (-1 == n)
			break;
		switch (n) {
		case 'a':
			if (-1 == conv_devaddr(optarg, &dev->devaddr))
				ret = -1;
			break;
		case 'b':
			if (-1 == conv_baudrate(optarg, &dev->baudrate))
				ret = -1;
			break;
		case 'D':
			if (-1 == conv_databits(optarg, &dev->databits))
				ret = -1;
			break;
		case 'c':
			if (-1 == conv_parity(optarg, &dev->parity))
				ret = -1;
			break;
		case 's':
			if (-1 == conv_stopbits(optarg, &dev->stopbits))
				ret = -1;
			break;
		case 'g':
			if (-1 == conv_param(optarg, dev))
				ret = -1;
			break;
		case 'G':
			if (-1 == conv_cmd(optarg, &dev->cmd))
				ret = -1;
			break;
		case 'p':
			conv_path(optarg, &dev->path);
			break;
		case 'K':
			dev->user_key = optarg;
			break;
		case 'k':
			dev->vktype = optarg;
			if (-1 == conv_vktype(optarg, &dev->opers))
				ret = -1;
			break;
		case 'S':
			dev->provider_key = optarg;
			break;
		case 'r':
			dev->reset_flag = 1;
			break;
		case 'H':
			dev->hostname = optarg;
			break;
		case 'P':
			dev->port = optarg;
			break;
		case 'R':
			if (-1 == conv_timeout(optarg, &dev->reset_timeout)) {
				fprintf(stderr, "Error: Reset timeout "
						"must be > 0.\n");
				ret = -1;
			}
			break;
		case 't':
			if (-1 == conv_timeout(optarg, &dev->octet_timeout)) {
				fprintf(stderr, "Error: Octet timeout "
						"must be > 0.\n");
				ret = -1;
			}
			break;
		case 'T':
			if (-1 == conv_timeout(optarg, &dev->msg_timeout)) {
				fprintf(stderr, "Error: Message timeout "
						"must be > 0.\n");
				ret = -1;
			}
			break;
		case 'd':
			if (-1 == conv_timeout(optarg, &dev->msg_delay)) {
				fprintf(stderr, "Error: Message delay "
						"must be > 0.\n");
				ret = -1;
			}
			break;
		case 'y':
			if (-1 == open_db(&(dev->db), optarg)) {
				fprintf(stderr, "Error: cannot open database");
				ret = -1;
			}
			break;
		case 'q':
			dev->quiet = 1;
			break;
		case 'h':
			print_usage(stdout, argv[0], 0);
		case '?':
		default:
			print_usage(stderr, argv[0], 1);
		}
	}
	if (0 == ret && NULL == dev->opers) {
		fprintf(stderr, "Error: You have to choose vktype.\n");
		ret = -1;
	}
	return ret;
}

static int 
print_error(struct device *dev)
{
	switch (get_error_id()) {
	case ERR_SYSTEM:
		fprintf(stderr, "Error: syscall(%s) is failed: %s\n",
				get_error_msg(), strerror(get_last_errno()));
		return 2;
	case ERR_LOST_DEVICE:
		fprintf(stderr, "Error: device is lost during query.\n");
		return 3;
	case ERR_TIMEOUT:
		fprintf(stderr, "Error: device doesn't respond.\n");
		return 4;
	case ERR_BADMSG:
		fprintf(stderr, "Error: unexpected messega was received.\n");
		return 5;
	case ERR_BUSY:
		fprintf(stderr, "Error: device is busy.\n");
		return 6;
	case ERR_CONNECT:
		fprintf(stderr, "Error: Connection is refused.\n");
		return 7;
	case ERR_BADTRANSPORT:
		fprintf(stderr, "Error: Too many damaged messages is received.\n");
		return 8;
	default:
		assert(0);
	}
}

static void
print_usage(FILE *stream, const char *progname, int ecode)
{
	fprintf(stream, "Usage: %s [OPTIONS]\n"
			"Options:\n"
			"\t-a <num>   - device address.\n"
			"\t-p <path>  - path to serial port.\n"
			"\t-D <bits>  - set number of data bits.\n"
			"\t-b <baud>  - set baudrate.\n"
			"\t-c <parity>- set parity checking.\n"
			"\t-s <bits>  - set number of stop bits.\n"
			"\t-g <expr>  - execute expression <expr>.\n"
			"\t-r         - This flag is mean nothing.\n"
			"\t-R <ms>    - Set reset timeout.\n"
			"\t-t <ms>    - Set octet timeout. How long wait next byte.\n"
			"\t-T <ms>    - How long wait response.\n"
			"\t-d <ms>    - How long wait before send message.\n"
			"\t-S <key>   - Set provider key.\n"
			"\t-K <key>   - Set user key.\n"
			"\t-H <addr>  - Connect to TCP socket rather than serial port.\n"
			"\t-P <port>  - Set port number for TCP connection.\n"
			"\t-q         - Don't print debug messages.\n"
			"\t-k <name>  - name of VK.\n"
			"\t-y <path>  - path to database for storing values.\n"
			"\t-h         - show this message and exit.\n"
			"Expression examples:\n"
			"\t1 + 1, 3 * (4 - 3), 3 * get(13)\n"
			"Available functions:\n"
			"\tget(INTEGER)                    - get parameter value.\n"
			"\tset(INTEGER,STRING)             - set value to parameter.\n"
			"\tharchive(INTEGER, DATE1, DATE2) - get hour archives for [DATE1, DATE2] interval.\n"
			"\tdarchive(INTEGER, DATE1, DATE2) - get day archives for [DATE1, DATE2] interval.\n"
			"\tmarchive(INTEGER, DATE1, DATE2) - get month archives for [DATE1, DATE2] interval.\n"
			"\tevents(DATE1,DATE2)             - get events from VK for [DATE1, DATE2] interval.\n"
			"\tdate(EMPTY STRING or DATE)      - get or set date from VK.\n"
			"where\n"
			"\tINTEGER   - is integer number (for example: 1, 3, 9)\n"
			"\tSTRING    - is everything between double quotes (for example: \"3434\", \"434\")\n"
			"\tDATE      - is string representing date in \"Y-m-d,H:M:S\" format.\n"
			"\tEMPTY STRING - is \"\"\n"
			"Default values:\n"
			"\tBaudrates       - 9600 19200\n"
			"\tData bits       - 8\n"
			"\tStop bits       - 1\n"
			"\tParity checking - none\n"
			"\tTCP port        - 2001\n"
			"\tProvider key    - 00000000\n"
			"\tClient key      - 00000000\n"
			"\tReset timeout   - 2000 ms\n"
			"\tOctet timeout   - 1000 ms\n"
			"\tMessage timeout - 10000 ms\n"
			"Available VKs:\n"
			"\tek270, ek260, ek88\n"
			"\ttekon17, tekon19\n"
			"\tspg741, spg742, spg761, spg762\n"
			"\tvkg2, im2300\n"
			"",
			progname);
	exit(ecode);
}

