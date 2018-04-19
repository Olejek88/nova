#define _POSIX_C_SOURCE 200112L

#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/file.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>

#include "regex_utils.h"
#include "device.h"
#include "utils.h"
#include "errors.h"

/* */
int open_serial(const char *, int, int, int, int);
#define dev_open_serial(dev)	\
	open_serial((dev)->path, (dev)->baudrate, (dev)->databits, \
			(dev)->parity, (dev)->stopbits)

int tcp_connect(const char *, const char *);
#define dev_open_tcp(dev)	\
	tcp_connect(dev->hostname, dev->port)

/* */
#define FIXED_PACK_SZ	9

/* */
void
devlog(struct device *dev, const char *fmt, ...)
{
	va_list ap;
	char buf[128];

	if (dev->quiet)
		return;

	snprintf(buf, sizeof(buf), "DEBUG: %s\n", fmt);

	va_start(ap, fmt);
	vfprintf(stderr, buf, ap);
	va_end(ap);
}

void
dump_msg(const char *preffix, const void *p, int sz)
{
	const uint8_t *s;
	int i;

	fprintf(stderr, "%s: Dump[%d]: ", preffix, sz);
	for (i = 0, s = p; i < sz; ++i)
		fprintf(stderr, "%.2x ", s[i]);
	fprintf(stderr, "\n");
}

int
mic_sleep(uint64_t usecs)
{
	struct timeval tm;
	int ret;

	tm.tv_sec  = usecs / 1000000;
	tm.tv_usec = usecs % 1000000;

	ret = select(0, NULL, NULL, NULL, &tm);
	if (-1 == ret)
		set_system_error("select");
	return ret;
}

static int
get_uptime(uint64_t *save)
{
	struct timespec ts;
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (0 == ret) {
		*save = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
	} else {
		set_system_error("clock_gettime");
	}
	return ret;
}

static int
dev_try_sleep(struct device *dev)
{
	uint64_t usecs, last_usecs;
	int ret;

	ret = get_uptime(&usecs);
	if (0 == ret) {
		last_usecs = dev->last_send_usecs + dev->msg_delay * 1000;
		if (usecs < last_usecs) {
			last_usecs -= usecs;
			ret = mic_sleep(last_usecs);
		}
	}
	return ret;
}

int
dev_write(struct device *dev, const void *buf, int bufsz)
{
	int ret;

	if (dev->msg_delay > 0 && -1 == dev_try_sleep(dev))
		return -1;

	if (!dev->quiet)
		dump_msg("SEND", buf, bufsz);

	ret = write(dev->fd, buf, bufsz);
	if (-1 == ret) {
		set_system_error("write");
	} else if (dev->msg_delay > 0) {
		ret = get_uptime(&dev->last_send_usecs);
	} else {
		ret = 0;
	}
	return ret;
}


static int
recv_data(int fd, struct buf *buf, int timeout)
{
	struct timeval tm;
	fd_set fds;
	int ret;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	tm.tv_sec  = timeout / 1000;
	tm.tv_usec = (timeout * 1000) % (1000 * 1000);

	ret = select(fd + 1, &fds, NULL, NULL, &tm);
	if (0 == ret) {
		set_error(ERR_TIMEOUT, "recv_data");
		return -1;
	}
	if (-1 == ret) {
		set_system_error("select");
		return -1;
	}

	ret = buf_read(buf, fd);
	if (0 == ret) {
		set_error(ERR_LOST_DEVICE, "read");
		return -1;
	}
	if (-1 == ret) {
		set_system_error("read");
		return -1;
	}
	return 0;
}

int
dev_recv_msg(struct device *dev, struct buf *buf)
{
	int ret, tm;

	for (tm = dev->msg_timeout; /* empty */; tm = dev->octet_timeout) {
		ret = recv_data(dev->fd, buf, tm);
		if (-1 == ret)
			break;

		ret = dev->opers->parse_msg(dev, buf->p, buf->len);
		if (0 == ret)
			break;

	}

	if (-1 == ret && dev->buf.len > 0)
		set_error(ERR_BADMSG, "dev_recv_msg");

	if (!dev->quiet && -1 == ret && buf->len > 0)
		dump_msg("\nRECV", buf->p, buf->len);

	return ret;
}

int
dev_query(struct device *dev)
{
	int tries, ret;

	for (tries = dev->resend_tries, ret = -1; tries > 0; --tries) {
		ret = dev->opers->send_msg(dev);
		if (-1 == ret)
			break;

		ret = dev_recv_msg(dev, &dev->buf);
		if (-1 == ret)
			break;

		ret = dev->opers->check_crc(dev, dev->buf.p, dev->buf.len);
		if (0 == ret)
			break;

		--tries;
	}
	return ret;
}

/*
static void
print_tokens(struct token *tok)
{
	while (tok) {
		printf("Token: type = %c(%d) str = %s i = %d f = %f\n",
			isprint(tok->type) ? (char)(tok->type) : '?',
			tok->type,
			tok->type == TOKEN_STRING || tok->type == TOKEN_SYMBOL ? tok->val.s : "NULL",
			tok->val.i,
			tok->val.f);
		tok = tok->next;
	}
}
*/

static void
print_res(struct res *res, int nline)
{
	switch (res->type) {
	case EVAL_INTEGER:
		printf("%d", res->val.i);
		break;
	case EVAL_FLOAT:
		printf("%f", res->val.f);
		break;
	case EVAL_STRING:
		printf("\"%s\"", res->val.s);
		break;
	case EVAL_ARRAY:
		{
			int i;

			printf("[");
			for (i = 0; i < res->val.arr.nres; ++i) {
				print_res(res->val.arr.res + i, 0);
				if (i + 1 < res->val.arr.nres)
					printf(", ");
			}
			printf("]");
		}
		break;
	case EVAL_UNDEF:
		printf("null");
		break;
	}
	if (nline)
		printf("\n");
}

static int
dev_run_exprs(struct device *dev)
{
	struct exprls *curr;
	struct res res;
	int ret, rv, num;

	for (ret = 0, curr = dev->exprs, num = 0; curr; curr = curr->next) {
		printf("%d: ", ++num);

		init_eval_error();

		init_res(&res);
		rv = do_eval(dev, curr->tok, &res);
		if (0 == rv)
			print_res(&res, 1);
		else
			printf("Error: %s\n", get_eval_error());
		free_res(&res);
	}
	return ret;
}

static void
exprls_free(struct exprls *head)
{
	struct exprls *d;

	while (head) {
		d = head;
		head = d->next;
		free_tokens(d->tok);
		free(d);
	}
}

static struct exprls *
exprls_add(const char *args, struct exprls *next)
{
	struct exprls *reg = malloc(sizeof(*reg));

	if (!reg) {
		set_system_error("malloc");
	} else if (-1 == parse_expr(args, &reg->tok)) {
		free(reg);
		reg = NULL;
	}
	return reg;
}

static void
exprls_push_back(struct exprls **top, struct exprls *reg)
{
	while (*top)
		top = &((*top)->next);
	*top = reg;
	reg->next = NULL;
}

static int
dev_add_exprls(struct exprls **top, const char *args)
{
	struct exprls *reg;
	int ret;

	reg = exprls_add(args, NULL);
	if (reg) {
		exprls_push_back(top, reg);
		ret = 0;
	} else {
		set_system_error("malloc");
		ret = -1;
	}
	return ret;
}

static int
dev_lock_port(struct device *dev, int fd)
{
	int flags;
	int ret;

	flags = LOCK_EX;
	if (dev->nonblock_flag)
		flags |= LOCK_NB;

	ret = flock(fd, flags);
	if (-1 == ret && EWOULDBLOCK == errno) {
		set_error(ERR_BUSY, "dev_lock_port");
	} else if (-1 == ret) {
		set_system_error("flock");
	} else {
		ret = 0;
	}
	return ret;
}

/* */
void
dev_init(struct device *dev)
{
	dev->path	= "/dev/ttyUSB0";
	dev->baudrate	= 0; /* do not touch baudrate */
	dev->databits	= 8;
	dev->parity	= 'n';
	dev->stopbits	= 1;

	dev->octet_timeout = 1000;
	dev->msg_timeout = 10000;
	dev->msg_delay   = 10;
	dev->reset_timeout = 2000;

	dev->last_send_usecs = 0;

	dev->provider_key = "00000000";
	dev->user_key = "00000000";
	dev->devaddr = 0x01;
	dev->cmd = 0x01;

	dev->resend_tries = 3;

	dev->exprs	= NULL;
	dev->opers	= NULL;
	dev->spec	= NULL;

	dev->highest_baudrate_mark = '5';
	dev->reset_flag = 0;
	dev->nonblock_flag = 0;
	dev->quiet = 0;

	dev->hostname	= NULL;
	dev->port	= "2001";

	dev->db		= NULL;

	buf_init(&dev->buf);
}

void
dev_free(struct device *dev)
{
	exprls_free(dev->exprs);
	dev->exprs = NULL;

	buf_free(&dev->buf);
}

int
dev_connect(struct device *dev)
{
	int fd, ret;

	fd = dev->hostname ? dev_open_tcp(dev) : dev_open_serial(dev);
	if (-1 == fd) {
		ret = -1;
	} else if (-1 == dev_lock_port(dev, fd)) {
		ret = -1;
	} else {
		dev->fd = fd;
		ret = 0;
	}
	return ret;
}

void
dev_close(struct device *dev)
{
	if (dev->nonblock_flag)
		flock(dev->fd, LOCK_UN);
	if (dev->db)
		close_db(dev->db);
	close(dev->fd);
}


int
dev_run(struct device *dev)
{
	int ret;

	signal(SIGPIPE, SIG_IGN);

	ret = dev->opers->init(dev);
	if (0 == ret) {
		ret = dev_run_exprs(dev);
		dev->opers->free(dev);
	}
	return ret;
}

int
dev_add_expr(struct device *dev, const char *str)
{
	return dev_add_exprls(&dev->exprs, str);
}

