#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 200112L

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "errors.h"

void
free_strings(char **ls, int nls)
{
	int i;

	for (i = 0; i < nls; ++i) {
		free(ls[i]);
		ls[i] = NULL;
	}
}

char *
vdynsprintf(const char *fmt, va_list ap)
{
	va_list aq;
	int sz, n;
	char *p, *s;

	sz = 128;
	p = malloc(sz);
	if (!p) {
		set_system_error("malloc");
		return NULL;
	}
	
	for (;;) {
		va_copy(aq, ap);
		n = vsnprintf(p, sz, fmt, ap);
		va_end(aq);

		if (n > -1 && n < sz)
			break;

		if (n > -1) /* glibc 2.1 */
			sz = n + 1;
		else /* glibc 2.0 */
			sz = 2 * sz;

		s = realloc(p, sz);
		if (!s) {
			set_system_error("realloc");
			free(p);
			p = NULL;
			break;
		}
		p = s;
	}
	return p;
}

char *
dynsprintf(const char *fmt, ...)
{
	va_list ap;
	char *ret;

	va_start(ap, fmt);
	ret = vdynsprintf(fmt, ap);
	va_end(ap);

	return ret;
}


void *
vdynpack(int n, va_list aq) 
{
	va_list ap;
	uint8_t *p;
	int i;

	p = malloc(n);
	if (p) {
		va_copy(ap, aq);
		for (i = 0; i < n; ++i)
			p[i] = (va_arg(ap, int) & 0xFF);
		va_end(ap);
	} else {
		set_system_error("malloc");
	}
	return p;
}

void *
dynpack(int n, ...)
{
	va_list ap;
	void *p;

	va_start(ap, n);
	p = vdynpack(n, ap);
	va_end(ap);

	return p;
}

uint8_t
calc_crc(void *ptr, int n)
{
	uint8_t *b;
	uint8_t crc;
	int i;

	for (i = 0, b = ptr, crc = 0; i < n; ++i)
		crc += b[i];

	return crc;
}

float
ntof(void *p)
{
	uint8_t *s, *f;
	float ret;
	int i;

	for (i = 0, s = p, f = (uint8_t *)&ret; i < 4; ++i)
		f[i] = (s[i] & 0xFF);
	return ret;
}

int
datef(void *buf, int sz, const char *fmt, const char *src)
{
	struct tm tm;
	char *s;
	int ret;

	memset(&tm, 0, sizeof(tm));
	s = strptime(src, "%Y-%m-%d,%H:%M:%S", &tm);
	if (s && '\0' == *s) {
		strftime(buf, sz, fmt, &tm);
		ret = 0;
	} else {
		ret = -1;
	}
	return ret;
}

void
dump_as_ascii(const char *s, int sz)
{
	int i;

	for (i = 0; i < sz; ++i) {
		if (isprint(s[i]) && '\n' != s[i] && '\r' != s[i])
			fprintf(stderr, "%c", s[i]);
		else
			fprintf(stderr, "?");
	}
	fprintf(stderr, "\n");
}

