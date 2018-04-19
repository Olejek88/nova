#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "parser.h"
#include "errors.h"
#include "buf.h"


static struct token *
make_token(int type)
{
	struct token *p = malloc(sizeof(*p));

	if (p) {
		p->type = type;
		p->next = NULL;
	} else {
		set_system_error("make_token");
	}
	return p;
}

static int
parse_string(const char **expr, struct token **save)
{
	struct token *tok;
	struct buf buf;
	const char *s;
	char ch;

	buf_init(&buf);

	for (s = *expr + 1; '"' != *s; ++s) {
		if ('\0' == *s)
			goto fail;
		if ('\\' == *s) {
			if ('n' == s[1])
				ch = '\n';
			else if ('r' == s[1])
				ch = '\r';
			else if ('t' == s[1])
				ch = '\t';
			else if ('"' == s[1])
				ch = '"';
			else
				ch = '\\', --s;
			++s;
		} else {
			ch = *s;
		}
		if (-1 == buf_putchar(&buf, ch)) {
			set_system_error("buf_putchar");
			goto fail;
		}
	}

	tok = make_token(TOKEN_STRING);
	if (NULL == tok)
		goto fail;

	tok->val.s = buf_detach(&buf);
	if (tok->val.s == NULL) { /* empty string */
		tok->val.s = strdup("");
		if (NULL == tok->val.s)
			goto fail;
	}
	*save = tok;
	*expr = s + 1;
	return 0;
fail:
	buf_free(&buf);
	return -1;
}

static int
parse_octal(const char **expr, struct token **save)
{
	struct token *tok;
	struct buf buf;
	const char *s;

	buf_init(&buf);

	for (s = *expr; isdigit(*s); ++s) {
		if ('8' == *s || '9' == *s)
			goto fail;
		if (-1 == buf_putchar(&buf, *s)) {
			set_system_error("buf_putchar");
			goto fail;
		}
	}

	tok = make_token(TOKEN_INT);
	if (NULL == tok)
		goto fail;

	sscanf(buf.p, "%o", &tok->val.i);
	buf_free(&buf);
	*save = tok;
	*expr = s;
	return 0;
fail:
	buf_free(&buf);
	return -1;
}

static int
parse_hex(const char **expr, struct token **save)
{
	struct token *tok;
	struct buf buf;
	const char *s;

	s = *expr + 2; /* skip "0x" */
	buf_init(&buf);
	if (-1 == buf_strcat(&buf, "0x")) {
		set_system_error("buf_strcat");
		goto fail;
	}

	while (isxdigit(*s)) {
		if (-1 == buf_putchar(&buf, *s)) {
			set_system_error("buf_putchar");
			goto fail;
		}
		++s;
	}

	tok = make_token(TOKEN_INT);
	if (NULL == tok)
		goto fail;

	sscanf(buf.p, "%x", &tok->val.i);
	buf_free(&buf);
	*save = tok;
	*expr = s;

	return 0;
fail:
	buf_free(&buf);
	return -1;
}

static int
parse_decimal(const char **expr, struct token **save)
{
	struct token *tok;
	struct buf buf;
	const char *s;
	int type;

	buf_init(&buf);
	type = TOKEN_INT;
	s = *expr;

	for (;;) {
		if ('.' == *s && type == TOKEN_FLOAT)
			goto fail;
		if ('.' == *s && type == TOKEN_INT) {
			type = TOKEN_FLOAT;
		} else if (!isdigit(*s)) {
			break;
		}
		if (-1 == buf_putchar(&buf, *s)) {
			set_system_error("buf_putchar");
			goto fail;
		}
		++s;
	}

	tok = make_token(type);
	if (NULL == tok)
		goto fail;

	if (tok->type == TOKEN_INT)
		sscanf(buf.p, "%d", &tok->val.i);
	else
		sscanf(buf.p, "%f", &tok->val.f);
	buf_free(&buf);
	*save = tok;
	*expr = s;
	return 0;
fail:
	buf_free(&buf);
	return -1;
}

static int
parse_number(const char **expr, struct token **save)
{
	const char *s;
	int ret;

	s = *expr;
	if ('0' != *s || '.' == s[1])
		ret = parse_decimal(expr, save);
	else if ('x' == s[1])
		ret = parse_hex(expr, save);
	else
		ret = parse_octal(expr, save);

	return ret;
}

static int
parse_symbol(const char **expr, struct token **save)
{
	struct token *tok;
	struct buf buf;
	const char *s;

	buf_init(&buf);

	for (s = *expr; isalnum(*s) || '_' == *s; ++s) {
		if (-1 == buf_putchar(&buf, *s)) {
			set_system_error("buf_putchar");
			goto fail;
		}
	}

	tok = make_token(TOKEN_SYMBOL);
	if (NULL == tok)
		goto fail;

	if (strcmp("null", buf.p) == 0) {
		tok->type = TOKEN_NULL;
		buf_free(&buf);
	} else {
		tok->val.s = buf_detach(&buf);
	}

	*expr = s;
	*save = tok;
	return 0;
fail:
	buf_free(&buf);
	return -1;
}

static int
parse_opers(const char **expr, struct token **save)
{
	struct token *tok;
	int ret;

	tok = make_token(**expr);
	if (tok) {
		tok->val.s = NULL;
		*expr = *expr + 1;
		*save = tok;
		ret = 0;
	} else {
		ret = -1;
	}
	return ret;
}

int
parse_expr(const char *expr, struct token **save)
{
	struct token *tok = NULL, **ptok = &tok;
	int ret;

	for (;;) {
		while (isspace(*expr))
			++expr;
		if ('\0' == *expr)
			break;

		if (isdigit(*expr))
			ret = parse_number(&expr, ptok);
		else if ('"' == *expr)
			ret = parse_string(&expr, ptok);
		else if (isalpha(*expr))
			ret = parse_symbol(&expr, ptok);
		else if (strchr("+-*/[](),", *expr))
			ret = parse_opers(&expr, ptok);
		else
			ret = -1;

		if (-1 == ret)
			break;

		ptok = &((*ptok)->next);
	}
	*ptok = make_token(TOKEN_END);
	if (0 == ret && *ptok)
		*save = tok;
	else
		free_tokens(tok);
	return ret;
}

void
free_tokens(struct token *tok)
{
	struct token *del;

	while (tok) {
		del = tok;
		tok = tok->next;

		if (del->type == TOKEN_STRING || del->type == TOKEN_SYMBOL)
			free(del->val.s);
		free(del);
	}
}

