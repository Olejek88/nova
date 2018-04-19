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

#include "spg742.h"
#include <wchar.h>
#include <locale.h>
#include <math.h>

/* */

struct spg742_ctx {

	char *send_query;
	int  send_query_sz;

	regex_t re;

	char crc_flag;

	uint16_t crc;
};

#define get_spec(dev)	((struct spg742_ctx *)((dev)->spec))
                                     
/* */
static int  spg742_init(struct device *);
static void spg742_free(struct device *);
static int  spg742_get(struct device *, int, char **);
static int  spg742_set(struct device *, int, char *, char **);

static int  spg742_parse_msg(struct device *, char *, int);
static int  spg742_parse_crc_msg(struct device *, char *, int);
static int  spg742_check_crc(struct device *, char *, int);
static int  spg742_send_msg(struct device *);

static int  spg742_date(struct device *dev, const char *, char **);
//static int  spg742_get_date(struct device *dev, char **save);
//static int  spg742_set_date(struct device *dev, char *ret, char **save);

static int  spg742_h_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  spg742_m_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  spg742_d_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  spg742_events(struct device *, const char *, const char *,
				struct events **);
static int  spg742_get_string_param(struct device *, uint16_t, char **, uint8_t, uint16_t);
static int  spg742_set_string_param(struct device *, uint16_t, char *, uint8_t, uint16_t);

static int  generate_sequence (struct device *dev, uint8_t type, uint8_t func, uint16_t padr, uint8_t nchan, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence, uint16_t* len);
static int  analyse_sequence (char* dats, uint len, char* answer, uint8_t analyse, uint8_t id, AnsLog *alog, uint16_t padr);

static int  checkdate (uint8_t	type, int ret_fr, int ret_to, struct tm* time_from, struct tm* time_to, struct tm* time_cur, time_t *tim, time_t *sttime, time_t *fntime);

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

static uint16_t
calc_bcc16(uint8_t *ptr, int n)
{
    uint16_t crc=0,j;

    while (n-- > 0)
	{
	 crc = crc ^ (int) *ptr++ << 8;
	 for (j=0;j<8;j++)
		{
		 if(crc&0x8000) crc = (crc << 1) ^ 0x1021;
		 else crc <<= 1;
		}
	}
    //crc=crc*256+crc/256;
//    printf ("%d %d\n",n,crc);
    return crc;
}


static int  spg742_date(struct device *dev, const char *ret, char **save)
{
	int	rt=0;
	char	date[30];
	struct 	tm *time_to=malloc (sizeof (struct tm));

	if (strlen (ret)==0)
		rt = spg742_get_string_param(dev, 1024, save, TYPE_CONST, 0);
	else	{
		 sscanf(ret,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
 		 snprintf (date,25,"%02d-%02d-%02d",time_to->tm_mday, time_to->tm_mon, time_to->tm_year-2000);
		 rt = spg742_set_string_param(dev, 3, (char *)date, TYPE_CONST, 0);
		 snprintf (date,25,"%02d-%02d-%02d", time_to->tm_hour, time_to->tm_min, time_to->tm_sec);
		 rt = spg742_set_string_param(dev, 4, (char *)date, TYPE_CONST, 0);
		 *save=malloc (10);
		 if (rt==0) sprintf (*save,"ok");
		 else sprintf (*save,"error");
		}
	free	(time_to);
	return	rt;
}

static void
ctx_zero(struct spg742_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

static int
spg742_get_ident(struct device *dev)
{
	static char ident_query[100];
	struct spg742_ctx *ctx;
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

	if (-1 == generate_sequence (dev, 0, GET742_FLASH, 29, 0, 0, 1, time_from, time_to, ident_query, &len)) goto free_regex;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto free_regex;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, spg_id, ANALYSE, 1, &alog, 8)) goto free_regex;
	buf_free(&dev->buf);
	sprintf (spg_id,"%s",alog.data[1][0]);
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
spg742_get_string_param(struct device *dev, uint16_t addr, char **save, uint8_t type, uint16_t padr)
{
	struct 	spg742_ctx *ctx;
	static char ident_query[100],	answer[1024], date[30];
	void 	*fnsave;
	int 	ret;
	uint16_t	len;
	float	temp=0;
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
	dev->opers->parse_msg = spg742_parse_crc_msg;

	if (type==TYPE_CURRENTS) {		
		if (padr==0)
    		    if (-1 == generate_sequence (dev, 0, M4_GET742_PARAM, addr, 0, 0, 4, time_from, time_to, ident_query, &len)) goto out;
		if (padr>0)
    		    if (-1 == generate_sequence (dev, 0, M4_GET742_PARAM, addr, 0, 1, 4, time_from, time_to, ident_query, &len)) goto out;

		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) goto out;
		if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, &alog, padr)) goto out;
		buf_free(&dev->buf);
		if (!dev->quiet)
		    devlog(dev, "RECV: [%x %x %x %x]\n", (uint8_t)alog.data[0][0][0], (uint8_t)alog.data[0][0][1], (uint8_t)alog.data[0][0][2], (uint8_t)alog.data[0][0][3]);
		if (padr>0)
			{
			 temp=*(float*)(alog.data[0][0]);
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
		if (addr==1024)
		    {
    		     if (-1 == generate_sequence (dev, 0, M4_GET742_PARAM, addr, 0, 0, 1, time_from, time_to, ident_query, &len)) goto out;
    		    }
    		else
    		    {
    		     if (-1 == generate_sequence (dev, 0, M4_GET742_PARAM, addr, 0, 0, 1, time_from, time_to, ident_query, &len)) goto out;
    		    }
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) goto out;
		if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, &alog, padr)) goto out;
		
		buf_free(&dev->buf);
		snprintf (value_string,100,"%s",alog.data[0][0]);
		//printf ("===%s\n",value_string);

		if (addr==0x400) {
			snprintf (date,20,"%s",alog.data[(addr)%4][0]);
			if (-1 == generate_sequence (dev, 0, M4_GET742_PARAM, addr+1, padr, 0, 1, time_from, time_to, ident_query, &len)) goto out;
			ctx->send_query = ident_query;
			ctx->send_query_sz = len;
			if (-1 == dev_query(dev)) goto out;
			if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, (addr+1)%4, &alog, padr)) goto out;
			buf_free(&dev->buf);
			snprintf (value_string,30,"%s,%s",date,alog.data[(addr+1)%4][0]);
			sscanf(value_string,"%d-%d-%d,%d-%d-%d",&time_to->tm_mday, &time_to->tm_mon, &time_to->tm_year, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
			snprintf (value_string,25,"%02d-%02d-%02d,%02d:%02d:%02d",time_to->tm_year+1900, time_to->tm_mon+1, time_to->tm_mday, time_to->tm_hour, time_to->tm_min, time_to->tm_sec);
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
spg742_set_string_param(struct device *dev, uint16_t addr, char *save, uint8_t type, uint16_t padr)
{
	struct spg742_ctx *ctx;
	static char ident_query[100],	answer[1024];
	void 		*fnsave;
	int 		ret;
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
	dev->opers->parse_msg = spg742_parse_crc_msg;

	if (addr==3 || addr==4)
	    {
	     if (addr==3) 
	        {
	         ident_query[0]=0x16;
		 ident_query[1]=8;
		 len=10;
		 memcpy (ident_query+2,save,8);
		 //addr=1025;
		}
	     if (addr==4) 
	        {
	         ident_query[0]=0x16;
		 ident_query[1]=8;
		 len=10;
		 memcpy (ident_query+2,save,8);
		 //addr=1024;
		}
	     if (-1 == generate_sequence (dev, 1, M4_SET742_PARAM, addr, padr, 0, 8, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) goto out;
	     if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, &alog, padr)) goto out;
	     buf_free(&dev->buf); 
	    }
	else
	    {
	     ident_query[0]=0x16;
	     ident_query[1]=strlen(save);
	     len=strlen(save)+2;
	     sprintf (ident_query+2,"%s",save);	
	     //sscanf (save,"%f",&value);
	     //memcpy	(ident_query+2,&value,4);	
	     if (-1 == generate_sequence (dev, 1, M4_SET742_PARAM, addr, padr, 0, 8, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) goto out;
	     if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, &alog, padr)) goto out;
	     buf_free(&dev->buf);
	    }
	if (!dev->quiet)
		    devlog(dev, "RECV: %s [%s]\n", alog.data[0][0],alog.time[0][0]);
	ret=0;

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
static struct vk_operations spg742_opers = {
	.init	= spg742_init,
	.free	= spg742_free,
	.get	= spg742_get,
	.set	= spg742_set,
	.date	= spg742_date,

	.send_msg  = spg742_send_msg,
	.parse_msg = spg742_parse_msg,
	.check_crc = spg742_check_crc,

	.h_archiv = spg742_h_archiv,
	.m_archiv = spg742_m_archiv,
	.d_archiv = spg742_d_archiv,

	.events = spg742_events
};

void
spg742_get_operations(struct vk_operations **p_opers)
{
	*p_opers = &spg742_opers;
}

static int
spg742_init(struct device *dev)
{
	struct spg742_ctx *spec;
	int ret=-1;

	spec = calloc(1, sizeof(*spec));

	if (!spec) {
		set_system_error("calloc");
		ret = -1;
	} else {
		dev->spec = spec;
		ret =   (-1 == spg742_get_ident(dev));
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
spg742_free(struct device *dev)
{
	struct spg742_ctx *spec = get_spec(dev);

	free(spec);
	dev->spec = NULL;
}


static int
spg742_get_ns (int no, char* code)
{
    int i;
    for (i=0; i<ARRAY_SIZE(nscode742); i++)
	if (no == nscode742[i].id)
	    {
	    snprintf (code,70,"%s",nscode742[i].name);
	    return 1;
	    }
    return -1;
}

static int
spg742_get(struct device *dev, int param, char **ret)
{
	int i=0, rv=0;

	for (i=0; i<ARRAY_SIZE(currents742); i++)
	if (param == currents742[i].no) {
	    rv = spg742_get_string_param(dev, currents742[i].adr, ret, TYPE_CURRENTS, currents742[i].type);
    	    return rv;
	}
	for (i=0; i<ARRAY_SIZE(const742); i++)
	if (param == const742[i].no)  {
    	    rv = spg742_get_string_param(dev, const742[i].adr, ret, TYPE_CONST, const742[i].type);
	    return rv;
    	}
	return -1;
}

static int
spg742_set(struct device *dev, int param, char* ret, char** save)
{
	int i=0, rv=0;

	printf ("param=%d\n",param);
	
	for (i=0; i<ARRAY_SIZE(const742); i++)
	if (param == const742[i].no)  {
		rv = spg742_set_string_param(dev, const742[i].adr, ret, TYPE_CONST, const742[i].type);
		*save=malloc (10);
		snprintf (*save,10,"ok");
		return rv;
    	}
	for (i=0; i<ARRAY_SIZE(currents742); i++)
	if (param == currents742[i].no)  {
		rv = spg742_set_string_param(dev, currents742[i].adr, ret, TYPE_CURRENTS, currents742[i].type);
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

			 for (np=0; np<ARRAY_SIZE(archive742); np++)
				{
    				 snprintf(ptr->datetime, sizeof(ptr->datetime),"%s",alog->time[archive742[np].id][i]);
    				 ptr->params[archive742[np].id] = *(float *)(alog->data[archive742[np].id][i]);
				 if (!dev->quiet)
				    fprintf (stderr,"[%s] %f\n",ptr->datetime,ptr->params[archive742[np].id]);
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
spg742_h_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct spg742_ctx *ctx;
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

		    try=1;
	    	    while (try--)	{
	    		alog.quant_param=0;
	    		buf_free(&dev->buf);

			if (-1 == generate_sequence (dev, 0, M4_GET742_ARCHIVE, archive742[i].adr, 1, 1, no, time_from, time_from, ident_query, &len)) break;
	 		ctx->send_query = ident_query;
	 		ctx->send_query_sz = len;
			if (-1 == dev_query(dev)) continue;
    			
    			for (i=0; i<ARRAY_SIZE(archive742); i++)	{
    			     //snprintf(alog.time[archive742[i].id][0], 20,"%d-%02d-%02d,%02d:00:00",time_to->tm_year+1900, time_to->tm_mon+1, time_to->tm_mday, time_to->tm_hour);
			     if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, archive742[i].id, &alog, archive742[i].adr)) continue;
			     //aaaaa
			    }
		        alog.quant_param++;
			break;
			}
	         if (alog.quant_param)		
    		    {
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
spg742_d_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct spg742_ctx *ctx;
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

		    try=1;
	    	    while (try--)	{
	    		alog.quant_param=0;
	    		buf_free(&dev->buf);

			if (-1 == generate_sequence (dev, 1, M4_GET742_ARCHIVE, archive742[i].adr, 1, 1, no, time_from, time_to, ident_query, &len)) break;
    	 		ctx->send_query = ident_query;
	 		ctx->send_query_sz = len;
			if (-1 == dev_query(dev)) continue;

			for (i=0; i<ARRAY_SIZE(archive742); i++)	{
    			     if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, archive742[i].id, &alog, archive742[i].adr)) continue;
			    }
		        alog.quant_param++;
	    		break;
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
spg742_m_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct spg742_ctx *ctx;
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

			try=1;
	    		while (try--)	{
	    		    alog.quant_param=0;
	    		    buf_free(&dev->buf);
			    snprintf(alog.time[archive742[i].id][0], 20,"%d-%02d-01,00:00:00",time_to->tm_year+1900, time_to->tm_mon+1);
			    if (-1 == generate_sequence (dev, 3, M4_GET742_ARCHIVE, archive742[i].adr, 1, 1, no, time_from, time_to, ident_query, &len)) break;
	 		    ctx->send_query = ident_query;
	 		    ctx->send_query_sz = len;
			    if (-1 == dev_query(dev)) continue;
	
			 for (i=0; i<ARRAY_SIZE(archive742); i++)	{
				if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, archive742[i].id, &alog, archive742[i].adr))	continue;
			    }
		         alog.quant_param++;
	    		 break;
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
spg742_events(struct device *dev, const char *from, const char *to, struct events **save)
{
	struct spg742_ctx *ctx;
	AnsLog alog;
	struct events *ev, *pev=NULL, *top=NULL;

	int ret=1,ret_fr=0,ret_to=0,i,j=0, ev_count=0;
	uint16_t len;
//	char	code[120];
	uint	cod[MAX_EVENTS];
	static char ident_query[100], answer[1024];
	time_t tim,sttime,fntime,entime;

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

	    // if (-1 == generate_sequence (dev, TYPE_EVENTS, GET742_FLASH, 0x3894+i*56, 0, 88, 0, time_from, time_to, ident_query, &len)) ret = -1;
	    // if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 98, &alog, 0)) ret = -1;
//        	spg742_get_ns (alog.data[0][j][6], code);
//	        	cod[ev_count]=alog.data[0][j][6];
//	        	snprintf (alog.data[0][ev_count],115,"%s",code);
//	        	ev_count++;
//	    buf_free(&dev->buf);
	if (sttime && fntime && ret==0)
	while (sttime<=fntime)
		{
	         entime=sttime+3600*24;
		 localtime_r(&sttime,time_from);
		 localtime_r(&entime,time_to);

    		    alog.quant_param=0;
    		    buf_free(&dev->buf);
		    //snprintf(alog.time[archive742[i].id][0], 20,"%d-%02d-01,00:00:00",time_to->tm_year+1900, time_to->tm_mon+1);
		    if (-1 == generate_sequence (dev, 6, M4_GET742_ARCHIVE, archive742[i].adr, 5, 1, 1, time_from, time_to, ident_query, &len)) break;
 		    ctx->send_query = ident_query;
 		    ctx->send_query_sz = len;
		    if (-1 == dev_query(dev)) 
			{
		    	 sttime+=3600*24;
			 continue;
			}
		    if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, &alog, 66)) 
			{
		    	 sttime+=3600*24;			
			 continue;
			 }
		    ev_count+=alog.quant_param;
		 printf ("time %ld %ld\n",sttime,fntime);
		 sttime+=3600*24;
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
spg742_send_msg(struct device *dev)
{
	struct spg742_ctx *ctx = get_spec(dev);
	return dev_write(dev, ctx->send_query, ctx->send_query_sz);
}

static int
spg742_parse_msg(struct device *dev, char *p, int psz)
{
// struct spg742_ctx *ctx;
 int ret=-1;
 char answer[1024];
 AnsLog alog;

// ctx = get_spec(dev);

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
spg742_parse_crc_msg(struct device *dev, char *p, int psz)
{
// struct spg742_ctx *ctx;
 int ret;
 char answer[1024];
 AnsLog alog;

// ctx = get_spec(dev);

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
spg742_check_crc(struct device *dev, char *p, int psz)
{
 struct spg742_ctx *ctx;
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
 float		temp=0;
 uint16_t 	i=0,ks16=0; 
 uint8_t	ks=0, startm=0, ln;

 switch (func)
	{
	case	M4_GET742_ARCHIVE:
				sprintf (sequence,"%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",DLE,dev->devaddr,0x90,0x0,0x0,20,0x0,
				func,OCTET_STRING,0x5,0xff,0xff,0,type,nchan,
				ARCHDATE_TAG,0x4,from->tm_year-100,from->tm_mon+1,from->tm_mday,from->tm_hour,
				ARCHDATE_TAG,0x4,to->tm_year-100,to->tm_mon+1,to->tm_mday,to->tm_hour);
				ln=27; startm=0;
				break;
	case	M4_SET742_PARAM://memcpy (buffer,sequence,6);
				//ln=19;
				memcpy (buffer,sequence,strlen(sequence));
				ln=13+strlen(sequence); 
				sprintf (sequence,"%c%c%c%c%c%c%c%c%c%c%c%c%c%s",DLE,dev->devaddr,0x90,0x0,0x0,0x6+strlen(sequence),0x0,
				//sprintf (sequence,"%c%c%c%c%c%c%c%c%c%c%c%c%c%s",DLE,dev->devaddr,0x90,0x0,0x0,0x6+0x6,0x0,
				func,PNUM,0x3,npipe,(uint8_t)((padr)%256),(uint8_t)((padr)/256),buffer);
				//memcpy (sequence+13,buffer,6);
				startm=0;
				//printf ("ln=%d (%x %x %x %x %x)\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4]);
				break;	
	case	M4_GET742_PARAM:sprintf (sequence,"%c%c%c%c%c%c%c%c%c%c%c%c%c",DLE,dev->devaddr,0x90,0x55,0x0,0x6,0x0,
				0x72,PNUM,0x3,npipe,(uint8_t)((padr)%256),(uint8_t)((padr)/256));	
				//sprintf (sequence,"%c%c%c%c%c%c%c%c",DLE,dev->devaddr,func,PNUM,0x4,npipe,(uint8_t)((padr)%256),(uint8_t)((padr)/256));
				ln=13; startm=0;
				break;

	case 	SET742_PARAM:	if (type==0)	{
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
	case 	GET742_FLASH:	if (npipe==88) sprintf (sequence,"%c%c%c%c%c%c%c",DLE,dev->devaddr,func,(uint8_t)((padr/64)%256),(uint8_t)((padr/64)/256),0x1,ZERO);
				else sprintf (sequence,"%c%c%c%c%c%c%c",DLE,dev->devaddr,func,(uint8_t)(8+padr/4),(uint8_t)(0),1,ZERO);
				ln=7; startm=0;
				break;
	case	START_EXCHANGE:	for (i=0;i<16; i++) buffer[i]=0xff; buffer[16]=0;
				sprintf (sequence,"%s%c%c%c%c%c%c%c",buffer,DLE,dev->devaddr,func,ZERO,ZERO,ZERO,ZERO);
				ln=7+16; startm = 16;
				break;
	default:		return	-1;
	}
 ks = calc_bcc ((uint8_t *)sequence+1+startm, ln-1-startm);
 sequence[ln]=(uint8_t)(ks);
 sequence[ln+1]=UK;
 if (func==M4_GET742_PARAM || func==M4_GET742_ARCHIVE || func==M4_SET742_PARAM)
    {
     ks16 = calc_bcc16 ((uint8_t *)sequence+1+startm, ln-1-startm);
     sequence[ln]=(ks16/256);
     sequence[ln+1]=(ks16%256);
    }
 *len=ln+2;
 return 0;
}

//----------------------------------------------------------------------------------------
static int 
analyse_sequence (char* dats, uint len, char* answer, uint8_t analyse, uint8_t id, AnsLog *alog, uint16_t padr)
{
 unsigned char 	dat[1000]; 
 uint16_t 	i=0,start=0, startm=0, cntNS=0, no=0, j=0, data_len=0, m4=0;
 if (len>1000) 	return -1;
 memcpy (dat,dats,len);
 alog->checksym = 0;
 //if (analyse==ANALYSE) 
 for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)dats[i],(uint8_t)dats[i]);
 i=0;
 while (i<len)
	{
	 if (dat[i+2]!=0x90 && dat[i]==DLE && (uint8_t)dat[i+1]<20 && !start)
		{
		 alog->from = (uint8_t)dat[i+1];
		 alog->func=(uint8_t)dat[i+2];
		 startm=i+1; i=i+3; start=1;
		 //if (analyse==ANALYSE) printf ("from=%d func=%d (st=%d)\n",alog->from,alog->func,startm);
		}
	 if (dat[i+2]==0x90 && dat[i]==DLE && (uint8_t)dat[i+1]<20 && !start)
		{
		 alog->from = (uint8_t)dat[i+1];
		 alog->func=(uint8_t)dat[i+7];
		 startm=i+1; i=i+7; start=7;
		 m4=1;
		 //if (analyse==ANALYSE) printf ("from=%d func=%d (st=%d)\n",alog->from,alog->func,startm);
		}
	 if (dat[i]==DLE && dat[i+2]==0x21 && dat[i+3]==0x3)
		{
		 return -1;
		}
	 if (dat[i]==DLE && dat[i+7]==0x21)
		{
		 return -1;
		}
	 //------------------------------------------------------------------------------------------------
	 if (m4 && i==len-1)
		{
 		 uint16_t ks16=calc_bcc16 ((uint8_t *)dat+startm,i-startm-1);
		 data_len=dat[startm+4];
		 if (analyse==NO_ANALYSE)
		    {
			if (ks16/256==(uint8_t)dat[i-1] && ks16%256==(uint8_t)dat[i])
				return 	1;
			else	return	-1;
			}

		 if (ks16/256==(uint8_t)dat[i-1] && ks16%256==(uint8_t)dat[i]) 
			{
			 alog->checksym = 1;
			 //printf ("ks = %x %x %x %x\n",dat[i-1],dat[i],ks16/256,ks16%256);
		 	 if (alog->func==START_EXCHANGE)
				{
				 memcpy (alog->data[id][no],dat+startm+2,3);
				 alog->data[id][no][3]=0;
				 strcpy (alog->time[id][no],"");
				 alog->quant_param = 1;
				}
			 if (alog->func==M4_GET742_PARAM)
				{
				 for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)dats[i],(uint8_t)dats[i]);
				 if (dat[startm+8]<12)
    					memcpy (alog->data[id][no],(dat+startm+9),dat[startm+8]);
    				 //printf ("==%s\n",alog->data[id][no]);
				 return 1;
				}

			 if (alog->func==M4_SET742_PARAM)
				{
				 for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)dats[i],(uint8_t)dats[i]);
				 return 1;
				}
				
			 if (alog->func==M4_GET742_ARCHIVE)
				{
				 //for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)dats[i],(uint8_t)dats[i]);
				 startm=10;
				 if (dat[21]==0x48) startm=0;
				 if (dat[22]==0x48) startm=1;
				 if (dat[23]==0x48) startm=2;
    			         memcpy (alog->data[id][alog->quant_param],(dat+startm+11+padr),4);
				 sprintf (alog->time[id][alog->quant_param],"%d-%02d-%02d,%02d:%02d:%02d",2000+dat[startm+25],dat[startm+24],dat[startm+23],dat[startm+20],0,0);
				 //printf ("[%s] %x %x %x %x (%f)\n",alog->time[id][alog->quant_param],alog->data[id][alog->quant_param][0],alog->data[id][alog->quant_param][1],alog->data[id][alog->quant_param][2],alog->data[id][alog->quant_param][3],*(float *)alog->data[id][alog->quant_param]);
				 if (startm<2)
				    return 1;
				 else
				    return -1;
				}
			}
		}
	 if (!m4)
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
			 if (alog->func==GET742_FLASH)
				{
				 if (id<98)		// Flash (param)
					{
					 memcpy (alog->data[id][no],(dat+startm+4+2+(id%4)*16),8);
					 alog->data[id][no][8]=0;
					 strcpy (alog->time[id][no],"");
					 alog->quant_param = 1;
					 //printf ("=%d %s",startm+4+2+(id%4)*16,alog->data[id][no]);
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
