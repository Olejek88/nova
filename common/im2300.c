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

#include "im2300.h"
#include <wchar.h>
#include <locale.h>
#include <math.h>

struct im2300_ctx {

	char *send_query;
	int  send_query_sz;

	regex_t re;
	char crc_flag;
	uint8_t crc;
};

#define get_spec(dev)	((struct im2300_ctx *)((dev)->spec))
                                     
static int  im2300_init(struct device *);
static void im2300_free(struct device *);
static int  im2300_get(struct device *, int, char **);
static int  im2300_set(struct device *, int, char *, char **);

static int  im2300_parse_msg(struct device *, char *, int);
static int  im2300_parse_crc_msg(struct device *, char *, int);
static int  im2300_check_crc(struct device *, char *, int);
static int  im2300_send_msg(struct device *);

static int  im2300_date(struct device *dev, const char *, char **);
static int  im2300_h_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  im2300_m_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  im2300_d_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  im2300_events(struct device *, const char *, const char *,
				struct events **);
static int  im2300_get_string_param(struct device *, uint16_t, char **, uint8_t, uint16_t);
static int  im2300_set_string_param(struct device *, uint16_t, char *, uint8_t, uint16_t);

static int  generate_sequence (struct device *dev, uint8_t type, uint8_t func, uint16_t padr, uint8_t nchan, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence, uint16_t* len);
static int  analyse_sequence (char* dats, uint len, char* answer, uint8_t analyse, uint8_t id, AnsLog *alog, uint16_t padr);

static int  checkdate (uint8_t	type, int ret_fr, int ret_to, struct tm* time_from, struct tm* time_to, struct tm* time_cur, time_t *tim, time_t *sttime, time_t *fntime);

static	uint8_t	getChannelID (uint8_t dt, uint16_t *no, uint16_t *type);

static int  im2300_date(struct device *dev, const char *ret, char **save)
{
	int	rt=0;
	struct 	tm *time_to=malloc (sizeof (struct tm));

	if (strlen (ret)==0)
		rt = im2300_get_string_param(dev, 120, save, TYPE_CONST, IM2300_TYPE_DATE);
	else	{
		 rt = im2300_set_string_param(dev, 10, (char *)ret, WRITE_DATE, 0);
		 *save=malloc (10);
		 if (rt==0) sprintf (*save,"ok");
		 else sprintf (*save,"error");
		}
	free	(time_to);
	return	rt;
}

/*
static int  im2300_get_date(struct device *dev, char **save)
{
	int	rt=0;
	rt = im2300_get_string_param(dev, 0xf3, save, TYPE_CURRENTS, 0);
	return	rt;
}

static int  im2300_set_date(struct device *dev, char *ret, char **save)
{
	int	rt=0;
	rt = im2300_set_string_param(dev, 0xf3, ret, TYPE_CURRENTS, 0);
	return	rt;
}*/

static uint8_t
BCD (uint8_t dat)
{
    uint8_t data=0;
    data=((dat&0xf0)>>4)*10+(dat&0xf);
    return	data;
}
static uint8_t
BCH (uint8_t dat)
{
    uint8_t data=0;
    data=(dat/10)*16+dat%10;
    return	data;
}

static void
ctx_zero(struct im2300_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

static int
im2300_get_ident(struct device *dev)
{
	static char ident_query[100];
	struct 	im2300_ctx *ctx;
	char 	answer[100];
	int 		ret;
	uint16_t	len;
	AnsLog	alog;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	ctx = get_spec(dev);

	generate_sequence (dev, 0, READ_DATETIME, 0, 0, 1, 8, time_from, time_to, ident_query, &len);
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	dev_query(dev);
	buf_free(&dev->buf);

	generate_sequence (dev, 0, READ_CONFIGURATION, 0, 0, 1, 8, time_from, time_to, ident_query, &len);
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, READ_CONFIGURATION, &alog, 0)) goto out;
	buf_free(&dev->buf);
	devlog(dev, "\tFound IM2300: %s Extended archives: %d\n", im2300_name,ARCHIVES_DM);
	
	generate_sequence (dev, 0, READ_PASS, 0, 0, 1, 8, time_from, time_to, ident_query, &len);
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, READ_PASS, &alog, 0)) goto out;
	buf_free(&dev->buf);	

	generate_sequence (dev, 0, READ_DATETIME, 0, 0, 1, 8, time_from, time_to, ident_query, &len);
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	dev_query(dev);
	buf_free(&dev->buf);

	ret = 0;
//free_regex:
	regfree(&ctx->re);
out:
	ctx_zero(ctx);
	buf_free(&dev->buf);
	free (time_from);
	free (time_to);
	return ret;
}

static int
im2300_get_string_param(struct device *dev, uint16_t addr, char **save, uint8_t type, uint16_t padr)
{
	struct 	im2300_ctx *ctx;
	static char ident_query[100],	answer[1024];
	void 	*fnsave;
	int 	ret;
	uint16_t	len,i,types;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	char	*value_string;
	AnsLog	alog;

	time_t tim;
	tim=time(&tim);			// default values
	localtime_r(&tim,time_from); 	// get current system time
	localtime_r(&tim,time_to); 	// get current system time

	ctx = get_spec(dev);
	ret = -1;

	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = im2300_parse_crc_msg;
	wait_len=395;
    
	switch (type)
		{
		 case TYPE_CURRENTS: 
				wait_len=0;
	    			if (-1 == generate_sequence (dev, 0, READ_CURRENTS, addr, 0, 1, 8, time_from, time_to, ident_query, &len)) goto out;
	    			types=READ_CURRENTS;
	    			break;
		 case TYPE_CONST:
				if (addr!=120)	{
	    		    		if (-1 == generate_sequence (dev, 0, READ_CONSTANT, addr, 0, 1, 8, time_from, time_to, ident_query, &len)) goto out;
	    		    		types=READ_CONSTANT;
	    			    }
				else	{
					wait_len=8;
					if (-1 == generate_sequence (dev, 0, READ_DATETIME, addr, 0, 1, 8, time_from, time_to, ident_query, &len)) goto out;
					types=READ_DATETIME;
				    }
				break;
		 default: 	goto out;
	    }
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, types, &alog, 0)) goto out;
	buf_free(&dev->buf);

	if (!dev->quiet)
	    devlog(dev, "RECV: [%x %x %x %x]\n", (uint8_t)alog.data[0][0][0], (uint8_t)alog.data[0][0][1], (uint8_t)alog.data[0][0][2], (uint8_t)alog.data[0][0][3]);

	switch	(padr)
		{
		 case	IM2300_TYPE_DATE:
	 		        value_string=malloc (100);
    				snprintf (value_string,90,"%s",alog.time[0][0]);
    				*save=value_string;
				ret = 0;
				break;
		 case	IM2300_TYPE_NOTDEFINED:	break;
		 case	IM2300_TYPE_FLOAT:
				if (type==TYPE_CURRENTS)
				    {
				     for (i=0; i<IM2300_MAX_DATA; i++)	{
				     //printf ("%d/%d %d %d\n",i,ARRAY_SIZE(currentsIM),addr,currentsIM[i].id);
				     if (addr == currentsIM[i].id) {
					    value_string=malloc (100);
					    snprintf (value_string,100,"%s",alog.data[currentsIM[i].id][0]);
					    *save=value_string;
					    ret=0;
					    }
					}
				    }
				if (type==TYPE_CONST)
				    {
				     value_string=malloc (100);
				     snprintf (value_string,100,"%s",alog.data[0][addr]);
				     *save=value_string;
				     ret=0;
				    }

				break;
		 default:	goto out;
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
im2300_set_string_param(struct device *dev, uint16_t addr, char *save, uint8_t type, uint16_t padr)
{
	struct im2300_ctx *ctx;
	static char 	ident_query[500],constants[500],answer[1024];
	void 		*fnsave;
	int 		ret;
	uint16_t	len,i;
	uint8_t		crc=0;
	float		value=0;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	AnsLog	alog;
	ctx = get_spec(dev);
	ret = -1;

	time_t tim;
	tim=time(&tim);			// default values
	localtime_r(&tim,time_from); 	// get current system time
	localtime_r(&tim,time_to); 	// get current system time

	if (type==WRITE_DATE)	{
	     if (-1 == generate_sequence (dev, 0, WRITE_DATE, addr, 0, 1, 8, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     wait_len=1;
	     dev_query(dev);
	     buf_free(&dev->buf);

	     sscanf(save,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
	     ident_query[0]=0;
	     ident_query[1]=BCH(time_to->tm_sec);
	     ident_query[2]=BCH(time_to->tm_min);
	     ident_query[3]=BCH(time_to->tm_hour);
	     ident_query[4]=BCH(time_to->tm_mday)+0x40*(time_to->tm_year%4);
	     ident_query[5]=BCH(time_to->tm_mon);
	     for (i=0;i<6;i++)
		crc+=(uint8_t)ident_query[i];
	     ident_query[6]=crc;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = 7;
	     wait_len=1;
	     if (-1 == dev_query(dev)) goto out;
	     if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, NO_ANALYSE, type, &alog, 0)) goto out;
	     //printf ("%x %x\n",dev->buf.p[0],crc);
	     if ((uint8_t)dev->buf.p[0]==(uint8_t)crc) ret=0;
	     buf_free(&dev->buf);
	    }
	else
	    {
	     wait_len=395;
    	     if (-1 == generate_sequence (dev, 0, READ_CONSTANT, addr, 0, 1, 8, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) goto out;
	     //	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, READ_CONSTANT, &alog, 0)) goto out;
	     //	for (i=0;i<IM2300_MAX_PARAMS;i++)
	     //	     printf ("bf[%d|%d] %s | %s [%x %x %x %x %x %x]\n",i,i*6,alog.data[0][i],alog.time[0][i],dev->buf.p[i*6],dev->buf.p[i*6+1],dev->buf.p[i*6+2],dev->buf.p[i*6+3],dev->buf.p[i*6+4],dev->buf.p[i*6+5]);
	     sscanf (save,"%f",&value);
	     value*=2;
	     memcpy	(dev->buf.p+2+addr*6,&value,4);
	     memcpy (constants,dev->buf.p,384);
	     for (i=0;i<384;i++)
		crc+=(uint8_t)constants[i];
	     constants[384]=crc;
	     //	for (i=0;i<IM2300_MAX_PARAMS;i++)
	     //	     printf ("af[%d|%d] %s | %s [%x %x %x %x %x %x]\n",i,i*6,alog.data[0][i],alog.time[0][i],dev->buf.p[i*6],dev->buf.p[i*6+1],dev->buf.p[i*6+2],dev->buf.p[i*6+3],dev->buf.p[i*6+4],dev->buf.p[i*6+5]);	     	
	     buf_free(&dev->buf);

    	     if (-1 == generate_sequence (dev, 0, WRITE_CONSTS, addr, 0, 1, 8, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     wait_len=1;
	     dev_query(dev);
	     buf_free(&dev->buf);

	     len=385;
	     ctx->send_query = constants;
	     ctx->send_query_sz = len;
	     wait_len=1;
	     if (-1 == dev_query(dev)) goto out;
	     if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, NO_ANALYSE, type, &alog, 0)) goto out;
	     //printf ("%x %x\n",dev->buf.p[0],crc);
	     if ((uint8_t)dev->buf.p[0]==(uint8_t)crc) ret=0;
	     buf_free(&dev->buf);
	    }
	/* save previous handler */
	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = im2300_parse_crc_msg;

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
static struct vk_operations im2300_opers = {
	.init	= im2300_init,
	.free	= im2300_free,
	.get	= im2300_get,
	.set	= im2300_set,
	.date	= im2300_date,

	.send_msg  = im2300_send_msg,
	.parse_msg = im2300_parse_msg,
	.check_crc = im2300_check_crc,

	.h_archiv = im2300_h_archiv,
	.m_archiv = im2300_m_archiv,
	.d_archiv = im2300_d_archiv,

	.events = im2300_events
};

void
im2300_get_operations(struct vk_operations **p_opers)
{
	*p_opers = &im2300_opers;
}

static int
im2300_init(struct device *dev)
{
	struct im2300_ctx *spec;
	int ret=-1;

	spec = calloc(1, sizeof(*spec));

	if (!spec) {
		set_system_error("calloc");
		ret = -1;
	} else {
		dev->spec = spec;
		ret =   (-1 == im2300_get_ident(dev));
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
im2300_free(struct device *dev)
{
	struct im2300_ctx *spec = get_spec(dev);

	free(spec);
	dev->spec = NULL;
}


static int
im2300_get_ns (int no, char* code)
{
    //int i;
    return -1;
}

static int
im2300_get(struct device *dev, int param, char **ret)
{
	int i=0, rv=0;

	rv = im2300_get_string_param(dev, 0, ret, TYPE_CONST, IM2300_TYPE_FLOAT);
	for (i=0; i<ARRAY_SIZE(constIM); i++)	{
	    //printf ("%d/%d %d %d\n",i,ARRAY_SIZE(currentsIM),param,currentsIM[i].no);
	    if (param == constIM[i].no) {
		rv = im2300_get_string_param(dev, constIM[i].adr, ret, TYPE_CONST, IM2300_TYPE_FLOAT);
    		return rv;
    	    }
	}

	for (i=0; i<ARRAY_SIZE(currentsIM); i++)	{
	    //printf ("%d/%d %d %d\n",i,ARRAY_SIZE(currentsIM),param,currentsIM[i].no);
	    if (param == currentsIM[i].no) {
		rv = im2300_get_string_param(dev, currentsIM[i].id, ret, TYPE_CURRENTS, IM2300_TYPE_FLOAT);
    		return rv;
    	    }
	}
	return -1;
}

static int
im2300_set(struct device *dev, int param, char* ret, char** save)
{
	int i=0, rv=-1;
	
	for (i=0; i<ARRAY_SIZE(constIM); i++)
		if (constIM[i].no==param) {
		    rv = im2300_set_string_param(dev, constIM[i].adr, ret, 0, param);
		    *save=malloc (10);
    		    if (rv==0)
	    	        snprintf (*save,10,"ok");
		    else
			snprintf (*save,10,"error");
		    }

	return rv;
}

static int
make_archive(struct device *dev, int no, AnsLog* alog, int type, struct archive **save)
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

			 for (np=0; np<ARRAY_SIZE(archivesIM); np++)
				{
    				 snprintf(ptr->datetime, sizeof(ptr->datetime),"%s",alog->time[archivesIM[np].id][i]);
    				 ptr->params[archivesIM[np].id] = atof (alog->data[np][i]);
				 //if (!dev->quiet)
				 //    fprintf (stderr,"[%s] %f\n",ptr->datetime,ptr->params[archivesIM[np].id]);
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
im2300_h_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct im2300_ctx *ctx;
	AnsLog alog;
	int ret,ret_fr,ret_to,lblock=0;
	uint16_t len,try;
	static char ident_query[100], answer[1024];
	time_t tim;
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
	while (1)
		{
	         //entime=sttime+3600*1;
		 try=3;
    		 while (try--)	{
	    		buf_free(&dev->buf);
	    		if (!lblock)
	    		    {
			     if (-1 == generate_sequence (dev, TYPE_HOURS, READ_ARCHIVES, 0, 0, 1, no, time_from, time_to, ident_query, &len)) continue;
	 		     ctx->send_query = ident_query;
	 		     ctx->send_query_sz = len;
	 		    }
	 		else	{
	 		     ident_query[0]=lblock-1;
	 		     len=1;
	 		     ctx->send_query = ident_query;
	 		     ctx->send_query_sz = len;	 		     
	 		    }
			if (-1 == dev_query(dev)) continue;
			if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, READ_ARCHIVES, &alog, alog.quant_param)) continue;

			lblock++;
			break;
		    }
		 if (lblock>IM2300_MAX_BLOCK) break;
		 if (try==0) break;
		 //quant++;
		 //sttime+=3600;
		}
	if (alog.quant_param == 0)  {
		*save=NULL;
		ret=0;
		}
	else
    		ret = make_archive(dev, no, &alog, TYPE_HOURS, save);
	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int 
im2300_d_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct im2300_ctx *ctx;
	AnsLog alog;
	int ret,ret_fr,ret_to,lblock=0;
	uint16_t len,try;
	static char ident_query[100], answer[1024];
	time_t tim,sttime,fntime;
	ctx = get_spec(dev);
	ctx->crc_flag = 1;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);

	struct 	tm *time_to=malloc (sizeof (struct tm));
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	tim=time(&tim);
	localtime_r(&tim,time_cur);

	ret=checkdate (TYPE_DAYS,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
	alog.quant_param=0;

	if (sttime && fntime && ret==0)
	while (ARCHIVES_DM)
		{
		 try=3;
    		 while (try--)	{
	    		buf_free(&dev->buf);
	    		if (!lblock)
	    		    {
			     if (-1 == generate_sequence (dev, TYPE_DAYS, READ_DAYS, 0, 0, 1, no, time_from, time_to, ident_query, &len)) continue;
	 		     ctx->send_query = ident_query;
	 		     ctx->send_query_sz = len;
	 		    }
	 		else	{
	 		     ident_query[0]=lblock-1;
	 		     len=1;
	 		     ctx->send_query = ident_query;
	 		     ctx->send_query_sz = len;	 		     
	 		    }
			if (-1 == dev_query(dev)) continue;
			if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, READ_ARCHIVES, &alog, alog.quant_param)) continue;

			lblock++;
			break;
		    }
		 if (try==0) break;
		}
	if (alog.quant_param == 0)  {
		*save=NULL;
		ret=0;
		}
	else
	    ret = make_archive(dev, no, &alog, TYPE_DAYS, save);
	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int 
im2300_m_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct im2300_ctx *ctx;
	AnsLog alog;
	int ret,ret_fr,ret_to,lblock=0;
	uint16_t len,try;
	static char ident_query[100], answer[1024];
	time_t tim,sttime,fntime;
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
	while (ARCHIVES_DM)
		{
		 try=3;
    		 while (try--)	{
	    		buf_free(&dev->buf);
	    		if (!lblock)
	    		    {
			     if (-1 == generate_sequence (dev, TYPE_MONTH, READ_MONTH, 0, 0, 1, no, time_from, time_to, ident_query, &len)) continue;
	 		     ctx->send_query = ident_query;
	 		     ctx->send_query_sz = len;
	 		    }
	 		else	{
	 		     ident_query[0]=lblock-1;
	 		     len=1;
	 		     ctx->send_query = ident_query;
	 		     ctx->send_query_sz = len;	 		     
	 		    }
			if (-1 == dev_query(dev)) continue;
			if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, READ_ARCHIVES, &alog, alog.quant_param)) continue;

			lblock++;
			break;
		    }
		 if (try==0) break;
		}
	if (alog.quant_param == 0)  {
		*save=NULL;
		ret=0;
		}
	else
		ret = make_archive(dev, no, &alog, TYPE_MONTH, save);
	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int
im2300_events(struct device *dev, const char *from, const char *to, struct events **save)
{
	struct im2300_ctx *ctx;
	AnsLog alog;
	struct events *ev, *pev=NULL, *top=NULL;

	int ret=1,ret_fr=0,ret_to=0,i,j=0, ev_count=0;
	uint16_t len,try;
	//char	code[120];
	//uint	cod[MAX_EVENTS];
	static char ident_query[100], answer[1024];
	time_t tim,sttime,fntime,crtime;

	ctx = get_spec(dev);
	ctx->crc_flag = 1;

	struct 	tm *time_from=malloc (sizeof (*time_from));
	struct 	tm *time_to=malloc (sizeof (*time_to));
	struct 	tm *time_cur=malloc (sizeof (*time_cur));

	ret = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
	ret = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	tim=time(&tim);
	localtime_r(&tim,time_cur);

	ret=checkdate (TYPE_EVENTS,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);

	try=5;
    	while (try--)	{
    	     buf_free(&dev->buf);
    	     if (-1 == generate_sequence (dev, TYPE_EVENTS, READ_EVENTS, 0x0, 0, 88, 0, time_from, time_to, ident_query, &len)) ret = -1;
 	     ctx->send_query = ident_query;
 	     ctx->send_query_sz = len;
 	     dev_query(dev);
	     if (dev->buf.len==704) break;
	    }
	    
	alog.quant_param=0;    
	if (ret==0)
	for (i=0; i<1; i++)
	    {	     
	     if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, READ_EVENTS, &alog, i)) ret = -1;
	     buf_free(&dev->buf);
	     for (j=0; j<alog.quant_param; j++)
		    {
			ret_to = sscanf(alog.time[0][j],"%d-%d-%d,%d:%d:%d",&time_cur->tm_year, &time_cur->tm_mon, &time_cur->tm_mday, &time_cur->tm_hour, &time_cur->tm_min, &time_cur->tm_sec);
			time_cur->tm_year-=1900;
			time_cur->tm_mon-=1;
			crtime=mktime (time_cur);		     
			//fprintf (stderr,"[%s]\n",alog.data[0][j]);
			//printf ("%d-%d-%d,%d:%d:%d",time_cur->tm_year, time_cur->tm_mon, time_cur->tm_mday, time_cur->tm_hour, time_cur->tm_min, time_cur->tm_sec);
			//fprintf (stderr,"[3][%s]%ld - %ld - %ld\n",alog.time[0][j],sttime,crtime,fntime);

			if (crtime>=sttime && crtime<=fntime)	{
	        	    alog.flag[j]=8;
	        	    //printf ("%d\n",alog.flag[j]);   
	    	    }
	        }
	     buf_free(&dev->buf);
	     
	     ident_query[0]=i;
     	     ctx->send_query = ident_query;
 	     ctx->send_query_sz = 1;
	     if (-1 == dev_query(dev)) break;
	    }

	ev = malloc(sizeof(*ev));
	if (!ev) {
		set_system_error("malloc");
	 	regfree(&ctx->re);
		ctx_zero(ctx);
		ret = -1;
	}
	ev_count=alog.quant_param;
	for (i = 0; i < alog.quant_param; i++)
	if (alog.flag[i]==8) {
	        ev = malloc(sizeof(*ev));
	        if (!ev) {
			set_system_error("malloc");
			return -1;
			}
		ev->num = i;
		ev->event = atoi(alog.data[0][i]);
		snprintf(ev->datetime, sizeof(ev->datetime),"%s",alog.time[0][i]);

		for (j=0;j<sizeof(nscodeIM)/sizeof(nscodeIM[0]);j++)
		    {
		     //printf ("%d %d\n",nscodeIM[j].kod,ev->event);
		     if (nscodeIM[j].kod==ev->event)
			if (!dev->quiet)
			    fprintf (stderr,"[%s] (%d) %s\n",ev->datetime,ev->event,nscodeIM[j].name);
		    }
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
im2300_send_msg(struct device *dev)
{
	struct im2300_ctx *ctx = get_spec(dev);
	return dev_write(dev, ctx->send_query, ctx->send_query_sz);
}

static int
im2300_parse_msg(struct device *dev, char *p, int psz)
{
 //struct im2300_ctx *ctx;
 int ret=-1;
 char answer[1024];
 AnsLog alog;

 //ctx = get_spec(dev);
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
im2300_parse_crc_msg(struct device *dev, char *p, int psz)
{
 //struct im2300_ctx *ctx;
 int ret;
 char answer[1024];
 AnsLog alog;
 //ctx = get_spec(dev);

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
im2300_check_crc(struct device *dev, char *p, int psz)
{
 struct im2300_ctx *ctx;
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
 //uint8_t	startm=0, ln;
 switch (func)
	{
	case 	READ_CURRENTS:	
	case	READ_CONSTANT:	
	case	WRITE_CONSTS:	
	case	WRITE_DATE:	
	case 	READ_ARCHIVES:
	case	READ_EVENTS:
	case 	READ_PASS:
	case 	READ_DAYS:
	case 	READ_MONTH:
	case	READ_CONFIGURATION:
	case	READ_DATETIME:	sprintf (sequence,"%c%c",dev->devaddr,func);
				//ln=2; startm=0;
				break;
	default:		return	-1;
	}
 *len=2;
 return 0;
}

//----------------------------------------------------------------------------------------
static int 
analyse_sequence (char* dats, uint len, char* answer, uint8_t analyse, uint8_t id, AnsLog *alog, uint16_t padr)
{
 char 		dat[1000]; 
 uint16_t 	i=0,ind=0,year;
 uint8_t	crc=0,idd,nblock=0;
 uint16_t	no=0,nl=0,type=0;
 time_t 	crtime;

 float		value;
 if (len>1000) 	return -1;
 memcpy (dat,dats,len);
 alog->checksym = 0;

 if (len>1)
 for (i=0;i<len-1;i++)
    crc+=dat[i];
 if (len==1) crc=dat[0];
 

 if (analyse==NO_ANALYSE)
    {
     //printf ("\nwait_len %d [%d]\n",len,wait_len);
     if (wait_len && len==wait_len)
        {
	 //printf ("\n%x [%d]=%x\n",crc,len-1,dat[len-1]);
	 wait_len=0;
         if (crc==(uint8_t)dat[len-1])
            {
	     return 1;
	    }
         else return 0;        
        }
     if (!wait_len)
     if (len==8 || len==17 || len==128 || len==327 || len==395 || len==704 || len==740 || len==978)
        {
	 //printf ("\n%x [%d]=%x\n",crc,len-1,dat[len-1]);
         wait_len=0;
         if (crc==(uint8_t)dat[len-1])
            {
	     return 1;
	    }
         else return 0;
        }
    }
 
 struct 	tm *time_cur=malloc (sizeof (struct tm));
 switch (id)
	{
	 case 	READ_CURRENTS:	//for (i=0;i<len;i+=5)
				//    printf ("[%d] %x %x %x %x %x [%f]\n",i,(uint8_t)dat[i],(uint8_t)dat[i+1],(uint8_t)dat[i+2],(uint8_t)dat[i+3],(uint8_t)dat[i+4],(*(float*)(dat+i)/2));
				for (i=0;i<IM2300_MAX_DATA;i++)
				if (currentsIM[i].id && currentsIM[i].id<ARCH_MAX_NPARAM)
				    {
				     if (currentsIM[i].type==IM2300_TYPE_FLOAT) value=(*(float*)(dat+i*5)/2);
				     if (currentsIM[i].type==IM2300_TYPE_INT) value=BCD(dat[3+i*5])*10000+BCD(dat[2+i*5])*100+BCD(dat[1+i*5])+BCD(dat[i*5])*0.01;
				     sprintf (alog->data[currentsIM[i].id][0],"%f",value);
				     //printf ("[cur]%d %d %s\n",currentsIM[i].id,currentsIM[i].no,alog->data[currentsIM[i].id][0]);
				    }
				//printf ("%x %x %x %x %x\n",dat[122],dat[123],dat[124],dat[125],dat[126]);	
				//sprintf (alog->time[0][0],"%02d-%02d-%d,%02d:%02d:%02d",BCD(dat[125]&0x3f),BCD(dat[126]),2013,BCD(dat[124]),BCD(dat[123]),BCD(dat[122]));
				//printf ("%s\n",alog->time[0][0]);
				break;
	 case 	READ_ARCHIVES:	

				while (nl<len)	{
    				    for (i=0;i<REGPARAMS;i++)
    				    if (archivesIM[i].id)
					{
				         value=*(float*)(dat+(1+i)*4+nblock*4*(REGPARAMS+1));
				         sprintf (alog->data[i][alog->quant_param],"%f",value);
				         ind=nblock*(REGPARAMS+1)*4;
			                 sprintf (alog->time[i][alog->quant_param],"%d-%02d-%02d,%02d:%02d:%02d",2012+(dat[ind+2]&0xC0)/0x40,BCD(dat[ind+3]&0x1f),BCD(dat[ind+2]&0x3f),BCD(dat[ind+1]),BCD(dat[ind]),0);			                 
				         //printf ("[arc][%d][%d] [%s] %s (%d)(%d)\n",alog->quant_param,archivesIM[i].id,alog->time[i][alog->quant_param],alog->data[i][alog->quant_param],(1+i)*4+4*nblock*(REGPARAMS+1),ind);
				        }
				     nl+=4*REGPARAMS+4;
				     if (nl>720) break;
	    			     sscanf(alog->time[0][alog->quant_param],"%d-%d-%d,%d:%d:%d",&time_cur->tm_year, &time_cur->tm_mon, &time_cur->tm_mday, &time_cur->tm_hour, &time_cur->tm_min, &time_cur->tm_sec);
	    			     time_cur->tm_year-=1900;
	    			     time_cur->tm_mon-=1;
	    			     crtime=mktime (time_cur);
	    			     if (crtime<=fntime && crtime>=sttime)
	    			    	    alog->quant_param++;
				     nblock++;
				    }
				break;
	 case	READ_CONSTANT:	//for (i=0;i<len;i+=6)
				//    printf ("[%d] %x %x %x %x %x %x\n",i,(uint8_t)dat[i],(uint8_t)dat[i+1],(uint8_t)dat[i+2],(uint8_t)dat[i+3],(uint8_t)dat[i+4],(uint8_t)dat[i+5]);
				for (i=0;i<IM2300_MAX_PARAMS;i++)
				    {
				     value=*(float*)(dat+i*6+2);
				     sprintf (alog->data[0][i],"%f",value/2);
				     sprintf (alog->time[0][i],"%d",dat[i*6]*256+dat[i*6+1]);
				     //printf ("[%d|%d] %s | %s [%x %x %x %x %x %x]\n",i,i*6,alog->data[0][i],alog->time[0][i],dat[i*6],dat[i*6+1],dat[i*6+2],dat[i*6+3],dat[i*6+4],dat[i*6+5]);
				    }
				break;
	 case	READ_EVENTS:	//for (i=0;i<len;i+=7)
				//    printf ("[%d] %x %x %x %x %x %x\n",i,(uint8_t)dat[i],(uint8_t)dat[i+1],(uint8_t)dat[i+2],(uint8_t)dat[i+3],(uint8_t)dat[i+4],(uint8_t)dat[i+5]);
				for (i=0;i<100;i++)
				    {	
				     year=2013;		
				     if (((dat[4]&0xC0)/0x40)==1) year=2013;
				     if (((dat[4]&0xC0)/0x40)==2) year=2010;
				     if (((dat[4]&0xC0)/0x40)==0) year=2012;
				     if (((dat[4]&0xC0)/0x40)==3) year=2011;

				     sprintf (alog->time[0][i+padr],"%d-%02d-%02d,%02d:%02d:%02d",year,BCD(dat[2+i*7]&0x1f),BCD(dat[3+i*7]&0x3f),BCD(dat[4+i*7]),BCD(dat[5+i*7]),BCD(dat[6+i*7]));
				     sprintf (alog->data[0][i+padr],"%x",dat[0+i*7]*256+dat[1+i*7]);				    
				     if (dat[1+i*7]>0)
				        alog->quant_param++;
				    }
				break;
	 case 	READ_PASS:	for (i=0;i<IM2300_MAX_DATA;i++)
				    {
				     //printf ("[%d] %x\n",i,dat[1+i*32]);
				     idd=getChannelID (dat[1+i*32],&no,&type);
				     if (idd<ARCH_MAX_NPARAM)	{
				         currentsIM[i].id=idd;
				         archivesIM[i].id=idd;
				         currentsIM[i].adr=i;
				         archivesIM[i].adr=i;
				         currentsIM[i].no=no;
				         archivesIM[i].no=no;
				         currentsIM[i].type=type;
				         archivesIM[i].type=type;
				         //fprintf (stderr,"[%x] %d|%d|%d\n",dat[1+i*32],idd,i,no);
				         REGPARAMS++;
				        }
				    }
				break;
	 case 	READ_DATETIME:	sprintf (alog->time[0][0],"%d-%02d-%02d,%02d:%02d:%02d",2012+(dat[4]&0xC0)/0x40,BCD(dat[5]&0x1f),BCD(dat[4]&0x3f),BCD(dat[3]),BCD(dat[2]),BCD(dat[1]));
				//printf ("%s\n",alog->time[0][0]);
				break;
	 case	READ_CONFIGURATION:
				sprintf (im2300_name,"%c%c-%d",dat[0],dat[1],dat[2]);
				//printf ("%s [%x %x %x]\n",im2300_name,dat[0],dat[1],dat[2]);
				ARCHIVES_DM=dat[10]&0x4;
				break;
	} 
 free (time_cur);
 return 0;
}
//----------------------------------------------------------------------------------------
uint8_t	getChannelID (uint8_t dt, uint16_t *no, uint16_t *type)
{
 uint8_t	kod=dt-PIPE;
 *type=IM2300_TYPE_INT;
 switch (kod)
    {    
     case 0x8:	*no=20;	*type=IM2300_TYPE_FLOAT; return ARCH_TAVG;	//T
     case 0x10:	*no=18; *type=IM2300_TYPE_FLOAT; return ARCH_PAVG;	//P
     case 0x28:	*no=16;	return ARCH_VWORK;	//Qo
     case 0x18:	*no=19; *type=IM2300_TYPE_FLOAT; return ARCH_DPAVG;	//dP
     case 0x30:	*no=21; return ARCH_VAGG_NORM;	//Go
//     case 0x40:	*no=20;	return 8;	//Gm
//     case 0x38:	*no=18; return 6;	//Qm
     case 0x48:	*no=12; return ARCH_VNORM;	//Gn
     case 0x50:	*no=23;	return ARCH_MAGG_NORM;	//Vn
     //case 0x68:	*no=504; 	return 1;	//tm
     case 0x88:	*no=28; *type=IM2300_TYPE_FLOAT; return ARCH_PBAR_AVG;	//Pbar
     //case 0x78:	*no=501;	return 1;	//ts
     case 0xC0:	*no=25; *type=IM2300_TYPE_FLOAT; return ARCH_HAVG;	//Ro
     case 0xC8:	*no=112; 	return 1;	//Me H2O
     default:	return 100;
    }
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
