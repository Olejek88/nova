#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <stdint.h>
#include <sqlite3.h>
#include "buf.h"
#include "parser.h"
#include "eval.h"
#include "db.h"


struct device;


struct exprls {
	struct token *tok;
	struct exprls *next;
};

struct vk_operations {
	int  (*init)(struct device *);
	void (*free)(struct device *);

	int  (*send_msg)(struct device *);
	int  (*parse_msg)(struct device *, char *, int);
	int  (*check_crc)(struct device *, char *, int);

	/* functions */
	int  (*get)(struct device *, int, char **);
	int  (*set)(struct device *, int, char *, char **);
	int  (*h_archiv)(struct device *, int, const char *, const char *,
			struct archive **);
	int  (*m_archiv)(struct device *, int, const char *, const char *,
			struct archive **);
	int  (*d_archiv)(struct device *, int, const char *, const char *,
			struct archive **);

	int  (*events)(struct device *, const char *, const char *,
			struct events **);

	int  (*setf)(struct device *, int, float, int *);

	int  (*date)(struct device *, const char *, char **);

	int  (*debug)(struct device *, const char *, int *);
};

struct device {
	/* protocol parameters */
	int octet_timeout;
	int msg_timeout;
	int msg_delay;
	int reset_timeout;

	const char *provider_key, *user_key;

	uint8_t devaddr;
	uint8_t cmd;

	/* session variables */
	uint64_t last_send_usecs;

	const char *vktype;
	struct vk_operations *opers;
	struct exprls *exprs;
	void *spec;

	int fd;

	int resend_tries;

	struct buf buf;

	char highest_baudrate_mark;
	char reset_flag;
	char nonblock_flag;
	char quiet;

	/* db */
	sqlite3 *db;

	/* COM port */
	const char *path;
	int baudrate;
	int databits, stopbits, parity;

	/* TCP */
	const char *hostname;
	const char *port;
};

void dev_init(struct device *);
void dev_free(struct device *);

int  dev_connect(struct device *);
void dev_close(struct device *);

int  dev_run(struct device *);
int  dev_query(struct device *);
int  dev_write(struct device *, const void *, int);
int  dev_add_expr(struct device *, const char *);

void devlog(struct device *, const char *, ...);
void dump_msg(const char *, const void *, int);

int  mic_sleep(uint64_t);

#endif/*_DEVICE_H_*/
