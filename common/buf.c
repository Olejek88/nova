#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buf.h"

void
buf_init(struct buf *buf)
{
	memset(buf, 0, sizeof(*buf));
}

void
buf_free(struct buf *buf)
{
	free(buf->p);
	buf_init(buf);
}

int
buf_resize(struct buf *buf, int newsz)
{
	void *p;

	if (2 * buf->sz > newsz)
		newsz = 2 * buf->sz;

	p = realloc(buf->p, newsz + 1);
	if (p) {
		buf->p	= p;
		buf->sz	= newsz;
		if (buf->len >= newsz) {
			buf->p[newsz] = '\0';
			buf->len = newsz;
		}
	}
	return p ? 0 : -1;
}

int
buf_memcat(struct buf *buf, const void *p, int len)
{
	if (buf->sz <= buf->len + len && -1 == buf_resize(buf, buf->len + len))
		return -1;

	memcpy(buf->p + buf->len, p, len);
	buf->len += len;
	buf->p[buf->len] = '\0';
	return 0;
}

int
buf_strcat(struct buf *buf, const char *str)
{
	return buf_memcat(buf, str, strlen(str));
}

int
buf_putchar(struct buf *buf, char ch)
{
	char str[2];

	str[0] = ch;
	str[1] = '\0';
	return buf_memcat(buf, str, 1);
}

int
buf_read(struct buf *buf, int fd)
{
	int rv;

	if (buf->sz == buf->len && -1 == buf_resize(buf, buf->len + 1))
		return -1;
	rv = read(fd, buf->p + buf->len, buf_space_left(buf));
	if (rv > 0) {
		buf->len += rv;
		buf->p[buf->len] = '\0';
	}
	return rv;
}

void
buf_flush(struct buf *buf, int len)
{
	if (len < buf->len) {
		memmove(buf->p, buf->p + len, buf->len - len);
		buf->len -= len;
	} else {
		buf_free(buf);
	}
}

void *
buf_detach(struct buf *buf)
{
	void *ret = buf->p;

	buf_init(buf);
	return ret;
}

