#include <sys/types.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "errors.h"

static int
make_matches(const char *haystack, regmatch_t *matches, int nmatches, char **ls)
{
	char *s;
	int i, sz, ret;

	for (i = 0; i < nmatches; ++i) {
		sz = matches[i].rm_eo - matches[i].rm_so;
		s = malloc(sz + 1);
		if (!s) {
			set_system_error("malloc");
			break;
		}
		strncpy(s, haystack + matches[i].rm_so, sz);
		s[sz] = '\0';

		ls[i] = s;
	}
	if (i != nmatches) { /* error occured */
		free_strings(ls, i);
		ret = -1;
	} else {
		ret = 0;
	}
	return ret;
}

int
regex_split_raw(regex_t *re, const char *haystack, char ***ls)
{
	regmatch_t matches[128];
	char **ss;
	int cc;

	if (0 != regexec(re, haystack, ARRAY_SIZE(matches), matches, 0))
		return 0;

	for (cc = 0; matches[cc].rm_so != -1; ++cc)
		;

	if (cc > 0) {
		ss = malloc(sizeof(*ss) * cc);
		if (!ss) {
			set_system_error("malloc");
			return -1;
		}
		if (-1 == make_matches(haystack, matches, cc, ss)) {
			free(ss);
			return -1;
		}
	} else {
		ss = NULL;
	}
	*ls  = ss;
	return cc;
}

int
regex_split(const char *re_str, const char *haystack, char ***ls)
{
	regex_t re;
	int ret;

	ret = regcomp(&re, re_str, REG_EXTENDED);
	if (0 == ret) {
		ret = regex_split_raw(&re, haystack, ls);
		regfree(&re);
	} else {
		set_regex_error(ret, &re);
		ret = -1;
	}
	return ret;
}

/* */
int
rec_regex_split(const char *re_str, const char *haystack, char ***save)
{
	regmatch_t matches[1];
	regex_t re;
	char **ls, *str;
	int nls, nsz, strsz;

	ls = NULL;
	nsz = nls = 0;

	nls = regcomp(&re, re_str, REG_EXTENDED);
	if (0 != nls) {
		set_regex_error(nls, &re);
		goto fail;
	}

	for (;;) {
		if (0 != regexec(&re, haystack, ARRAY_SIZE(matches), matches, 0))
			break;

		if (nsz == nls) {
			void *tmp;

			nsz = (nsz) ? 2 * nsz : 32;
			tmp = realloc(ls, sizeof(*ls) * nsz);
			if (!tmp) {
				set_system_error("realloc");
				goto regex_free;
			}
			ls = tmp;
		}

		strsz = matches[0].rm_eo - matches[0].rm_so;
		str = malloc(strsz + 1);
		if (!str) {
			set_system_error("malloc");
			goto regex_free;
		}

		strncpy(str, haystack + matches[0].rm_so, strsz);
		str[strsz] = '\0';

		ls[nls++] = str;

		haystack += matches[0].rm_eo;
	}

	regfree(&re);
	*save = ls;
	return nls;
regex_free:
	regfree(&re);
fail:
	free_strings(ls, nls);
	free(ls);
	return -1;
}

