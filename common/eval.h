#ifndef _EVAL_H_
#define _EVAL_H_

struct res {
	int type;
	union {
		int  i;
		char *s;
		float f;
		struct {
			struct res *res;
			int nres;
		} arr;
	} val;
};

enum evalres_types {
	EVAL_INTEGER	= 'i',
	EVAL_FLOAT	= 'f',
	EVAL_STRING	= 's',
	EVAL_ARRAY	= 'a',
	EVAL_UNDEF	= 'u'
};

struct device;

/* */
int  do_eval(struct device *, struct token *, struct res *);
void init_res(struct res *);
void free_res(struct res *);

void set_eval_error(const char *fmt, ...);
void init_eval_error(void);
char * get_eval_error(void);

/* */
enum archive_params {
	ARCH_VNORM,
	ARCH_VSUBS_NORM,
	ARCH_MNORM,
	ARCH_MSUBS_NORM,
	ARCH_VWORK,
	ARCH_VSUBS_WORK,
	ARCH_PAVG,
	ARCH_DPAVG,
	ARCH_TAVG,
	ARCH_PBAR_AVG,
	ARCH_TENV_AVG,
	ARCH_VAGG_NORM,
	ARCH_MAGG_NORM,
	ARCH_VAGG_WORK,
	ARCH_SENSOR_VAL,
	ARCH_DAVG,
	ARCH_KAVG,
	ARCH_HAVG,
	ARCH_MAX_NPARAM /* it should be last */
};

struct archive {
	struct archive *next;

	int num;
	char datetime[64];

	float params[ARCH_MAX_NPARAM];
};

struct archive * alloc_archive(void);
void free_archive(struct archive *);

/* */
struct events {
	int num, event;
	char datetime[64];

	struct events *next;
};

void free_events(struct events *);

#endif/*_EVAL_H_*/
