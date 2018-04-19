#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <assert.h>

#include "device.h"
#include "parser.h"
#include "eval.h"
#include "utils.h"


struct handler {
	int ltype, rtype;
	int (*fn)(struct res *, const struct res *);
};

struct oper {
	int tok_type;
	struct handler *fh;
	int nfh;
};

static struct oper *
find_oper(struct oper *opers, int nopers, int tok_type)
{
	int i;

	for (i = 0; i < nopers; ++i)
		if (opers[i].tok_type == tok_type)
			return opers + i;
	return NULL;
}

static struct handler *
find_handler(struct oper *oper, int ltype, int rtype)
{
	int i;

	for (i = 0; i < oper->nfh; ++i)
		if (oper->fh[i].ltype == ltype && oper->fh[i].rtype == rtype)
			break;
	return i != oper->nfh ? oper->fh + i : NULL;
}

/* */
static void
free_res_array(struct res *res, int nres)
{
	int i;

	for (i = 0; i < nres; ++i)
		free_res(res + i);
}

/* */
static int  eval_add_int(struct res *, const struct res *);
static int  eval_add_float(struct res *, const struct res *);
static int  eval_concat(struct res *, const struct res *);

static int  eval_sub_int(struct res *, const struct res *);
static int  eval_sub_float(struct res *, const struct res *);

static int  eval_mul_int(struct res *, const struct res *);
static int  eval_mul_float(struct res *, const struct res *);

static int  eval_div_int(struct res *, const struct res *);
static int  eval_div_float(struct res *, const struct res *);
static int  eval_oper_undef(struct res *, const struct res *);

static int  eval_func(const char *, const struct res *, struct res *);

static int  expr(struct token **, struct res *);

static int
get_array_temp(struct token **ptok, struct res *res, int end_token)
{
	struct res *arr, *tmp;
	struct token *tok;
	int nln, nsz;

	tok = *ptok;
	arr = NULL;
	nln = nsz = 0;

	if (tok->type != end_token) {
		for (tok = *ptok; /* */; tok = tok->next) {
			if (nsz == nln) {
				nsz = nsz ? 2 * nsz : 8;
				tmp = realloc(arr, sizeof(*arr) * nsz);
				if (!tmp) {
					set_system_error("realloc");
					goto fail;
				}
				arr = tmp;
			}
			if (!tok || -1 == expr(&tok, arr + nln))
				goto fail;
			++nln;
		
			if (tok->type == TOKEN_COMMA)
				continue;
			if (tok->type == end_token)
				break;

			goto fail;
		}
		arr = realloc(arr, sizeof(*arr) * nln); /* optional step */
	}

	*ptok = tok; /* tok->type == ']' */
	res->type = EVAL_ARRAY;
	res->val.arr.res = arr;
	res->val.arr.nres = nln;
	return 0;
fail:
	free_res_array(arr, nln);
	free(arr);
	return -1;
}

static int
get_array(struct token **ptok, struct res *res)
{
	return get_array_temp(ptok, res, TOKEN_SRP);
}

static int
get_array_func(struct token **ptok, struct res *res)
{
	return get_array_temp(ptok, res, TOKEN_RP);
}

static int
prime(struct token **ptok, struct res *res)
{
	struct token *tok = *ptok;
	int ret = 0;

	switch (tok->type) {
	case TOKEN_INT:
		res->type  = EVAL_INTEGER;
		res->val.i = tok->val.i;
		break;
	case TOKEN_FLOAT:
		res->type  = EVAL_FLOAT;
		res->val.f = tok->val.f;
		break;
	case TOKEN_STRING:
		res->type  = EVAL_STRING;
		res->val.s = strdup(tok->val.s);
		if (NULL == res->val.s) {
			set_system_error("strdup");
			ret = -1;
		}
		break;
	case TOKEN_SYMBOL:
		tok = tok->next;
		if (tok->type == TOKEN_LP) {
			struct res argv;

			init_res(&argv);

			tok = tok->next;
			ret = get_array_func(&tok, &argv);
			if (0 == ret)
				ret = eval_func((*ptok)->val.s, &argv, res);

			free_res(&argv);
			
		} else {
			ret = -1;
		}
		assert(-1 == ret || (tok->type == TOKEN_RP && 0 == ret));
		break;
	case TOKEN_SLP:
		tok = tok->next;
		ret = get_array(&tok, res);
		break;
	case TOKEN_LP:
		tok = tok->next;
		ret = expr(&tok, res);
		if (0 == ret && TOKEN_RP != tok->type)
			ret = -1;
		break;
	case TOKEN_MINUS:
		tok = tok->next;
		ret = prime(&tok, res);
		if (0 == ret) {
			if (res->type == EVAL_INTEGER) {
				res->val.i = -res->val.i;
				*ptok = tok;
			} else if (res->type == EVAL_FLOAT) {
				res->val.f = -res->val.f;
				*ptok = tok;
			} else {
				ret = -1;
				free_res(res);
			}
		}
		return ret;
	case TOKEN_NULL:
		init_res(res);
		break;
	default:
		ret = -1;
	}
	if (0 == ret)
		*ptok = tok->next;
	return ret;
}

struct template {
	int (*next_func)(struct token **, struct res *);

	struct oper *opers;
	int nopers;

	struct token **ptok;
	struct res *save;
};

static int
expr_template(struct template *temp)
{
	struct handler *fh;
	struct oper *oper;
	struct token *tok;
	struct res left, right;
	int ret;

	init_res(&left);
	init_res(&right);

	tok = *(temp->ptok);
	ret = temp->next_func(&tok, &left);
	if (0 == ret) {
		for (;;) {
			oper = find_oper(temp->opers, temp->nopers, tok->type);
			if (NULL == oper) {
				*(temp->save) = left;
				*(temp->ptok) = tok;
				break;
			}

			tok = tok->next;
			ret = temp->next_func(&tok, &right);
			if (-1 == ret) {
				free_res(&left);
				break;
			}

			fh = find_handler(oper, left.type, right.type);
			ret = (fh != NULL) ?
				fh->fn(&left, &right) :
				eval_oper_undef(&left, &right);
			free_res(&right);
			if (-1 == ret) {
				free_res(&left);
				break;
			}
		}
	}
	return ret;
}

static int
term(struct token **ptok, struct res *save)
{
	static struct handler mul_funcs[] = {
		{EVAL_INTEGER,	EVAL_INTEGER,	eval_mul_int},
		{EVAL_FLOAT,	EVAL_INTEGER,	eval_mul_float},
		{EVAL_INTEGER,	EVAL_FLOAT,	eval_mul_float},
		{EVAL_FLOAT,	EVAL_FLOAT,	eval_mul_float}
	};
	static struct handler div_funcs[] = {
		{EVAL_INTEGER,	EVAL_INTEGER,	eval_div_int},
		{EVAL_FLOAT,	EVAL_INTEGER,	eval_div_float},
		{EVAL_INTEGER,	EVAL_FLOAT,	eval_div_float},
		{EVAL_FLOAT,	EVAL_FLOAT,	eval_div_float}
	};
	static struct oper opers[] = {
		{TOKEN_MUL,	mul_funcs, ARRAY_SIZE(mul_funcs)},
		{TOKEN_DIV,	div_funcs, ARRAY_SIZE(div_funcs)}
	};
	struct template temp = {
		.opers	= opers,
		.nopers	= ARRAY_SIZE(opers),
		.ptok	= ptok,
		.save	= save,
		.next_func = prime
	};
	return expr_template(&temp);
}

static int
expr(struct token **ptok, struct res *save)
{
	static struct handler add_funcs[] = {
		{EVAL_INTEGER,	EVAL_INTEGER,	eval_add_int},
		{EVAL_FLOAT,	EVAL_INTEGER,	eval_add_float},
		{EVAL_INTEGER,	EVAL_FLOAT,	eval_add_float},
		{EVAL_FLOAT,	EVAL_FLOAT,	eval_add_float},
		{EVAL_STRING,	EVAL_STRING,	eval_concat}
	};
	static struct handler sub_funcs[] = {
		{EVAL_INTEGER,	EVAL_INTEGER,	eval_sub_int},
		{EVAL_FLOAT,	EVAL_INTEGER,	eval_sub_float},
		{EVAL_INTEGER,	EVAL_FLOAT,	eval_sub_float},
		{EVAL_FLOAT,	EVAL_FLOAT,	eval_sub_float}
	};
	static struct oper opers[] = {
		{TOKEN_PLUS,	add_funcs, ARRAY_SIZE(add_funcs)},
		{TOKEN_MINUS,	sub_funcs, ARRAY_SIZE(sub_funcs)}
	};
	struct template temp = {
		.opers	= opers,
		.nopers	= ARRAY_SIZE(opers),
		.ptok	= ptok,
		.save	= save,
		.next_func = term
	};
	return expr_template(&temp);
}



static int
eval_concat(struct res *lvalue, const struct res *arg)
{
	char *s;
	int ret;

	s = dynsprintf("%s%s", lvalue->val.s, arg->val.s);
	if (s) {
		free_res(lvalue);
		lvalue->type 	= EVAL_STRING;
		lvalue->val.s	= s;
		ret = 0;
	} else {
		ret = -1;
	}
	return ret;
}

static void
make_float(struct res *dest, const struct res *src)
{
	assert(src->type == EVAL_INTEGER || src->type == EVAL_FLOAT);

	if (src->type == EVAL_INTEGER)
		dest->val.f = (float)(src->val.i);
	else
		dest->val.f = src->val.f;

	dest->type = EVAL_FLOAT;
}

static int
eval_add_int(struct res *dest, const struct res *arg)
{
	dest->val.i += arg->val.i;
	return 0;
}

static int
eval_add_float(struct res *dest, const struct res *arg)
{
	struct res res;

	make_float(dest, dest);
	make_float(&res, arg);

	dest->val.f += res.val.f;
	return 0;
}

static int
eval_sub_int(struct res *dest, const struct res *arg)
{
	dest->val.i -= arg->val.i;
	return 0;
}

static int
eval_sub_float(struct res *dest, const struct res *arg)
{
	struct res res;

	make_float(dest, dest);
	make_float(&res, arg);

	dest->val.f -= res.val.f;
	return 0;
}

static int
eval_mul_int(struct res *dest, const struct res *arg)
{
	dest->val.i *= arg->val.i;
	return 0;
}

static int
eval_mul_float(struct res *dest, const struct res *arg)
{
	struct res res;

	make_float(dest, dest);
	make_float(&res, arg);

	dest->val.f *= res.val.f;
	return 0;
}

static int
eval_div_int(struct res *dest, const struct res *arg)
{
	int ret;

	if (arg->val.i) {
		dest->val.i /= arg->val.i;
		ret = 0;
	} else {
		free_res(dest);
		ret = -1;
	}
	return ret;
}

static int
eval_div_float(struct res *dest, const struct res *arg)
{
	struct res res;
	int ret;

	make_float(dest, dest);
	make_float(&res, arg);

	if (arg->val.f != 0.0) {
		dest->val.f /= res.val.f;
		ret = 0;
	} else {
		free_res(dest);
		ret = -1;
	}
	return ret;
}

static int
eval_oper_undef(struct res *dest, const struct res *arg)
{
	init_res(dest);
	return 0;
}

/* */

static int
fn_get(struct device *dev, const struct res *argv, struct res *ret)
{
	struct res *arg = argv->val.arr.res;
	float tmp;
	int rv;

	rv = dev->opers->get(dev, arg->val.i, &(ret->val.s));
	if (0 == rv)
		ret->type = EVAL_STRING;
	/*
	 * It was wrong descision to change return type from float to string.
	 * Now user cannot to do any arithmetic operations with return value.
	 * 
	 * To fix it we convert return value to float if it's possible.
	 * This is temporary
	 */
	if (NULL == ret->val.s) {
		init_res(ret);
	} else if (1 == sscanf(ret->val.s, "%f", &tmp)) {
		free(ret->val.s);
		ret->val.f = tmp;
		ret->type = EVAL_FLOAT;

		if (dev->db)
			save_param(dev, arg->val.i, tmp);
	}
	return rv;
}

static int
fn_set(struct device *dev, const struct res *argv, struct res *ret)
{
	struct res *rparam, *rvalue;
	int rv;
	
	rparam = argv->val.arr.res;
	rvalue = argv->val.arr.res + 1;

	if (dev->opers->set) {
		rv = dev->opers->set(dev, rparam->val.i, rvalue->val.s, &(ret->val.s));
		if (0 == rv)
			ret->type = EVAL_STRING;
	} else {
		rv = -1;
	}
	return rv;
}

static int
fn_debug(struct device *dev, const struct res *argv, struct res *ret)
{
	struct res *arg = argv->val.arr.res;
	int rv, retval;

	if (dev->opers->debug) {
		rv = dev->opers->debug(dev, arg->val.s, &retval);
		if (0 == rv) {
			init_res(ret);
			ret->type	= EVAL_INTEGER;
			ret->val.i	= retval;
		}
	} else {
		rv = -1;
	}
	return rv;
}

static int
fn_setf(struct device *dev, const struct res *argv, struct res *ret)
{
	struct res *iarg = argv->val.arr.res;
	struct res *farg = argv->val.arr.res + 1;
	int rv, retval;

	if (dev->opers->setf) {
		rv = dev->opers->setf(dev, iarg->val.i, farg->val.f, &retval);
		if (0 == rv) {
			init_res(ret);
			ret->type	= EVAL_INTEGER;
			ret->val.i	= 0;
		}
	} else {
		rv = -1;
	}
	return rv;
}


/* */

static int  archive2array(struct archive *, struct res *);

static int
fn_hour_archiv(struct device *dev, const struct res *argv, struct res *ret)
{
	struct archive *archive;
	struct res *from;
	struct res *to;
	int arch;
	int rv;

	archive = NULL;
	arch = argv->val.arr.res[0].val.i;
	from = argv->val.arr.res + 1;
	to   = argv->val.arr.res + 2;

	if (dev->opers->h_archiv) {
		rv = dev->opers->h_archiv(dev, arch, from->val.s,
				to->val.s, &archive);
		if (0 == rv) {
			if (dev->db)
				save_all_archive(dev, "hour", archive);
			init_res(ret);
			rv = archive2array(archive, ret);
			free_archive(archive);
		}
	} else {
		init_res(ret);
		rv = 0;
	}
	return rv;
}

static int
fn_month_archiv(struct device *dev, const struct res *argv, struct res *ret)
{
	struct archive *archive;
	struct res *from;
	struct res *to;
	int arch;
	int rv;

	archive = NULL;
	arch = argv->val.arr.res[0].val.i;
	from = argv->val.arr.res + 1;
	to   = argv->val.arr.res + 2;

	if (dev->opers->m_archiv) {
		rv = dev->opers->m_archiv(dev, arch, from->val.s, to->val.s, &archive);
		if (0 == rv) {
			if (dev->db)
				save_all_archive(dev, "month", archive);
			init_res(ret);
			rv = archive2array(archive, ret);
			free_archive(archive);
		}
	} else {
		init_res(ret);
		rv = 0;
	}
	return rv;
}

static int
fn_day_archiv(struct device *dev, const struct res *argv, struct res *ret)
{
	struct archive *archive;
	struct res *from;
	struct res *to;
	int arch;
	int rv;

	archive = NULL;
	arch = argv->val.arr.res[0].val.i;
	from = argv->val.arr.res + 1;
	to   = argv->val.arr.res + 2;

	if (dev->opers->d_archiv) {
		rv = dev->opers->d_archiv(dev, arch, from->val.s,
				to->val.s, &archive);
		if (0 == rv) {
			if (dev->db)
				save_all_archive(dev, "day", archive);
			init_res(ret);
			rv = archive2array(archive, ret);
			free_archive(archive);
		}
	} else {
		init_res(ret);
		rv = 0;
	}
	return rv;
}

static int
events_count(struct events *top)
{
	int i;

	for (i = 0; top; top = top->next, ++i)
		;
	return i;
}

static int
conv_events(struct events *ev, struct res *res)
{
	struct res *arr;
	char *str;
	int ret;

	arr = calloc(2, sizeof(*arr));
	if (!arr) {
		set_system_error("calloc");
		ret = -1;
	} else if (NULL == (str = strdup(ev->datetime))) {
		free(arr);
		set_system_error("strdup");
		ret = -1;
	} else {
		arr[0].type = EVAL_STRING;
		arr[0].val.s = str;

		arr[1].type = EVAL_INTEGER;
		arr[1].val.i = ev->event;

		res->type = EVAL_ARRAY;
		res->val.arr.res = arr;
		res->val.arr.nres = 2;
		ret = 0;
	}
	return ret;
}

static int
events2array(struct events *top, struct res *ret)
{
	struct res *arr, retval;
	int narr;
	int rv, i;

	narr = events_count(top);
	arr = calloc(narr, sizeof(*arr));
	if (arr) {
		rv = 0;

		retval.type = EVAL_ARRAY;
		retval.val.arr.res = arr;
		for (i = 0; i < narr; ++i) {
			rv = conv_events(top, arr + i);
			if (-1 == rv) {
				free_res(&retval);
				break;
			}

			top = top->next;

			retval.val.arr.nres = i + 1;
		}
		if (0 == rv)
			*ret = retval;
	} else {
		set_system_error("calloc");
		rv = -1;
	}
	return rv;
}

static int
fn_events(struct device *dev, const struct res *argv, struct res *ret)
{
	struct events *events;
	struct res *from, *to;
	int rv;

	events = NULL;
	from = argv->val.arr.res;
	to = argv->val.arr.res + 1;

	if (dev->opers->events) {
		rv = dev->opers->events(dev, from->val.s, to->val.s, &events);
		if (0 == rv) {
			if (dev->db)
				save_all_events(dev, events);
			rv = events2array(events, ret);
			free_events(events);
		}
	} else {
		init_res(ret);
		rv = 0;
	}
	return rv;
}

static int
fn_date(struct device *dev, const struct res *argv, struct res *ret)
{
	struct res *arg = argv->val.arr.res;
	int rv;

	if (dev->opers->date) {
		rv = dev->opers->date(dev, arg->val.s, &(ret->val.s));
		if (0 == rv)
			ret->type = EVAL_STRING;

	} else {
		set_eval_error("date isn't supported on this type of VK");
		rv = -1;
	}
	return rv;
}

/* */
struct func_table_node {
	const char *name;
	const char *signature;
	int  (*func)(struct device *, const struct res *, struct res *);
};

static struct func_table_node func_table[] = {
	{"get",		"i",	fn_get},
	{"set",		"is",	fn_set},
	{"harchive",	"iss",	fn_hour_archiv},
	{"darchive",	"iss",	fn_day_archiv},
	{"marchive",	"iss",	fn_month_archiv},
	{"events",	"ss",	fn_events},
	{"set",		"if",	fn_setf},
	{"debug",	"s",	fn_debug},
	{"date",	"s",	fn_date}
};


/* */
static struct device *device;

static int
check_signature(const char *signature, const struct res *argv)
{
	int i, rv, f;

	for (i = 0, rv = 0; /* empty */; ++i) {
		if ('\0' == signature[i] && i == argv->val.arr.nres)
			break;
		f = ('\0' == signature[i] || i == argv->val.arr.nres) ||
			(int)(signature[i]) != argv->val.arr.res[i].type;
		if (f) {
			rv = -1;
			break;
		}
	}
	return rv;
}

static int
run_func(struct func_table_node *node, const struct res *argv, struct res *ret)
{
	int rv;

	if (check_signature(node->signature, argv) == 0)
		rv = node->func(device, argv, ret);
	else
		rv = 0;

	return rv;
}

static int
eval_func(const char *name, const struct res *argv, struct res *res)
{
	int i, ret;

	init_res(res);

	for (i = 0, ret = 0; i < ARRAY_SIZE(func_table); ++i) {
		if (0 == strcmp(name, func_table[i].name)) {
			ret = run_func(func_table + i, argv, res);
			break;
		}
	}
	return ret;
}

/* */
int
do_eval(struct device *dev, struct token *tok, struct res *res)
{
	int ret;

	device = dev;
	ret = expr(&tok, res);
	return ret;
}

void
init_res(struct res *res)
{
	memset(res, 0, sizeof(*res));
	res->type = EVAL_UNDEF;
}

void
free_res(struct res *res)
{
	if (EVAL_STRING == res->type) {
		free(res->val.s);
	} else if (EVAL_ARRAY == res->type) {
		free_res_array(res->val.arr.res, res->val.arr.nres);
		free(res->val.arr.res);
	}
	init_res(res);
}

/* */


void
free_archive(struct archive *top)
{
	struct archive *del;

	while (top) {
		del = top;
		top = top->next;

		free(del);
	}
}

struct archive *
alloc_archive(void)
{
	struct archive *p = malloc(sizeof(*p));

	if (p) {
		int i;

		p->num = 0;
		memset(p->datetime, 0, sizeof(p->datetime));
		p->next	= NULL;

		for (i = 0; i < ARCH_MAX_NPARAM; ++i)
			p->params[i] = nanf("char-sequence");
	} else {
		set_system_error("malloc");
	}
	return p;
}

static int
archive_count(struct archive *top)
{
	int ret;

	for (ret = 0; top; top = top->next, ++ret)
		;
	return ret;
}

static int
conv2array(struct archive *arch, struct res *res)
{
	struct res *arr;
	char *str;
	int i, ret;

	arr = calloc(ARCH_MAX_NPARAM + 1, sizeof(*arr));
	if (!arr) {
		set_system_error("calloc");
		ret = -1;
	} else if ((str = strdup(arch->datetime)) == NULL) {
		free(arr);
		set_system_error("strdup");
		ret = -1;
	} else {
		arr[0].type = EVAL_STRING;
		arr[0].val.s = str;

		for (i = 0; i < ARCH_MAX_NPARAM; ++i) {
			if (isnan(arch->params[i])) {
				init_res(arr + i + 1);
			} else {
				arr[i + 1].type = EVAL_FLOAT;
				arr[i + 1].val.f = arch->params[i];
			}
		}
		
		res->type = EVAL_ARRAY;
		res->val.arr.res = arr;
		res->val.arr.nres = ARCH_MAX_NPARAM + 1;
		ret = 0;
	}
	return ret;
}

static int
archive2array(struct archive *top, struct res *res)
{
	struct res *arr, retval;
	int narr, i, ret;

	narr = archive_count(top);
	if (narr > 0) {
		arr  = calloc(narr, sizeof(*arr));
	
		if (arr) {
			retval.type = EVAL_ARRAY;
			retval.val.arr.res = arr;
			retval.val.arr.nres = 0;
	
			for (i = 0; i < narr; ++i) {
				ret = conv2array(top, arr + i);
				if (-1 == ret) {
					free_res(&retval);
					break;
				}
				top = top->next;
	
				retval.val.arr.nres = i + 1;
			}
			if (i == narr)
				*res = retval;
		} else {
			ret = -1;
		}
	} else {
		res->type = EVAL_ARRAY;
		ret = 0;
	}
	return ret;
}

/* */
void
free_events(struct events *top)
{
	while (top) {
		struct events *del = top;

		top = top->next;
		free(del);
	}
}


static char eval_errmsg[1024];

void
set_eval_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(eval_errmsg, sizeof(eval_errmsg), fmt, ap);
	va_end(ap);
}

char *
get_eval_error(void)
{
	return eval_errmsg;
}

void
init_eval_error(void)
{
	eval_errmsg[0] = '\0';
}

