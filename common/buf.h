#ifndef _BUF_H_
#define _BUF_H_

struct buf {
	char *p;
	int len, sz;
};

/* */
#define buf_space_left(buf)	((buf)->sz - (buf)->len)

/* */
void buf_init(struct buf *);
void buf_free(struct buf *);

int  buf_strcat(struct buf *, const char *);
int  buf_memcat(struct buf *, const void *, int);
int  buf_putchar(struct buf *, char);

int  buf_read(struct buf *, int);
void buf_flush(struct buf *, int);

void * buf_detach(struct buf *);

#endif/*_BUF_H_*/
