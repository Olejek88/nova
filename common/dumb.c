#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "device.h"
#include "errors.h"

static int  dumb_init(struct device *);
static void dumb_free(struct device *);
static int  dumb_get(struct device *, int, char **);
static int  dumb_check_msg(struct device *, char *, int);
static int  dumb_check_crc(struct device *, char *, int);
static int  dumb_send_msg(struct device *);

static int  dumb_h_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  dumb_m_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  dumb_d_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  dumb_events(struct device *, const char *, const char *,
				struct events **);

static struct vk_operations opers = {
	.init = dumb_init,
	.free = dumb_free,
	.check_crc = dumb_check_crc,
	.parse_msg = dumb_check_msg,
	.send_msg = dumb_send_msg,

	.get  = dumb_get,
	.h_archiv = dumb_h_archiv,
	.m_archiv = dumb_m_archiv,
	.d_archiv = dumb_d_archiv,
	.events = dumb_events
};

/* */
static int
dumb_init(struct device *dev)
{
	return 0;
}

static void
dumb_free(struct device *dev)
{
}

static int
dumb_get(struct device *dev, int param, char **ret)
{
	char *s = strdup("3.14");
	int rv = -1;

	if (s) {
		*ret = s;
		rv = 0;
	}
	return rv;
}

static int
dumb_check_crc(struct device *dev, char *p, int psz)
{
	return 0;
}

static int
dumb_check_msg(struct device *dev, char *p, int psz)
{
	return 0;
}

static int
dumb_send_msg(struct device *dev)
{
	return 0;
}

static int
dumb_h_archiv(struct device *dev, int n, const char *from, const char *to,
		struct archive **save)
{
	struct archive *arch;
	time_t t;
	int i;

	arch = alloc_archive();
	if (!arch)
		return -1;

	for (i = 0; i < ARCH_MAX_NPARAM - 10; ++i)
		arch->params[i] = 2.718981898;

	t = time(NULL);
	strftime(arch->datetime, sizeof(arch->datetime),
			"%Y-%m-%d,%H:%M:%S", localtime(&t)
	);

	*save = arch;
	return 0;
}

static int
dumb_m_archiv(struct device *dev, int n, const char *from, const char *to,
		struct archive **save)
{
	struct archive *arch;
	time_t t;
	int i;

	arch = alloc_archive();
	if (!arch)
		return -1;

	for (i = 0; i < ARCH_MAX_NPARAM - 10; ++i)
		arch->params[i] = 3.718981898777;

	t = time(NULL);
	strftime(arch->datetime, sizeof(arch->datetime),
			"%Y-%m-%d,00:00:00", localtime(&t)
	);

	*save = arch;
	return 0;
}

static int
dumb_d_archiv(struct device *dev, int n, const char *from, const char *to,
		struct archive **save)
{
	return -1;
}

static int
dumb_events(struct device *dev, const char *from, const char *to,
		struct events **save)
{
	struct events *ev;

	ev = malloc(sizeof(*ev));
	if (!ev)
		return -1;

	ev->num = 1;
	ev->event = 0x10;
	ev->next = NULL;
	snprintf(ev->datetime, sizeof(ev->datetime), "%s", from);
	*save = ev;
	return 0;
}

/* */
void
dumb_get_operations(struct vk_operations **save)
{
	*save = &opers;
}


