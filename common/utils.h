#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdint.h>
#include <stdarg.h>

/* */
#define ARRAY_SIZE(x)	(sizeof(x) / sizeof(*(x)))

/* */

void * dynpack(int, ...);
void * vdynpack(int, va_list);

uint8_t calc_crc(void *, int);
float ntof(void *);

void free_strings(char **, int);

char * vdynsprintf(const char *, va_list ap);
char * dynsprintf(const char *, ...);

int  datef(void *, int, const char *, const char *);

void dump_as_ascii(const char *, int);

#endif/*_UTILS_H_*/
