#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "utils.h"
#include "regex_utils.h"
#include "device.h"
#include "errors.h"
#include "eval.h"

/* */

struct ek270_ctx {

	char *send_query;
	int  send_query_sz;

	regex_t re;

	char crc_flag;

	uint8_t crc;
};

#define get_spec(dev)	((struct ek270_ctx *)((dev)->spec))

/* */
static int  ek270_init(struct device *);
static void ek270_free(struct device *);
static int  ek270_get(struct device *, int, char **);
static int  ek270_set(struct device *, int, char *, char **);
static int  ek270_parse_msg(struct device *, char *, int);
static int  ek270_parse_crc_msg(struct device *, char *, int);
static int  ek270_check_crc(struct device *, char *, int);
static int  ek270_send_msg(struct device *);
static int  ek270_debug(struct device *, const char *, int *);

static int  ek270_h_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  ek270_m_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  ek270_d_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  ek270_events(struct device *, const char *, const char *,
				struct events **);

static int  ek270_date(struct device *, const char *, char **);
static int  ek270_set_date(struct device *, const char *, char **);

/* */
static int  ek270_get_date(struct device *, char **);

/* */

static int
reset_session(struct device *dev)
{
	char msg[60];
	int ret;

	if (dev->reset_flag) {
		memset(msg, 0, sizeof(msg));
		ret = dev_write(dev, msg, sizeof(msg));
		if (0 == ret)
			ret = mic_sleep((uint64_t)dev->reset_timeout * 1000);
	} else {
		ret = 0;
	}
	return ret;
}

static uint8_t
calc_bcc(void *ptr, int n)
{
	uint8_t *b;
	uint8_t bcc;
	int i;

	for (i = 0, b = ptr, bcc = 0; i < n; ++i)
		bcc = bcc ^ b[i];

	return bcc;
}



static int
set_ctx_regex(struct ek270_ctx *ctx, const char *restr)
{
	regex_t re;
	int rv;

	rv = regcomp(&re, restr, REG_EXTENDED);
	if (0 == rv) {
		ctx->re = re;
	} else {
		set_regex_error(rv, &re);
		rv = -1;
	}
	return rv;
}

static void
ctx_zero(struct ek270_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

static int
ek270_get_ident(struct device *dev)
{
	static const char *restr  = "/([A-Za-z]{3})([0-9])([^\r\n]+)\r\n";
	static char ident_query[] = "/?!\r\n";
	struct ek270_ctx *ctx;
	char **ls;
	int nls;
	int ret;

	ctx = get_spec(dev);
	ret = -1;
	ctx->crc_flag = 0;

	ctx->send_query = ident_query;
	ctx->send_query_sz = strlen(ident_query);

	if (-1 == set_ctx_regex(ctx, restr))
		goto out;

	if (-1 == dev_query(dev))
		goto free_regex;

	nls = regex_split_raw(&ctx->re, dev->buf.p, &ls);
	if (-1 == nls)
		goto free_regex;
	assert(nls == 4);

	devlog(dev,
		"\tFull msg = \"%s\"\n"
		"\tXXX = \"%s\"\n"
		"\tBaudrate mark = %s\n"
		"\tIdentifier = \"%s\"\n"
		, ls[0], ls[1], ls[2], ls[3]
	);
	dev->highest_baudrate_mark = ls[2][0];

	ret = 0;

	free_strings(ls, nls);
	free(ls);

free_regex:
	regfree(&ctx->re);
out:
	ctx_zero(ctx);
	buf_free(&dev->buf);
	return ret;
}

static int
ek270_do_negotiation(struct device *dev)
{
	struct ek270_ctx *ctx;
	char *query;
	int ret;

	ctx = get_spec(dev);
	ret = -1;

	query = dynsprintf("\006%c%c%c\r\n", '0', '5', '1');
	if (!query)
		goto out;

	ctx->crc_flag = 0;
	ctx->send_query = query;
	ctx->send_query_sz = strlen(query);

	if (-1 == set_ctx_regex(ctx, "\001P0\002(\\([^\003]+\\))\003(.)"))
		goto free_query;

	if (-1 == dev_query(dev))
		goto free_all;

	if (!dev->quiet)
		dump_msg("EK270 NEGOT", dev->buf.p, dev->buf.len);
	ret = 0;

free_all:
	regfree(&ctx->re);
free_query:
	free(query);
out:
	buf_free(&dev->buf);
	ctx_zero(ctx);
	return ret;
}


/*
 * TODO: this function is deprecated, it should be removed.
 */
static int
make_query(struct device *dev, const char *cmd, const char *addr)
{
	struct ek270_ctx *ctx;
	char *query;
	uint8_t bcc;
	int sz;

	query = dynsprintf("\001%s\002%s\003%%", cmd, addr);
	if (!query)
		return -1;

	ctx = get_spec(dev);
	sz = strlen(query);

	/*
	 * BCC CRC calculated without SOH byte and
	 * last symbol ('%') which we use just to reserve
	 * byte for one.
	 */
	bcc = calc_bcc(query + 1, sz - 1 - 1);
	((uint8_t*)query)[sz - 1] = bcc;

	ctx->send_query = query;
	ctx->send_query_sz = sz;
	return 0;
}

static int
get_query(struct device *dev, const char *cmd, const char *fmt, ...)
{
	struct ek270_ctx *ctx;
	char bfmt[128];
	va_list ap;
	char *query;
	uint8_t bcc;
	int sz;

	sz = snprintf(bfmt, sizeof(bfmt), "\001%s\002%s\003%%%%", cmd, fmt);
	assert(-1 == sz || sz != sizeof(bfmt));

	va_start(ap, fmt);
	query = vdynsprintf(bfmt, ap);
	va_end(ap);

	if (NULL == query)
		return -1;

	ctx = get_spec(dev);
	sz = strlen(query);
	/*
	 * BCC CRC calculated without SOH byte and
	 * last symbol ('%') which we use just to reserve
	 * byte for one.
	 */
	bcc = calc_bcc(query + 1, sz - 1 - 1);
	((uint8_t *)query)[sz - 1] = bcc;

	ctx->send_query = query;
	ctx->send_query_sz = sz;
	return 0;
}

static int
ek270_authorize(struct device *dev)
{
	struct ek270_ctx *ctx;
	const char *key, *cmd;
	int ret;

	if (0 != strcmp("00000000", dev->provider_key)) {
		key = dev->provider_key;
		cmd = "3:171.0(%s)";
	} else if (0 != strcmp("00000000", dev->user_key)) {
		key = dev->user_key;
		cmd = "4:171.0(%s)";
	} else {
		return 0;
	}

	ctx = get_spec(dev);
	ret = -1;

	if (-1 == get_query(dev, "W1", cmd, key))
		goto out;

	if (-1 == set_ctx_regex(ctx, "\006|\025|\001B0\003."))
		goto free_query;

	if (!dev->quiet)
		devlog(dev, "Send key: %s\n", key);

	if (-1 == dev_query(dev))
		goto free_all;

	if (!dev->quiet)
		dump_msg("EK270 DEBUG KEY", dev->buf.p, dev->buf.len);
	ret = '\006' == dev->buf.p[0];

free_all:
	regfree(&ctx->re);
free_query:
	free(ctx->send_query);
out:
	buf_free(&dev->buf);
	ctx_zero(ctx);
	return ret;
}

static int
ek270_get_float_param(struct device *dev, const char *addr, char **save)
{
	const char *regstr = "\002([^\003]+)\003";
	struct ek270_ctx *ctx;
	void *fnsave;
	char **ls, **msgls;
	int nls, nmsgls;
	int ret;

	ctx = get_spec(dev);
	ret = -1;

	/* save previous handler */
	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = ek270_parse_crc_msg;

	if (-1 == make_query(dev, "R1", addr))
		goto out;

	if (-1 == set_ctx_regex(ctx, regstr))
		goto free_query;

	if (-1 == dev_query(dev))
		goto free_msg;

	nmsgls = regex_split_raw(&ctx->re, dev->buf.p, &msgls);
	if (-1 == nmsgls)
		goto free_msg;
	assert(nmsgls == 2);

	if (!dev->quiet)
		devlog(dev, "RECV: %s\n", msgls[0]);

	ls = NULL;
	nls = regex_split("([^()]*)\\(([^*()]+)(\\*[^()]+)?\\)", msgls[1], &ls);
	if (nls <= 0)
		goto free_msg_strings;

	*save = ls[2];
	ls[2] = NULL; /* to prevent deallocating */

	ret = 0;

	free_strings(ls, nls);
	free(ls);
free_msg_strings:
	free_strings(msgls, nmsgls);
	free(msgls);
free_msg:
	regfree(&ctx->re);
free_query:
	free(ctx->send_query);
out:
	/* restore handler */
	dev->opers->parse_msg = fnsave;

	buf_free(&dev->buf);
	ctx_zero(ctx);
	return ret;
}

static int
ek270_debug(struct device *dev, const char *arg, int *save)
{
	const char *regstr = "\002([^\003]+)\003|\x06|\x15";
	struct ek270_ctx *ctx;
	char cmd[3], addr[1024];
	int ret;

	ctx = get_spec(dev);
	ret = -1;

	if (2 != sscanf(arg, "%2s,%1023s", cmd, addr))
		goto out;

	if (-1 == make_query(dev, cmd, addr))
		goto out;

	if (-1 == set_ctx_regex(ctx, regstr))
		goto free_query;

	if (-1 == dev_query(dev))
		goto free_regex;

	dump_msg("EK270 DEBUG", dev->buf.p, dev->buf.len);
	dump_as_ascii(dev->buf.p, dev->buf.len);

	*save = 0;
	ret = 0;

free_regex:
	regfree(&ctx->re);
free_query:
	free(ctx->send_query);
out:
	buf_free(&dev->buf);
	ctx_zero(ctx);
	return ret;
}

static int
ek270_read_partial(struct device *dev, const char *addr, struct buf *save_buf)
{
	struct ek270_ctx *ctx;
	void *fnsave;
	char *query;
	char **ls;
	int nls;
	int ret;

	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = ek270_parse_crc_msg;

	ctx = get_spec(dev);
	ctx->crc_flag = 0;
	ret = -1;

	if (-1 == set_ctx_regex(ctx, "\002([^\003\004]+)(\003|\004)"))
		goto out;

	if (-1 == make_query(dev, "R3", addr))
		goto free_regex;

	query = ctx->send_query; /* save query to free */

	if (-1 == dev_query(dev))
		goto free_query;

	ctx->send_query = "\006";
	ctx->send_query_sz = 1;

	do {
		ret = -1;

		nls = regex_split_raw(&ctx->re, dev->buf.p, &ls);
		if (-1 == nls)
			break;
		assert(nls == 3);

		if (-1 == buf_strcat(save_buf, ls[1]) ||
		    -1 == buf_strcat(save_buf, "\r\n")) {
			set_system_error("buf_strcat");
			break;
		}
		buf_flush(&dev->buf, strlen(ls[0]) + 1);

		if ('\003' == ls[2][0]) {
			ret = 0;
			break;
		}

		free_strings(ls, nls);
		free(ls);

		ls = NULL;
		nls = 0;

		ret = dev_query(dev);
	} while (0 == ret);

	if (nls) {
		free_strings(ls, nls);
		free(ls);
	}

free_query:
	free(query);
free_regex:
	regfree(&ctx->re);
out:
	dev->opers->parse_msg = fnsave;

	buf_free(&dev->buf);
	ctx_zero(ctx);
	return ret;
}

static void
ek270_close_session(struct device *dev)
{
	const char msg[] = "\001B0\003\x71";

	dev_write(dev, msg, sizeof(msg) - 1);
	dev_write(dev, msg, sizeof(msg) - 1);
}

/*
 * Interface
 */
static struct vk_operations ek270_opers = {
	.init	= ek270_init,
	.free	= ek270_free,
	.get	= ek270_get,
	.set	= ek270_set,
	.debug	= ek270_debug,
	.date	= ek270_date,

	.send_msg  = ek270_send_msg,
	.parse_msg = ek270_parse_msg,
	.check_crc = ek270_check_crc,

	.h_archiv = ek270_h_archiv,
	.m_archiv = ek270_m_archiv,
	.d_archiv = ek270_d_archiv,

	.events = ek270_events
};

void
ek270_get_operations(struct vk_operations **p_opers)
{
	*p_opers = &ek270_opers;
}

/* */
static int
ek270_init(struct device *dev)
{
	struct ek270_ctx *spec;
	int ret;

	spec = calloc(1, sizeof(*spec));

	if (!spec) {
		set_system_error("calloc");
		ret = -1;
	} else {
		dev->spec = spec;

		ret =   (-1 == reset_session(dev)) ||
			(-1 == ek270_get_ident(dev)) ||
			(-1 == ek270_do_negotiation(dev)) ||
			(-1 == ek270_authorize(dev));

		if (!ret) {
			ret = 0;
		} else {
			free(spec);
			dev->spec = NULL;
			ret = -1;
		}
	}
	return ret;
}

static void
ek270_free(struct device *dev)
{
	struct ek270_ctx *spec = get_spec(dev);

	ek270_close_session(dev);

	free(spec);
	dev->spec = NULL;
}

static int
ek270_get(struct device *dev, int param, char **ret)
{
	static struct {
		int paramno;
		const char *addr;
	} addrs[] = {
		{12,	"2:310.0(1)"},
		{14,	"5:310.0(1)"},
		{15,	"4:301.0(1)"},
		{16,	"4:310.0(1)"},
		{18,	"7:310.0(1)"},
		{20,	"6:310_1.0(1)"},
		{21,	"2:302.0(1)"},
		{22,	"2:301.0(1)"},
		{23,	"4:302.0(1)"},
		{25,	"13:314_1.0(1)"},
		{26,	"8:310.0(1)"},
		{28,	"6:212_1.0(1)"},
		{106,	"7:311.0(1)"},
		{111,	"8:311.0(1)"}
	};
	int i, rv;

	for (i = 0; i < ARRAY_SIZE(addrs); ++i) {
		if (param == addrs[i].paramno) {
			rv = ek270_get_float_param(dev, addrs[i].addr, ret);
			break;
		}
	}
	if (i == ARRAY_SIZE(addrs)) {
		set_eval_error("No such parameter (%d)", param);
		rv = -1;
	}

	return rv;
}

static int
ek270_setp(struct device *dev, const char *addr, const char *newval, char **ret)
{
	static const char *regstr = "\006|\025|\002([^\003]+)\003";

	struct ek270_ctx *ctx;
	struct buf *buf;
	int rv;

	ctx = get_spec(dev);
	buf = &(dev->buf);
	rv = -1;

	if (-1 == set_ctx_regex(ctx, regstr))
		goto out;

	if (-1 == get_query(dev, "W1", "%s(%s)", addr, newval))
		goto free_regex;

	if (-1 == dev_query(dev))
		goto free_query;

	if ('\006' == buf->p[0]) {
		*ret = strdup("OK");
	}
	else if ('\025' == buf->p[0]) {
		*ret = strdup("REJECTED");
	}
	else {
		buf->p[buf->len - 1] = '\0'; /* remove \003 symbol */
		*ret = strdup(buf->p + 1);
	}

	if (NULL == *ret) {
		set_system_error("strdup");
		goto free_query;
	}

	rv = 0;

free_query:
	free(ctx->send_query);
free_regex:
	regfree(&ctx->re);
out:
	buf_free(buf);
	ctx_zero(ctx);
	return rv;
}

static int
ek270_set(struct device *dev, int nparam, char *newval, char **ret)
{
	/* */
	static struct {
		int nparam;
		const char *addr;
	} params[] = {
		{106,	"7:311"},
		{111,	"8:311"}
	};

	/* */
	int i, rv;

	for (i = 0, rv = -1; i < ARRAY_SIZE(params); ++i) {
		if (nparam == params[i].nparam) {
			rv = ek270_setp(dev, params[i].addr, newval, ret);
			break;
		}
	}
	if (ARRAY_SIZE(params) == i) {
		set_eval_error("No such parameter (%d)", nparam);
	}
	return rv;
}

/* 
static int
dump_to_file(const char *path, const void *p, int psz)
{
	int fd;

	fd = open(path, O_WRONLY | O_APPEND | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd != -1) {
		write(fd, p, psz);
		close(fd);
		fd = 0;
	}
	return fd;
}
*/

static int
make_hour_rec(const char *str, struct archive **save)
{
	struct archive *arch;
	int ret, gnum, lnum;
	int years, months, days, hours, minutes, seconds;
	float VbT, Vb, V, Vo;
	float P_MP, T_MP;
	float K_MP, C_MP;

	ret = sscanf(str,
		"(%d)(%d)(%d-%d-%d,%d:%d:%d)"
		"(%f)(%f)(%f)(%f)"
		"(%f)(%f)(%f)(%f)",
		&gnum, &lnum, &years, &months, &days, &hours, &minutes, &seconds,
		&Vb, &VbT, &V, &Vo,
		&P_MP, &T_MP, &K_MP, &C_MP
	);
	if (16 == ret && (NULL != (arch = alloc_archive()))) {
		arch->num = lnum;
		snprintf(arch->datetime, sizeof(arch->datetime),
				"%d-%.2d-%.2d,%.2d:%.2d:%.2d",
				years, months, days,
				hours, minutes, seconds);
		arch->params[ARCH_VNORM] 	= Vb;
		arch->params[ARCH_VSUBS_NORM]	= VbT;
		arch->params[ARCH_VWORK]	= V;
		arch->params[ARCH_VSUBS_WORK]	= Vo;
		arch->params[ARCH_TAVG]		= T_MP;
		arch->params[ARCH_PAVG]		= P_MP;
		arch->params[ARCH_KAVG]		= C_MP;
		*save = arch;
		ret = 0;
	} else {
		ret = -1;
	}
	return ret;
}

static int
make_archive(struct device *dev, struct buf *buf,
		int (*fn)(const char *, struct archive **),
		struct archive **save)
{
	struct archive *archives, **aptr;
	char **ls, **recs;
	int nls, nrecs, i;
	int ret;

	recs = ls = NULL;
	nrecs = nls = 0;
	ret = -1;

	nrecs = rec_regex_split("[^\r\n]+", buf->p, &recs);
	if (nrecs <= 0)
		goto out;

	for (archives = NULL, aptr = &archives, i = 0; i < nrecs; ++i) {
		ret = fn(recs[i], aptr);
		if (-1 == ret) {
			free_archive(archives);
			break;
		}

		aptr = &((*aptr)->next);
	}
	if (!dev->quiet)
		fprintf(stderr, "RECS: %d\n", nrecs);

	free_strings(recs, nrecs);
	free(recs);

	if (0 == ret)
		*save = archives;
out:
	return ret;
}

static int
ek270_h_archiv(struct device *dev, int no, const char *from, const char *to,
		struct archive **save)
{
	struct buf buf;
	char addr[1024], buf_from[256], buf_to[256];
	int ret;

	buf_init(&buf);
	buf_free(&dev->buf);
	ret = -1;

	buf_from[0] = buf_to[0] = '\0';

	if (*from && -1 == datef(buf_from, sizeof(buf_from), "%Y-%m-%d,%H:%M:%S",from))
		goto out;
	if (*to && -1 == datef(buf_to, sizeof(buf_to), "%Y-%m-%d,%H:%M:%S", to))
		goto out;

	snprintf(addr, sizeof(addr), "%d:V.0(3;%s;%s;10)", 3, buf_from, buf_to);

	ret = ek270_read_partial(dev, addr, &buf);
	if (0 == ret)
		ret = make_archive(dev, &buf, make_hour_rec, save);
out:
	buf_free(&buf);
	return ret;
}

static int
make_month_rec(const char *str, struct archive **save)
{
	int gnum, lnum, year, month, day, hour, minute, second;
	float Vb, VbT, VbMPmax, VbDymax, V, Vo, VMPmax, VDymax;
	struct archive *arch;
	int unused, ret;

	ret = sscanf(str, "(%d)(%d)(%d-%d-%d,%d:%d:%d)"
			"(%f)(%f)(%f)(%d-%d-%d,%d:%d:%d)(%d)"
			"(%f)(%d-%d-%d,%d:%d:%d)(%d)"
			"(%f)(%f)(%f)(%d-%d-%d,%d:%d:%d)(%d)"
			"(%f)",
			&gnum, &lnum, &year, &month, &day,
			&hour, &minute, &second,
			&Vb, &VbT, &VbMPmax,
			&unused, &unused, &unused,
			&unused, &unused, &unused, &unused,
			&VbDymax, &unused, &unused, &unused,
			&unused, &unused, &unused, &unused,
			&V, &Vo, &VMPmax, &unused, &unused, &unused,
			&unused, &unused, &unused, &unused,
			&VDymax);
	if (37 == ret && (arch = alloc_archive())) {
		arch->num	= lnum;
		snprintf(arch->datetime, sizeof(arch->datetime),
				"%d-%d-%d,%.2d:%.2d:%.2d",
				year, month, day, hour, minute, second);
		arch->params[ARCH_VNORM]	= Vb;
		arch->params[ARCH_VSUBS_NORM]	= VbT;
		arch->params[ARCH_VWORK]	= V;
		arch->params[ARCH_VSUBS_WORK]	= Vo;
		arch->next			= NULL;
		*save = arch;
		ret = 0;
	} else {
		ret = -1;
	}
	return ret;
}

static int
update_month_rec(const char *str, struct archive *arch)
{
	float Qbmax, Qbmin, Qmax, Qmin, pMon, pMonmax, pMonmin;
	float tMon, tMonmax, tMonmin, kMon, cMon;
	char **ls;
	int nls, ret;

	ret = -1;
	ls  = NULL;
	nls = 0;

	nls = rec_regex_split("\\([^()]+\\)", str, &ls);
	if (nls <= 0)
		goto out;
	if (nls != 36)
		goto free_ls;

	sscanf(ls[3], "(%f)", &Qbmax);
	sscanf(ls[6], "(%f)", &Qbmin);
	sscanf(ls[9], "(%f)", &Qmax);
	sscanf(ls[12],"(%f)", &Qmin);
	sscanf(ls[15],"(%f)", &pMon);
	sscanf(ls[16],"(%f)", &pMonmax);
	sscanf(ls[19],"(%f)", &pMonmin);
	sscanf(ls[22],"(%f)", &tMon);
	sscanf(ls[23],"(%f)", &tMonmax);
	sscanf(ls[26],"(%f)", &tMonmin);
	sscanf(ls[29],"(%f)", &kMon);
	sscanf(ls[30],"(%f)", &cMon);

	arch->params[ARCH_KAVG]	= cMon;
	arch->params[ARCH_TAVG]	= tMon;
	arch->params[ARCH_PAVG] = pMon;

	ret = 0;

free_ls:
	free_strings(ls, nls);
	free(ls);
out:
	return ret;
}

static int
update_archive(struct device *dev, struct buf *buf, struct archive *arch)
{
	char **ls;
	int nls, i, ret;

	ret = -1;

	nls = rec_regex_split("[^\r\n]+", buf->p, &ls);
	if (nls <= 0)
		goto out;

	for (i = 0; i < nls && arch; ++i) {
		update_month_rec(ls[i], arch);
		arch = arch->next;
	}

	ret = 0;

	free_strings(ls, nls);
	free(ls);
out:
	return ret;
}

static int
ek270_m_archiv(struct device *dev, int no, const char *from, const char *to,
		struct archive **save)
{
	struct buf buf;
	char addr[1024], buf_from[256], buf_to[256];
	int ret;

	buf_init(&buf);
	buf_free(&dev->buf);
	ret = -1;

	buf_from[0] = buf_to[0] = '\0';

	if (*from && -1 == datef(buf_from, sizeof(buf_from), "%Y-%m-%d,%H:%M:%S",from))
		goto out;
	if (*to && -1 == datef(buf_to, sizeof(buf_to), "%Y-%m-%d,%H:%M:%S", to))
		goto out;

	snprintf(addr, sizeof(addr), "%d:V.0(3;%s;%s;10)", 1, buf_from, buf_to);
	ret = ek270_read_partial(dev, addr, &buf);
	if (-1 == ret)
		goto out;

	ret = make_archive(dev, &buf, make_month_rec, save);
	if (-1 == ret)
		goto out;

	buf_free(&buf);

	snprintf(addr, sizeof(addr), "%d:V.0(3;%s;%s;10)", 2, buf_from, buf_to);
	ret = ek270_read_partial(dev, addr, &buf);
	if (-1 == ret) {
		free_archive(*save);
		*save = NULL;
		goto out;
	}

	ret = update_archive(dev, &buf, *save);
	if (-1 == ret) {
		free_archive(*save);
		*save = NULL;
	}
out:
	buf_free(&buf);
	return ret;

}

static int
ek270_d_archiv(struct device *dev, int no, const char *from, const char *to,
		struct archive **save)
{
	struct buf buf;
	char addr[1024], buf_from[256], buf_to[256];
	int ret;

	buf_init(&buf);
	buf_free(&dev->buf);
	ret = -1;

	buf_from[0] = buf_to[0] = '\0';

	if (*from && -1 == datef(buf_from, sizeof(buf_from), "%Y-%m-%d,%H:%M:%S",from))
		goto out;
	if (*to && -1 == datef(buf_to, sizeof(buf_to), "%Y-%m-%d,%H:%M:%S", to))
		goto out;

	snprintf(addr, sizeof(addr), "%d:V.0(3;%s;%s;10)", 7, buf_from, buf_to);

	ret = ek270_read_partial(dev, addr, &buf);
	if (0 == ret)
		ret = make_archive(dev, &buf, make_hour_rec, save);
out:
	buf_free(&buf);
	return ret;
}

static int
str2event(const char *str, struct events *ev)
{
	int event, year, month, day, hour, minute, second;
	int gnum, lnum;
	int ret;

	ret = sscanf(str, "(%d)(%d)(%d-%d-%d,%d:%d:%d)(%x)",
			&gnum, &lnum, &year, &month, &day,
			&hour, &minute, &second, &event);
	if (9 == ret) {
		ev->num	= lnum;
		ev->event = event;
		snprintf(ev->datetime, sizeof(ev->datetime),
			"%d-%d-%d,%.2d:%.2d:%.2d",
			year, month, day, hour, minute, second);
		ret = 0;
	} else {
		ret = -1;
	}
	return ret;
}

static int
conv_event(const char *str, struct events **pev)
{
	struct events *ev;
	int ret;

	ev = malloc(sizeof(*ev));
	if (!ev) {
		set_system_error("malloc");
		ret = -1;
	} else if (-1 == str2event(str, ev)) {
		ret = -1;
	} else {
		ev->next = NULL;
		*pev = ev;
		ret = 0;
	}
	return ret;
}

static int
make_events(struct device *dev, struct buf *buf, struct events **save)
{
	struct events *top, **pev;
	char **ls;
	int nls, ret, i;

	top = NULL;
	pev = &top;
	ls = NULL;
	nls = 0;
	ret = -1;

	nls = rec_regex_split("[^\r\n]+", buf->p, &ls);
	if (nls <= 0)
		goto out;

	for (i = 0; i < nls; ++i) {
		ret = conv_event(ls[i], pev);
		if (-1 == ret) {
			free_events(top);
			break;
		}
		pev = &((*pev)->next);
	}

	free_strings(ls, nls);
	free(ls);

	if (0 == ret)
		*save = top;
out:
	return ret;
}

static int
ek270_events(struct device *dev, const char *from, const char *to,
		struct events **save)
{
	struct buf buf;
	char addr[1024], buf_from[256], buf_to[256];
	int ret;

	buf_init(&buf);
	buf_free(&dev->buf);
	ret = -1;

	buf_from[0] = buf_to[0] = '\0';

	if (*from && -1 == datef(buf_from, sizeof(buf_from), "%Y-%m-%d,%H:%M:%S",from))
		goto out;
	if (*to && -1 == datef(buf_to, sizeof(buf_to), "%Y-%m-%d,%H:%M:%S", to))
		goto out;

	snprintf(addr, sizeof(addr), "%d:V.0(3;%s;%s;10)", 4, buf_from, buf_to);

	ret = ek270_read_partial(dev, addr, &buf);
	if (0 == ret)
		ret = make_events(dev, &buf, save);
out:
	buf_free(&buf);
	return ret;

}

static int
ek270_date(struct device *dev, const char *newval, char **ret)
{
	int rv;
	
	if (*newval == '\0')
		rv = ek270_get_date(dev, ret);
	else
		rv = ek270_set_date(dev, newval, ret);

	return rv;
}

static int
ek270_set_date(struct device *dev, const char *newval, char **ret)
{
	struct ek270_ctx *ctx;
	struct buf *buf;
	char *rval;
	int rv;

	buf = &dev->buf;
	ctx = get_spec(dev);
	rv = -1;

	if (-1 == set_ctx_regex(ctx, "\006|\025|\002([^\003]+)\003"))
		goto out;

	if (-1 == get_query(dev, "W1", "1:0400(%s)", newval))
		goto free_regex;

	if (-1 == dev_query(dev))
		goto free_query;

	if ('\006' == buf->p[0])
		rval = strdup("OK");
	else if ('\025' == buf->p[0])
		rval = strdup("REJECTED");
	else {
		buf->p[buf->len - 1] = '\0'; /* remove \003 */
		set_eval_error(buf->p + 1);
		rval = NULL;
	}

	if (NULL != rval) {
		*ret = rval;
		rv = 0;
	}

free_query:
	free(ctx->send_query);

free_regex:
	regfree(&ctx->re);

out:
	buf_free(buf);
	ctx_zero(ctx);
	return rv;
}

static int
ek270_get_date(struct device *dev, char **ret)
{
	static const char reg[] =
		"\002[^()]+[(]"
		"[0-9]{4}-[0-9]{1,2}-[0-9]{1,2},[0-9]{1,2}:[0-9]{1,2}:[0-9]{1,2}"
		"[)]\003";
	struct ek270_ctx *ctx;
	char *data;
	int ys, mos, ds, hs, mis, ss;
	int rv;

	rv = -1;
	ctx= get_spec(dev);

	if (-1 == get_query(dev, "R1", "%s.0(1)", "1:0400"))
		goto out;

	if (-1 == set_ctx_regex(ctx, reg))
		goto free_query;

	if (-1 == dev_query(dev))
		goto free_regex;

	data = strchr(dev->buf.p, '(');
	sscanf(data, "(%d-%d-%d,%d:%d:%d)", &ys, &mos, &ds, &hs, &mis, &ss);

	data = dynsprintf(
		"%.4d-%.02d-%.02d %.02d:%.02d:%.02d", ys, mos, ds, hs, mis, ss
	);
	if (NULL == data)
		goto free_regex;

	*ret = data;
	rv = 0;

free_regex:
	regfree(&ctx->re);

free_query:
	free(ctx->send_query);
out:
	buf_free(&dev->buf);
	ctx_zero(ctx);
	return rv;
}

static int
ek270_send_msg(struct device *dev)
{
	struct ek270_ctx *ctx = get_spec(dev);

	return dev_write(dev, ctx->send_query, ctx->send_query_sz);
}

static int
ek270_parse_msg(struct device *dev, char *p, int psz)
{
	struct ek270_ctx *ctx;
	regmatch_t matches[1];
	int match, ret;

	ctx = get_spec(dev);

	if (!dev->quiet)
		fprintf(stderr, "\rParsing: %10d bytes", psz);

	match = regexec(&ctx->re, p, ARRAY_SIZE(matches), matches, 0);
	if (match == 0) {
		if (!dev->quiet)
			fprintf(stderr, "\nParsing ok\n");
		ret = 0;
	} else {
		ret = -1;
	}
	return ret;
}

static int
ek270_parse_crc_msg(struct device *dev, char *p, int psz)
{
	struct ek270_ctx *ctx;
	regmatch_t matches[1];
	int match;
	int ret;

	ctx = get_spec(dev);

	if (!dev->quiet)
		fprintf(stderr, "\rParsing: %10d bytes", psz);

	match = regexec(&ctx->re, p, ARRAY_SIZE(matches), matches, 0);
	if (match != 0 || psz != matches[0].rm_eo - matches[0].rm_so + 1) {
		ret = -1;
	} else {
		if (!dev->quiet)
			fprintf(stderr, "\nParsing ok\n");

		ctx->crc = ((uint8_t *)p)[psz - 1];
		ret = 0;
	}
	return ret;
}

static int
ek270_check_crc(struct device *dev, char *p, int psz)
{
	struct ek270_ctx *ctx;
	uint8_t crc;
	int ret;

	ctx = get_spec(dev);

	if (ctx->crc_flag) {
		assert(psz > 2);
		crc = calc_bcc(p + 1, psz - 2);
		ret = (crc == ctx->crc);
	} else {
		ret = 0;
	}
	return ret;
}

