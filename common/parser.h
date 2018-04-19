#ifndef _PARSER_H_
#define _PARSER_H_

#include "errors.h"

struct token {
	struct token *next;
	int type;
	union {
		char *s; /* on operators (such +, -, *) s == null */
		int i;
		float f;
	} val;
};

enum token_types {
	TOKEN_NULL,
	TOKEN_INT,
	TOKEN_FLOAT,
	TOKEN_STRING,
	TOKEN_SYMBOL,
	TOKEN_LP	= '(',
	TOKEN_RP	= ')',
	TOKEN_PLUS	= '+',
	TOKEN_MINUS	= '-',
	TOKEN_MUL	= '*',
	TOKEN_DIV	= '/',
	TOKEN_COMMA	= ',',
	TOKEN_SLP	= '[',
	TOKEN_SRP	= ']',
	TOKEN_END	= ';'
};

/* */
int  parse_expr(const char *, struct token **);
void free_tokens(struct token *);

#endif/*_PARSER_H_*/
