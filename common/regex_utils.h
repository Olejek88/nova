#ifndef _REGEX_H_
#define _REGEX_H_

#include <regex.h>

int regex_split_raw(regex_t *, const char *, char ***);
int regex_split(const char *, const char *, char ***);
int rec_regex_split(const char *, const char *, char ***);

#endif/*_REGEX_H_*/
