#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
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

#include "spg741.h"
#include <wchar.h>
#include <locale.h>
#include <math.h>

/* */

struct spg741_ctx {

	char *send_query;
	int  send_query_sz;

	regex_t re;

	char crc_flag;

	uint16_t crc;
};

#define get_spec(dev)	((struct spg741_ctx *)((dev)->spec))
                                     
/* */
static int  spg741_init(struct device *);
static void spg741_free(struct device *);
static int  spg741_get(struct device *, int, char **);
static int  spg741_set(struct device *, int, char *, char **);

static int  spg741_parse_msg(struct device *, char *, int);
static int  spg741_parse_crc_msg(struct device *, char *, int);
static int  spg741_check_crc(struct device *, char *, int);
static int  spg741_send_msg(struct device *);

static int  spg741_date(struct device *dev, const char *, char **);
//static int  spg741_get_date(struct device *dev, char **save);
//static int  spg741_set_date(struct device *dev, char *ret, char **save);

static int  spg741_h_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  spg741_m_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  spg741_d_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  spg741_events(struct device *, const char *, const char *,
				struct events **);
static int  spg741_get_string_param(struct device *, uint16_t, char **, uint8_t, uint16_t);
static int  spg741_set_string_param(struct device *, uint16_t, char *, uint8_t, uint16_t);

static int  generate_sequence (struct device *dev, uint8_t type, uint8_t func, uint16_t padr, uint8_t nchan, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence, uint16_t* len);
static int  analyse_sequence (char* dats, uint len, char* answer, uint8_t analyse, uint8_t id, AnsLog *alog, uint16_t padr);

static int  checkdate (uint8_t	type, int ret_fr, int ret_to, struct tm* time_from, struct tm* time_to, struct tm* time_cur, time_t *tim, time_t *sttime, time_t *fntime);

static double cIEEE754toFloat (char *Data);

static uint16_t
calc_bcc (uint8_t *ptr, int n)
{
 uint8_t crc = 0;
 while (n-- > 0) 
	{
	 crc = crc + (uint8_t) *ptr++;
	}
 crc=crc^0xff;
 return crc;
}

static int  spg741_date(struct device *dev, const char *ret, char **save)
{
	int	rt=0;
	char	date[30];
	struct 	tm *time_to=malloc (sizeof (struct tm));

	if (strlen (ret)==0)
		rt = spg741_get_string_param(dev, 0xf3, save, TYPE_CURRENTS, 0);
	else	{
		 sscanf(ret,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
		 snprintf (date,25,"%02d-%02d-%02d",time_to->tm_mday, time_to->tm_mon, time_to->tm_year-2000);
		 rt = spg741_set_string_param(dev, 9, (char *)date, TYPE_CONST, 0);
		 snprintf (date,25,"%02d-%02d-%02d", time_to->tm_hour, time_to->tm_min, time_to->tm_sec);
		 rt = spg741_set_string_param(dev, 10, (char *)date, TYPE_CONST, 0);
		 *save=malloc (10);
		 if (rt==0) sprintf (*save,"ok");
		 else sprintf (*save,"error");
		}
	free	(time_to);
	return	rt;
}

/*
static int  spg741_get_date(struct device *dev, char **save)
{
	int	rt=0;
	rt = spg741_get_string_param(dev, 0xf3, save, TYPE_CURRENTS, 0);
	return	rt;
}

static int  spg741_set_date(struct device *dev, char *ret, char **save)
{
	int	rt=0;
	rt = spg741_set_string_param(dev, 0xf3, ret, TYPE_CURRENTS, 0);
	return	rt;
}*/

static void
ctx_zero(struct spg741_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

static int
spg741_get_ident(struct device *dev)
{
	static char ident_query[100];
	struct spg741_ctx *ctx;
	char spg_id[100],answer[100];
	int 		ret;
	uint16_t	len;
	AnsLog	alog;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));

	time_t tim;
	tim=time(&tim);			// default values
	localtime_r(&tim,time_from); 	// get current system time
	localtime_r(&tim,time_to); 	// get current system time

	ctx = get_spec(dev);
	ret = -1;
	ctx->crc_flag = 0;

	if (-1 == generate_sequence (dev, 0, START_EXCHANGE, 0, 0, 0, 1, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto free_regex;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, &alog, 0)) goto free_regex;
	buf_free(&dev->buf);

	if (-1 == generate_sequence (dev, 0, GET741_FLASH, 3, 0, 0, 3, time_from, time_to, ident_query, &len)) goto free_regex;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto free_regex;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, spg_id, ANALYSE, 3, &alog, 8)) goto free_regex;
	buf_free(&dev->buf);
	sprintf (spg_id,"%s",alog.data[3][0]);
	if (!dev->quiet)
		devlog(dev, "\tSPG ident=%s\n", spg_id);
	ret = 0;

free_regex:
	regfree(&ctx->re);
out:
	ctx_zero(ctx);
	buf_free(&dev->buf);
	free (time_from);
	free (time_to);
	return ret;
}

static int
spg741_get_string_param(struct device *dev, uint16_t addr, char **save, uint8_t type, uint16_t padr)
{
	struct 	spg741_ctx *ctx;
	static char ident_query[100],	answer[1024], date[30];
	void 	*fnsave;
	int 	ret;
	uint16_t	len;
	float	temp=0,temp2=0;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	char	*value_string;
	AnsLog	alog;

	value_string=malloc (100);
	time_t tim;
	tim=time(&tim);			// default values
	localtime_r(&tim,time_from); 	// get current system time
	localtime_r(&tim,time_to); 	// get current system time

	ctx = get_spec(dev);
	ret = -1;

	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = spg741_parse_crc_msg;

	if (type==TYPE_CURRENTS) {
		if (padr>0x200)
		    {
			if (-1 == generate_sequence (dev, 0, GET741_FLASH, addr+8, padr, 88, 8, time_from, time_to, ident_query, &len)) goto out;
			ctx->send_query = ident_query;
			ctx->send_query_sz = len;
			if (-1 == dev_query(dev)) goto out;
			if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, addr%4, &alog, padr)) goto out;
			buf_free(&dev->buf);
			temp2=cIEEE754toFloat(alog.data[0][0]);
			//printf ("%f %x %x\n",temp2,alog.data[0][0][0],alog.data[0][0][1]);
		    }
		    
		if (padr==0)
    		    if (-1 == generate_sequence (dev, 0, GET741_RAM, addr, 0, 1, 8, time_from, time_to, ident_query, &len)) goto out;
		if (padr>0)
    		    if (-1 == generate_sequence (dev, 0, GET741_RAM, addr, 0, 1, 4, time_from, time_to, ident_query, &len)) goto out;

		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) goto out;
		if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, &alog, padr)) goto out;
		buf_free(&dev->buf);
		if (!dev->quiet)
		    devlog(dev, "RECV: [%x %x %x %x]\n", (uint8_t)alog.data[0][0][0], (uint8_t)alog.data[0][0][1], (uint8_t)alog.data[0][0][2], (uint8_t)alog.data[0][0][3]);
		if (padr>0)
			{
			 temp=cIEEE754toFloat(alog.data[0][0]);
			 //fprintf (stderr,"%f (%x %x %x %x)\n",temp,(uint8_t)alog.data[0][0][0],(uint8_t)alog.data[0][0][1],(uint8_t)alog.data[0][0][2],(uint8_t)alog.data[0][0][3]);
			 snprintf (value_string,100,"%f",temp);
			 *save=value_string;
			 ret=0;
			}
		else	{
			 //fprintf (stderr,"%d-%02d-%02d,%02d:%02d:%02d\n",alog.data[0][0][2]+2000,alog.data[0][0][1],alog.data[0][0][0],alog.data[0][0][3],alog.data[0][0][4],alog.data[0][0][5]);
			 snprintf (value_string,100,"%d-%02d-%02d,%02d:%02d:%02d",alog.data[0][0][0]+2000,alog.data[0][0][1],alog.data[0][0][2],alog.data[0][0][3],alog.data[0][0][4],alog.data[0][0][5]);
			 *save=value_string;
			 ret=0;
			}
		 if (!dev->quiet)
		    fprintf (stderr,"=%s\n",value_string);
		}
	if (type==TYPE_CONST) {
		if (-1 == generate_sequence (dev, 0, GET741_FLASH, addr, padr, 0, 8, time_from, time_to, ident_query, &len)) goto out;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) goto out;
		if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, addr%4, &alog, padr)) goto out;
		buf_free(&dev->buf);
		snprintf (value_string,100,"%s",alog.data[addr%4][0]);

		if (addr==9)	{
			snprintf (date,20,"%s",alog.data[(addr)%4][0]);
			if (-1 == generate_sequence (dev, 0, GET741_FLASH, addr+1, padr, 0, 8, time_from, time_to, ident_query, &len)) goto out;
			ctx->send_query = ident_query;
			ctx->send_query_sz = len;
			if (-1 == dev_query(dev)) goto out;
			if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, (addr+1)%4, &alog, padr)) goto out;
			buf_free(&dev->buf);
			snprintf (value_string,30,"%s,%s",date,alog.data[(addr+1)%4][0]);
			sscanf(value_string,"%d-%d-%d,%d-%d-%d",&time_to->tm_mday, &time_to->tm_mon, &time_to->tm_year, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
			snprintf (value_string,25,"%02d-%02d-%02d,%02d:%02d:%02d",time_to->tm_year+2000, time_to->tm_mon, time_to->tm_mday, time_to->tm_hour, time_to->tm_min, time_to->tm_sec);
			//printf ("date=%s\n",value_string);
		    }
		if (!dev->quiet)
		    fprintf (stderr,"=%s\n",value_string);
		*save=value_string;
		ret=0;
		}

out:
	regfree(&ctx->re);
//	free(ctx->send_query);
	/* restore handler */
	dev->opers->parse_msg = fnsave;
	buf_free(&dev->buf);
	ctx_zero(ctx);
	free (time_from);
	free (time_to);
	return ret;
}

static int
spg741_set_string_param(struct device *dev, uint16_t addr, char *save, uint8_t type, uint16_t padr)
{
	struct spg741_ctx *ctx;
	static char ident_query[100],	answer[1024];
	void 		*fnsave;
	int 		ret, i;
	uint16_t	len;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	AnsLog	alog;

	time_t tim;
	tim=time(&tim);			// default values
	localtime_r(&tim,time_from); 	// get current system time
	localtime_r(&tim,time_to); 	// get current system time

	ctx = get_spec(dev);
	ret = -1;

	/* save previous handler */
	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = spg741_parse_crc_msg;
	if (-1 == generate_sequence (dev, 0, SET741_PARAM, addr, padr, 0, 8, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, &alog, padr)) goto out;
	buf_free(&dev->buf);
	if (!dev->quiet)
    	    devlog(dev, "RECV: %s [%s]\n", alog.data[0][0],alog.time[0][0]);

	if (type==TYPE_CONST)	{
		for (i=0; i<64; i++) 
		    {
		    if (save[i]>10) ident_query[i]=save[i];
		        else
	    		for (; i<64; i++) ident_query[i]=0x20;
		    }
		ident_query[64]=0;
	    }
	if (strlen(ident_query)==64)	{
		if (-1 == generate_sequence (dev, 1, SET741_PARAM, addr, padr, 0, 8, time_from, time_to, ident_query, &len)) goto out;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) goto out;
		if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, &alog, padr)) goto out;
		buf_free(&dev->buf);
		if (!dev->quiet)
    		    devlog(dev, "RECV: %s [%s]\n", alog.data[0][0],alog.time[0][0]);
    		ret=0;
    	    }
out:
	regfree(&ctx->re);
//	free(ctx->send_query);
	dev->opers->parse_msg = fnsave;
	buf_free(&dev->buf);
	free (time_from);
	free (time_to);
	ctx_zero(ctx);
	return ret;
}

/*
 * Interface
 */
static struct vk_operations spg741_opers = {
	.init	= spg741_init,
	.free	= spg741_free,
	.get	= spg741_get,
	.set	= spg741_set,
	.date	= spg741_date,

	.send_msg  = spg741_send_msg,
	.parse_msg = spg741_parse_msg,
	.check_crc = spg741_check_crc,

	.h_archiv = spg741_h_archiv,
	.m_archiv = spg741_m_archiv,
	.d_archiv = spg741_d_archiv,

	.events = spg741_events
};

void
spg741_get_operations(struct vk_operations **p_opers)
{
	*p_opers = &spg741_opers;
}

static int
spg741_init(struct device *dev)
{
	struct spg741_ctx *spec;
	int ret=-1;

	spec = calloc(1, sizeof(*spec));

	if (!spec) {
		set_system_error("calloc");
		ret = -1;
	} else {
		dev->spec = spec;
		ret =   (-1 == spg741_get_ident(dev));
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
spg741_free(struct device *dev)
{
	struct spg741_ctx *spec = get_spec(dev);

	free(spec);
	dev->spec = NULL;
}


static int
spg741_get_ns (int no, char* code)
{
    int i;
    for (i=0; i<ARRAY_SIZE(nscode741); i++)
	if (no == nscode741[i].id)
	    {
	    snprintf (code,70,"%s",nscode741[i].name);
	    return 1;
	    }
    return -1;
}

static int
spg741_get(struct device *dev, int param, char **ret)
{
	int i=0, rv=0;

	for (i=0; i<ARRAY_SIZE(currents741); i++)
	if (param == currents741[i].no) {
	    rv = spg741_get_string_param(dev, currents741[i].adr, ret, TYPE_CURRENTS, currents741[i].type);
    	    return rv;
	}
	for (i=0; i<ARRAY_SIZE(const741); i++)
	if (param == const741[i].no)  {
    	    rv = spg741_get_string_param(dev, const741[i].adr, ret, TYPE_CONST, const741[i].type);
	    return rv;
    	}
	return -1;
}

static int
spg741_set(struct device *dev, int param, char* ret, char** save)
{
	int i=0, rv=0;

	for (i=0; i<ARRAY_SIZE(const741); i++)
	if (param == const741[i].no)  {
		rv = spg741_set_string_param(dev, const741[i].adr, ret, TYPE_CONST, const741[i].type);
		*save=malloc (10);
		snprintf (*save,10,"ok");
		return rv;
    	}
	for (i=0; i<ARRAY_SIZE(currents741); i++)
	if (param == currents741[i].no)  {
		rv = spg741_set_string_param(dev, currents741[i].adr, ret, TYPE_CURRENTS, currents741[i].type);
		*save=malloc (10);
		snprintf (*save,10,"ok");
		return rv;
    	}

	return -1;
}

static int
make_archive(struct device *dev, int no, AnsLog* alog, int type, struct archive **save, uint16_t recs)
{
	struct archive *archives = NULL, *lptr = NULL, *ptr;
	int nrecs,i=0,ret,np=0;
	struct 	tm *tim=malloc (sizeof (struct tm));
	uint16_t	ind=0;

	ret = -1;
	nrecs = alog->quant_param;
	if (nrecs <= 0) return -1;
	if (!dev->quiet)
		fprintf(stderr, "RECS: %d\n", nrecs);

	if(*save != NULL)
	{
	    lptr = *save;
	    while(lptr->next != NULL)
		lptr = lptr->next;
	    ind = lptr->num + 1;
	}

	for (i=0; i < nrecs; ++i)
		{
		  if ((ptr = alloc_archive()) != NULL) 
			{
			 ptr->num = ind+i;

			 for (np=0; np<ARRAY_SIZE(archive741); np++)
				{
    				 snprintf(ptr->datetime, sizeof(ptr->datetime),"%s",alog->time[archive741[np].id][i]);
    				 ptr->params[archive741[np].id] = cIEEE754toFloat((alog->data[archive741[np].id][i]));
				 if (!dev->quiet)
				    fprintf (stderr,"[%s] %f\n",ptr->datetime,ptr->params[archive741[np].id]);
    				}
			 ret=0;

			 if(!lptr)
				archives = ptr;
			 else
				lptr->next = ptr;
			 lptr = ptr;
			}
		else	{
			ret  = -1;
			break;
		    }
		}

	if(!(*save))
	    *save = archives;
	free (tim);
	return ret;
}

static int 
spg741_h_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct spg741_ctx *ctx;
	AnsLog alog;
	int ret,ret_fr,ret_to,quant=0,i;
	uint16_t len,try;
	static char ident_query[100], answer[1024];
	time_t tim,sttime,fntime,entime;
	ctx = get_spec(dev);
	ctx->crc_flag = 1;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);

	struct 	tm *time_to=malloc (sizeof (struct tm));
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	tim=time(&tim);
	localtime_r(&tim,time_cur);

	ret=checkdate (TYPE_HOURS,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
	alog.quant_param=0;

	if (sttime && fntime && ret==0)
	while (sttime<=fntime)
		{
	         entime=sttime+3600*1;
		 localtime_r(&sttime,time_from);
		 localtime_r(&entime,time_to);
		 for (i=0; i<ARRAY_SIZE(archive741); i++)	{
		    try=5;
	    	    while (try--)	{
	    		alog.quant_param=0;
	    		buf_free(&dev->buf);
			snprintf(alog.time[archive741[i].id][0], 20,"%d-%02d-%02d,%02d:00:00",time_to->tm_year+1900, time_to->tm_mon+1, time_to->tm_mday, time_to->tm_hour);
			if (-1 == generate_sequence (dev, TYPE_HOURS, GET741_HOURS, archive741[i].adr, 0, 1, no, time_from, time_to, ident_query, &len)) break;
	 		ctx->send_query = ident_query;
	 		ctx->send_query_sz = len;
			if (-1 == dev_query(dev)) continue;
			if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, archive741[i].id, &alog, archive741[i].adr)) continue;
			//fprintf (stderr,"(%d/%d) %s (%s)\n",archive741[i].id,quant,alog.time[archive741[i].id][0],alog.data[archive741[i].id][0]);
			break;
			}
		    }
		 if (alog.quant_param)		{
		         ret = make_archive(dev, no, &alog, TYPE_HOURS, save, quant-1);
			 quant++;
			}
		 sttime+=3600;
		}
	if (quant == 0)  {
		*save = alloc_archive();
		(*save)->num=0;
		ret=0;
		}
    	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int 
spg741_d_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct spg741_ctx *ctx;
	AnsLog alog;
	int ret,ret_fr,ret_to,quant=0,i;
	uint16_t len,try;
	static char ident_query[100], answer[1024];
	time_t tim,sttime,fntime,entime;

	ctx = get_spec(dev);
	ctx->crc_flag = 1;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);

	struct 	tm *time_to=malloc (sizeof (struct tm));
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	tim=time(&tim);
	localtime_r(&tim,time_cur);

	ret=checkdate (TYPE_MONTH,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
	alog.quant_param=0;
	sttime-=3600;

	if (sttime && fntime && ret==0)
	while (sttime<=fntime)
		{
	         entime=sttime+3600*24;
		 localtime_r(&sttime,time_from);
		 localtime_r(&entime,time_to);
		 for (i=0; i<ARRAY_SIZE(archive741); i++)	{
		    try=5;
	    	    while (try--)	{
	    		alog.quant_param=0;
	    		buf_free(&dev->buf);
			snprintf(alog.time[archive741[i].id][0], 20,"%d-%02d-%02d,00:00:00",time_to->tm_year+1900, time_to->tm_mon+1, time_to->tm_mday);
			if (-1 == generate_sequence (dev, TYPE_DAYS, GET741_DAYS, archive741[i].adr, 0, 1, no, time_from, time_to, ident_query, &len)) break;
    	 		ctx->send_query = ident_query;
	 		ctx->send_query_sz = len;
			if (-1 == dev_query(dev)) continue;
			if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, archive741[i].id, &alog, archive741[i].adr)) continue;
	    		break;
			}
		    }
		 if (alog.quant_param)		{
		         ret = make_archive(dev, no, &alog, TYPE_DAYS, save, quant-1);
		         ret=0;
			 quant++;
			}
		 sttime+=3600*24;
		}
	if (quant == 0)  {
		*save = alloc_archive();
		(*save)->num=0;
		ret=0;
		}

    	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int 
spg741_m_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct spg741_ctx *ctx;
	AnsLog alog;
	int ret,ret_fr,ret_to,quant=0,i;
	uint16_t len,try;
	static char ident_query[100], answer[1024];
	time_t tim,sttime,fntime,entime;

	ctx = get_spec(dev);
	ctx->crc_flag = 0;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
	struct 	tm *time_to=malloc (sizeof (struct tm));
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
	struct 	tm *time_cur=malloc (sizeof (struct tm));
	tim=time(&tim);
	localtime_r(&tim,time_cur);

	ret=checkdate (TYPE_MONTH,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
	alog.quant_param=0;

	if (sttime && fntime && ret==0)
	while (sttime<=fntime)
		{
	         entime=sttime+3600*24*31;
		 //if (entime>fntime) entime=fntime;
		 localtime_r(&sttime,time_from);
		 localtime_r(&entime,time_to);

		 for (i=0; i<ARRAY_SIZE(archive741); i++)	{
			try=5;
	    		while (try--)	{
	    		    alog.quant_param=0;
	    		    buf_free(&dev->buf);
			    snprintf(alog.time[archive741[i].id][0], 20,"%d-%02d-01,00:00:00",time_to->tm_year+1900, time_to->tm_mon+1);
			    if (-1 == generate_sequence (dev, TYPE_MONTH, GET741_MONTHS, archive741[i].adr, 0, 1, no, time_from, time_to, ident_query, &len)) break;
	 		    ctx->send_query = ident_query;
	 		    ctx->send_query_sz = len;
			    if (-1 == dev_query(dev)) continue;
			    if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, archive741[i].id, &alog, archive741[i].adr))	continue;
	    		    break;
			}
		    }
		 if (alog.quant_param)		{
		         ret = make_archive(dev, no, &alog, TYPE_MONTH, save, quant-1);
		         ret=0;
			 quant++;
			}
		 sttime+=3600*24*31;
		}
	if (quant == 0)  {
		*save = alloc_archive();
		(*save)->num=0;
		ret=0;
		}

    	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int
spg741_events(struct device *dev, const char *from, const char *to, struct events **save)
{
	struct spg741_ctx *ctx;
	AnsLog alog;
	struct events *ev, *pev=NULL, *top=NULL;

	int ret=1,ret_fr=0,ret_to=0,i,j=0, ev_count=0;
	uint16_t len;
	char	code[120];
	uint	cod[MAX_EVENTS];
	static char ident_query[100], answer[1024];
	time_t tim,sttime,fntime,crtime;

	ctx = get_spec(dev);
	ctx->crc_flag = 1;

	struct 	tm *time_from=malloc (sizeof (*time_from));
	struct 	tm *time_to=malloc (sizeof (*time_to));
	struct 	tm *time_cur=malloc (sizeof (*time_cur));

	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	tim=time(&tim);
	localtime_r(&tim,time_cur);

	ret=checkdate (TYPE_EVENTS,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);

	if (ret==0)
	for (i=0; i<12; i++)
	    {
	     alog.quant_param=0;
	     if (-1 == generate_sequence (dev, TYPE_EVENTS, GET741_FLASH, 0x3894+i*56, 0, 88, 0, time_from, time_to, ident_query, &len)) ret = -1;
 	     ctx->send_query = ident_query;
 	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) ret = -1;
	     if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 98, &alog, 0)) ret = -1;
	     if (ret==0 && alog.quant_param>0)
	     for (j=0; j<=6; j++)
	        {
		 time_cur->tm_year=alog.data[0][j][1]+100;
		 time_cur->tm_mon=alog.data[0][j][2]-1;
		 time_cur->tm_mday=alog.data[0][j][3];
		 time_cur->tm_hour=alog.data[0][j][4];
		 time_cur->tm_min=alog.data[0][j][5];
		 time_cur->tm_sec=0;
		 crtime=mktime (time_cur);
		 if (crtime>=sttime && crtime<=fntime)	{
			//printf ("%ld > %ld <%ld\n",sttime,crtime,fntime);
	        	sprintf (alog.time[0][ev_count],"%d-%02d-%02d,%02d:%02d:00",alog.data[0][j][1]+2000,alog.data[0][j][2],alog.data[0][j][3],alog.data[0][j][4],alog.data[0][j][5]);
	        	spg741_get_ns (alog.data[0][j][6], code);
	        	cod[ev_count]=alog.data[0][j][6];
	        	snprintf (alog.data[0][ev_count],115,"%s",code);
	        	if (alog.data[0][j][7]==0x1) snprintf (alog.data[0][ev_count],100,"(%d) %s [+]\n",alog.data[0][j][6],code);
	        	if (alog.data[0][j][7]==0x0) snprintf (alog.data[0][ev_count],100,"(%d) %s [+]\n",alog.data[0][j][6],code);
	        	ev_count++;
	    	    }
	        }
	    buf_free(&dev->buf);
	    }
	ev = malloc(sizeof(*ev));
	if (!ev) {
		set_system_error("malloc");
	 	regfree(&ctx->re);
		ctx_zero(ctx);
		ret = -1;
	}

	for (i = 0; i < ev_count; i++) {
	        ev = malloc(sizeof(*ev));
	        if (!ev) {
			set_system_error("malloc");
			return -1;
			}
		ev->num = i;
		ev->event = cod[i];
		snprintf(ev->datetime, sizeof(ev->datetime),"%s",alog.time[0][i]);
		if (!dev->quiet)
			fprintf (stderr,"[%s] (%d) %s\n",ev->datetime,ev->event,alog.data[0][i]);
		ev->next = NULL;
		if(pev)
		    pev->next = ev;
		else
		    top = ev;
	        pev = ev;
	    }

	 if (ev_count == 0)  {
	        top = malloc(sizeof(*ev));
	        if (!ev) {
			set_system_error("malloc");
			return -1;
			}
		top->num = 0;
		top->event = 0;
		top->next = NULL;
		ret=0;
		}
	*save = top;

 	regfree(&ctx->re);
 	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int
spg741_send_msg(struct device *dev)
{
	struct spg741_ctx *ctx = get_spec(dev);
	return dev_write(dev, ctx->send_query, ctx->send_query_sz);
}

static int
spg741_parse_msg(struct device *dev, char *p, int psz)
{
 struct spg741_ctx *ctx;
 int ret=-1;
 char answer[1024];
 AnsLog alog;

 ctx = get_spec(dev);

// if (!dev->quiet)
//	fprintf(stderr, "\rParsing: %10d bytes", psz);

 ret=analyse_sequence (p, psz, answer, NO_ANALYSE, 0, &alog, 0);

 if (ret==1)
    {
	if (!dev->quiet)
		fprintf(stderr, "\nParsing ok\n");
	ret = 0;
	return ret;
	}
 usleep (10000);
 return 1;
}

static int
spg741_parse_crc_msg(struct device *dev, char *p, int psz)
{
 struct spg741_ctx *ctx;
 int ret;
 char answer[1024];
 AnsLog alog;

 ctx = get_spec(dev);

 if (!dev->quiet)
	fprintf(stderr, "\rParsing: %10d bytes", psz);

 ret=analyse_sequence (p, psz, answer, NO_ANALYSE, 0, &alog, 0);

 if (ret==1)
    {
	if (!dev->quiet)
		fprintf(stderr, "\nParsing ok\n");
	ret = 0;
	return ret;
	}
 usleep (10000);
 return 1;
}

static int
spg741_check_crc(struct device *dev, char *p, int psz)
{
 struct spg741_ctx *ctx;
 int ret;
 char answer[1024];
 AnsLog alog;

 ctx = get_spec(dev);

 if (ctx->crc_flag) {
		analyse_sequence (p, psz, answer, NO_ANALYSE, 0, &alog, 0);
		return alog.checksym; 
	} else {
		ret = 0;
	}
 return ret;
}

//static int  generate_sequence (struct device *dev, uint8_t type, uint8_t func, const char* padr, uint8_t nchan, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence);
// function generate send sequence for logika

static int 
generate_sequence (struct device *dev, uint8_t type, uint8_t func, uint16_t padr, uint8_t nchan, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence, uint16_t* len)
{
 char 		buffer[150];
 uint16_t 	i=0; 
 uint8_t	ks=0, startm=0, ln;

 switch (func)
	{
	// Запрос поиска записи в часовом архиве:  0x10 NT 0x48 гг мм дд чч КС 0x16
	case 	GET741_HOURS:	sprintf (sequence,"%c%c%c%c%c%c%c",DLE,dev->devaddr,func,to->tm_year,to->tm_mon+1,to->tm_mday,to->tm_hour);
				ln=7; startm=0;
				break;
	case 	GET741_DAYS:	sprintf (sequence,"%c%c%c%c%c%c%c",DLE,dev->devaddr,func,to->tm_year,to->tm_mon+1,to->tm_mday,ZERO);
				ln=7; startm=0;
				break;
	case 	GET741_MONTHS:	sprintf (sequence,"%c%c%c%c%c%c%c",DLE,dev->devaddr,func,to->tm_year,to->tm_mon+1,ZERO,ZERO);
				ln=7; startm=0;
				break;
        // Запрос чтения ОЗУ: 0x10 NT 0x52 А1 А0 КБ 0x00 КС 0x16
	case 	GET741_RAM:	sprintf (sequence,"%c%c%c%c%c%c%c",DLE,dev->devaddr,func,(uint8_t)((padr)%256),(uint8_t)((padr)/256),no,ZERO);
				ln=7; startm=0;
				break;
	case 	SET741_PARAM:	if (type==0)	{
				    sprintf (sequence,"%c%c%c%c%c%c%c",DLE,dev->devaddr,func,(uint8_t)((padr)%256),(uint8_t)((padr)/256),ZERO,ZERO);
				    ln=7; startm=0;
				    }
				else	{
					sprintf (buffer,"%c%c%c%s",DLE,dev->devaddr,func,sequence);
					ln=strlen(buffer); startm=0;
					sprintf (sequence,"%s",buffer);
				    }
				break;
	// Запрос чтения FLASH-памяти: 0x10 NT 0x45 N1 N0 K 0x00 КС 0x16
	case 	GET741_FLASH:	if (npipe==88) sprintf (sequence,"%c%c%c%c%c%c%c",DLE,dev->devaddr,func,(uint8_t)((padr/64)%256),(uint8_t)((padr/64)/256),0x1,ZERO);
				else sprintf (sequence,"%c%c%c%c%c%c%c",DLE,dev->devaddr,func,(uint8_t)(8+padr/4),(uint8_t)(0),1,ZERO);
				ln=7; startm=0;
				break;
	case	START_EXCHANGE:	for (i=0;i<16; i++) buffer[i]=0xff; buffer[16]=0;
				sprintf (sequence,"%s%c%c%c%c%c%c%c",buffer,DLE,dev->devaddr,func,ZERO,ZERO,ZERO,ZERO);
				ln=7+16; startm = 16;
				break;
	case	GET741_PARAM:
	default:		return	-1;
	}
 ks = calc_bcc ((uint8_t *)sequence+1+startm, ln-1-startm);
 sequence[ln]=(uint8_t)(ks);
 sequence[ln+1]=UK;
 *len=ln+2;
 return 0;
}

//----------------------------------------------------------------------------------------
static int 
analyse_sequence (char* dats, uint len, char* answer, uint8_t analyse, uint8_t id, AnsLog *alog, uint16_t padr)
{
 char 	dat[1000]; 
 uint16_t 	i=0,start=0, startm=0, cntNS=0, no=0, j=0, data_len=0;
 if (len>1000) 	return -1;
 memcpy (dat,dats,len);
 alog->checksym = 0;
// if (analyse==ANALYSE) 
 //for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)dats[i],(uint8_t)dats[i]);
 i=0;
 while (i<len)
	{
	 if (dat[i]==DLE && (uint8_t)dat[i+1]<50 && !start)
		{
		 alog->from = (uint8_t)dat[i+1];
		 alog->func=(uint8_t)dat[i+2];
		 startm=i+1; i=i+3; start=1;
		 //if (analyse==ANALYSE) printf ("from=%d func=%d (st=%d)\n",alog->from,alog->func,startm);
		}
	 if (dat[i]==DLE && dat[i+2]==0x21 && dat[i+3]==0x3)
		{
		 //fprintf (stderr,"no data\n");
		 return -1;
		}
	 if (i==len-1 && (dat[i]==UK && dat[i+1]!=UK) && ((id<98) || ((id>=98) && (i-2-startm>62))))
		{
 		 uint8_t ks=calc_bcc ((uint8_t *)dat+startm,i-startm-1);
		 data_len=i-1-3;
		 if (analyse==NO_ANALYSE)
		    {
			//fprintf (stderr,"re=(%d)%d %d\n",i,(uint8_t)ks,(uint8_t)dat[i-1]);
			if ((uint8_t)ks==(uint8_t)dat[i-1])
				return 	1;
			else	return	-1;
			}
		 if (((uint8_t)ks==(uint8_t)dat[i-1]) || (i==67 && len==68)) 
			{
			 alog->checksym = 1;

		 	 if (alog->func==START_EXCHANGE)
				{
				 memcpy (alog->data[id][no],dat+startm+2,3);
				 alog->data[id][no][3]=0;
				 strcpy (alog->time[id][no],"");
				 alog->quant_param = 1;
				}
			 if (alog->func==SET741_PARAM)
				{
				 sprintf (alog->data[0][0],"ok");
				 //fprintf (stderr,"write enable\n");
				}
			 if (alog->func==GET741_FLASH)
				{
				 if (id<98)		// Flash (param)
					{
					 memcpy (alog->data[id][no],(dat+startm+4+2+(id%4)*16),8);
					 alog->data[id][no][8]=0;
					 strcpy (alog->time[id][no],"");
					 alog->quant_param = 1;
					 
					 //for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)dats[i],(uint8_t)dats[i]);
					 // fprintf (stderr,"[flash] [%s] (%d/%d)\n",alog->data[id][no],id,no);
					}
				 if (id>=98)		// Flash (NS)
					{
					 for (j=0; j<7; j++)	{
					    memcpy (alog->data[0][j],(dat+startm+6+j*8),8);
					    alog->quant_param = cntNS;
					    //printf ("cntNS = [%d] [%x %x %x %x]\n",cntNS,alog->data[0][j][0],alog->data[0][j][1],alog->data[0][j][2],alog->data[0][j][3]);
					    cntNS++;
					    }
					}
				}
			 if (alog->func==GET741_RAM)
				{
				 memcpy (alog->data[id][no],(dat+startm+2),data_len);
				 //fprintf (stderr,"[RAM] [%s][%d]\n",alog->data[id][no],data_len);
				 return 1;
				}
			 if (alog->func==GET741_HOURS || alog->func==GET741_DAYS || alog->func==GET741_MONTHS)
			    {
				memcpy (alog->data[id][alog->quant_param],(dat+startm+2+padr),4);
				//fprintf (stderr,"GET (%x %x %x %x)\n",alog->data[id][alog->quant_param][0],alog->data[id][alog->quant_param][1],alog->data[id][alog->quant_param][2],alog->data[id][alog->quant_param][3]);
				alog->quant_param += 1;
			    }
			}
		 else 
			{
			 alog->checksym = 0;
			 for (j=0;j<len;j++) fprintf (stderr,"[%d] [%d] = 0x%x [%c]\n",padr,j,(uint8_t)dat[j],(uint8_t)dat[j]);
			 //fprintf (stderr,"wrong checksum alog.checksym=%d(rec=%x,must=%x) at pos=%d\n",alog->checksym,ks,(uint8_t)dat[i-1],i);
			}
		 if (id<0x189) return 1;
		}
	 i++;
	}
 if (id>=98) return 1;
 else  return 0;
}
//----------------------------------------------------------------------------------------
double cIEEE754toFloat (char *Data)
{
	uint8_t sign;
	int	exp,j;
	double res=0,zn=0.5, tmp;
	uint8_t mask;
	uint16_t i;
	if (*(Data+2)&0x80) sign=1; else sign=0;
	exp = ((*(Data+3)&0xff))-127;
	for (j=2;j>=0;j--)
    	    {
            mask = 0x80;
	    for (i=0;i<=7;i++)
	    	{
	    	 if (j==2&&i==0) {res = res+1; mask = mask/2;}
	    	 else {
	    		res = (*(Data+j)&mask)*zn/mask + res;
	    		mask = mask/2; zn=zn/2;
	    	    }
	    	}
	    }
	res = res * pow (2,exp);
	tmp = 1*pow (10,-15);
	if (res<tmp) res=0;
	if (sign) res = -res;
	return res;
}
//----------------------------------------------------------------------------------------
static int  checkdate (uint8_t	type, int ret_fr, int ret_to, struct tm* time_from, struct tm* time_to, struct tm* time_cur, time_t *tim, time_t *sttime, time_t *fntime)
{
	if (ret_fr<6) ret_fr=-1;
	if (ret_to<6) ret_to=-1;

	if (ret_fr==-1 && ret_to==6)	{
		localtime_r(tim,time_from);
		time_from->tm_year=time_cur->tm_year;
		time_from->tm_mon=time_cur->tm_mon;
		time_from->tm_mday=time_cur->tm_mday;
		time_from->tm_hour=time_cur->tm_hour;
		time_to->tm_year-=1900;
		time_to->tm_mon-=1;
		*sttime=mktime (time_from);
		*fntime=mktime (time_to);
		if (type==TYPE_HOURS) *sttime-=3600*24*45;
		if (type==TYPE_DAYS) *sttime-=3600*24*365;
		if (type==TYPE_MONTH) *sttime-=3600*24*365*2;
		if (type==TYPE_EVENTS) *sttime-=3600*24*365*5;
    		//fprintf (stderr,"[1]%ld - %ld - %ld\n",*sttime,*fntime,*tim);
    		return 0;
		}
	if (ret_fr==6 && ret_to==-1)	{
		localtime_r(tim,time_to);
		time_to->tm_year=time_cur->tm_year;
		time_to->tm_mon=time_cur->tm_mon;
		time_to->tm_mday=time_cur->tm_mday;
		time_to->tm_hour=time_cur->tm_hour;
		time_from->tm_year-=1900;
		time_from->tm_mon-=1;
		*sttime=mktime (time_from);
		*fntime=mktime (time_to);
    		//fprintf (stderr,"[2]%ld - %ld - %ld\n",*sttime,*fntime,*tim);
    		return 0;
		}
	if (ret_fr==-1 && ret_to==-1)	{
		localtime_r(tim,time_from);
		localtime_r(tim,time_to);
		time_from->tm_year=time_cur->tm_year;
		time_from->tm_mon=time_cur->tm_mon;
		time_from->tm_mday=time_cur->tm_mday;
		time_from->tm_hour=time_cur->tm_hour;
		time_to->tm_year=time_cur->tm_year;
		time_to->tm_mon=time_cur->tm_mon;
		time_to->tm_mday=time_cur->tm_mday;
		time_to->tm_hour=time_cur->tm_hour;
		*sttime=mktime (time_from);
		*fntime=mktime (time_to);
		if (type==TYPE_HOURS) *sttime-=3600*24*45;
		if (type==TYPE_DAYS) *sttime-=3600*24*365;
		if (type==TYPE_MONTH) *sttime-=3600*24*365;
		if (type==TYPE_EVENTS) *sttime-=3600*24*365*5;
    		//fprintf (stderr,"[3]%ld - %ld - %ld\n",*sttime,*fntime,*tim);
		return 0;
		}
	if (ret_fr==6 && ret_to==6)	{
		time_to->tm_year-=1900;
		time_from->tm_year-=1900;
		time_to->tm_mon-=1;
		time_from->tm_mon-=1;
		*sttime=mktime (time_from);
		*fntime=mktime (time_to);
		//fprintf (stderr,"[4]%ld - %ld - %ld\n",*sttime,*fntime,*tim);
		return 0;
		}
	return -1;
}
