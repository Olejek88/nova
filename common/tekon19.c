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
#include "tekon19.h"
#include <wchar.h>
#include <locale.h>
#include <math.h>

/* */
static	uint8_t	adapter_adr=0x2;

struct tekon19_ctx {

	char *send_query;
	int  send_query_sz;

	regex_t re;

	char crc_flag;

	uint16_t crc;
};

#define get_spec(dev)	((struct tekon19_ctx *)((dev)->spec))
                                     
/* */
static int  tekon19_init(struct device *);
static void tekon19_free(struct device *);
static int  tekon19_get(struct device *, int, char **);
static int  tekon19_set(struct device *, int, char *, char **);
static int  tekon19_parse_msg(struct device *, char *, int);
static int  tekon19_parse_crc_msg(struct device *, char *, int);
static int  tekon19_check_crc(struct device *, char *, int);
static int  tekon19_send_msg(struct device *);

static int  tekon19_get_string_param(struct device *dev, uint16_t addr, char **save, uint8_t type, uint16_t padr);
static int  tekon19_set_string_param(struct device *dev, uint16_t addr, char *save, uint8_t type, uint16_t padr);

static int  tekon19_date(struct device *dev, const char *, char **);

static int  tekon19_h_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  tekon19_m_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  tekon19_d_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  tekon19_events(struct device *, const char *, const char *,
				struct events **);
static int  generate_sequence (struct device *dev, uint8_t type, uint8_t func, uint16_t padr, uint16_t index, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence, uint16_t* len);
static int  analyse_sequence (struct device *, char*, uint16_t, char*, uint8_t, uint8_t, uint16_t);
static int  checkdate (uint8_t	type, int ret_fr, int ret_to, struct tm* time_from, struct tm* time_to, struct tm* time_cur, time_t *tim, time_t *sttime, time_t *fntime);

static uint8_t	BCD (uint8_t dat);
static uint8_t	BCH (uint8_t dat);

static uint8_t
calc_bcc (uint8_t *ptr, int n)
{
 uint8_t crc = 0;
 while (n-- > 0) 
	{
	 crc = crc + (uint8_t) *ptr++;
	}
 return crc;
}

static int
get_param_addr (struct device *dev, uint16_t param, uint16_t* addr, uint8_t	type)
{
    FILE    	*cfg_file;
    uint16_t	i=0;
    unsigned int	adr;
    char	buf[100],find[50],adr_str[100];
    char	value[10][10];
    char*	pos;
    cfg_file =  fopen(DEVICE_CFG,"r");
    if (!cfg_file) 
	{
	 cfg_file =  fopen(DEVICE_CFG2,"r");
	 if (!cfg_file) 
		{
		 if (!dev->quiet)
		    fprintf (stderr,"error open config file\n");
		 return -1;
		}
	}
    if (type>5)
    	{
	 fclose	(cfg_file);
	 if (!dev->quiet)
		fprintf (stderr,"unknown type %d\n",type);
	 return -1;
	}
    while(!feof(cfg_file))
	{
	 if(fgets(buf,100,cfg_file)!=NULL)
		{
		 for (i=0;i<strlen(buf)-1;i++) 
		    if (buf[i]==';' || buf[i]=='/' || buf[i]=='#') 
			buf[i+1]=0;
		 sprintf (find,"%d=",param);
		 pos=strstr(buf,find);
		 if (pos)
		    {
		     snprintf (adr_str,sizeof(adr_str),"%s",pos+strlen(find));
			i=0;
		     char* token = strtok(adr_str,",");
		     while(token!=NULL)
			{
			 if (strlen(token)<10) snprintf (value[i],10,token);
			 else sprintf (value[i],"0");
			 token = strtok(NULL,","); i++;
			}
		     if (i<=TYPE_MONTH)
			{
			 fclose	(cfg_file);
			 if (!dev->quiet)
				fprintf (stderr,"error in config file: few parameters in %s\n",buf);
			 return -1;
			}
		     if (sscanf (value[type],"%x",&adr))
		        {
		         *addr=adr;
			 fclose	(cfg_file);
		         //if (!dev->quiet) fprintf (stderr,"found: ret = %s\n",value[type]);
		         return	0;
		         }
		     else
		        {
		         if (!dev->quiet)
		        	fprintf (stderr,"unknown parameter %s\n",value[type]);
		         fclose	(cfg_file);
		         return -1;
			}
		    }
	     }
	}
    if (!dev->quiet)
	    fprintf (stderr,"parameter %d not found\n",param);
    fclose	(cfg_file);
    return	-1;
}

static void
ctx_zero(struct tekon19_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

static int
tekon19_get_ident(struct device *dev)
{
	static char ident_query[100];
	struct 	tekon19_ctx *ctx;
	char 	t19_id[100],answer[100];

	int 		ret;
	uint16_t	len;
	struct 	tm *time_from,*time_to;

	time_t tim;
	tim=time(&tim);			// default values
	time_from=localtime(&tim); 	// get current system time
	time_to=localtime(&tim); 	// get current system time

	ctx = get_spec(dev);
	ret = -1;
	ctx->crc_flag = 0;

        if (PROTOCOL==1) 
    	    {
	    if (-1 == generate_sequence (dev, 6, CMD_READ_PARAM, 0, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	    ctx->send_query = ident_query;
	    ctx->send_query_sz = len;
	    if (-1 == dev_query(dev)) goto free_regex;
	    if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, NO_ANALYSE, 0, 0)) goto out;
	    buf_free(&dev->buf);
	    if (-1 == generate_sequence (dev, 7, CMD_READ_PARAM, 0, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	    ctx->send_query = ident_query;
	    ctx->send_query_sz = len;
	    if (-1 == dev_query(dev)) goto free_regex;
	    if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, NO_ANALYSE, 0, 0)) goto out;
	    buf_free(&dev->buf);
	    if (-1 == generate_sequence (dev, 8, CMD_READ_PARAM, 0, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	    ctx->send_query = ident_query;
	    ctx->send_query_sz = len;
	    if (-1 == dev_query(dev)) goto free_regex;
	    if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, NO_ANALYSE, 0, 0)) goto out;
	    buf_free(&dev->buf);
	    }
/*
	if (-1 == generate_sequence (dev, 5, CMD_SET_ACCESS_LEVEL, 0, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto free_regex;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, NO_ANALYSE, 0, 0)) goto out;
	buf_free(&dev->buf);
*/
	if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM_SLAVE, T19_SERIAL, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto free_regex;
	if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) goto out;
	buf_free(&dev->buf);

	sprintf (t19_id,"%x%x",(uint8_t)answer[1],(uint8_t)answer[0]);
	devlog(dev, "\tT19 serial = %s\n", t19_id);
	ret = 0;

free_regex:
	regfree(&ctx->re);
out:
	ctx_zero(ctx);
	buf_free(&dev->buf);
	return ret;
}

static int
tekon19_get_string_param(struct device *dev, uint16_t addr, char **save, uint8_t type, uint16_t padr)
{
	struct tekon19_ctx *ctx;
	float	value;
	//char	value_string[100];
	uint16_t	value_uint=0;
	static char ident_query[100],	answer[1024], date[50];
	void 		*fnsave;
	int 		ret;
	uint16_t	len,i;
	char	*value_string;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	time_t 	tim;

	localtime_r(&tim,time_from); 	// get current system time
	localtime_r(&tim,time_to); 	// get current system time

	ctx = get_spec(dev);
	ret = -1;

	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = tekon19_parse_crc_msg;

	if (type==T19_TYPE_DATE)
	    {
    	     if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM_SLAVE, addr, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) goto out;
	     if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
	     snprintf (date,40,"%04d-%02d-%02d",BCD(answer[3])+2000,BCD(answer[2]),BCD(answer[1]));
	     buf_free(&dev->buf);
    	     if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM_SLAVE, addr+1, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) goto out;
	     if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
	     buf_free(&dev->buf);
	     value_string=malloc (100);
	     snprintf (value_string,100,"%s,%02d:%02d:%02d",date,BCD(answer[3]),BCD(answer[2]),BCD(answer[1]));
	     *save=value_string;
	     if (!dev->quiet)
	    	    fprintf (stderr,"%s\n",value_string);
	     ret=0;
	     goto out;
    	    }
	if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM_SLAVE, addr, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	len = analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr);
	if (-1 == len) goto out;

	switch	(type)
	    {
		case	T19_TYPE_NOTDEFINED:	break;
		case	T19_TYPE_FLOAT:		value=*(float*)(answer);
						value_string=malloc (100);
						snprintf (value_string,100,"%f",value);
						*save=value_string;
						if (!dev->quiet)
						    //fprintf (stderr,"%s\n",value_string);
						ret=0;
						break;
		case	T19_TYPE_INT:		for (i=0; i<len;i++)
	    					    value_uint+=BCD(answer[i])*(pow(100,i));
	    					value_string=malloc (100);
	    					snprintf (value_string,100,"%d",value_uint);
						*save=value_string;
						if (!dev->quiet)
	    					    //fprintf (stderr,"%s\n",value_string);
	    					ret=0;
						break;
		default:			break;
	    }
out:
	regfree(&ctx->re);
	/* restore handler */
	dev->opers->parse_msg = fnsave;
	buf_free(&dev->buf);
	ctx_zero(ctx);
	free (time_from);
	free (time_to);
	return ret;
}

static int
tekon19_set_string_param(struct device *dev, uint16_t addr, char *save, uint8_t type, uint16_t padr)
{
	struct tekon19_ctx *ctx;
	static char ident_query[100],	answer[1024];
	void 		*fnsave;
	int 		ret;
	uint16_t	len,i;
	float	value;
	struct 	tm *time_from,*time_to;
	time_t 	tim;
	tim=time(&tim);			// default values
	time_from=localtime(&tim); 	// get current system time
	time_to=localtime(&tim); 	// get current system time

	ctx = get_spec(dev);
	ret = -1;

	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = tekon19_parse_crc_msg;

	if (-1 == generate_sequence (dev, 2, CMD_WRITE_PARAM_SLAVE, addr, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
	buf_free(&dev->buf);

	if (type==T19_TYPE_DATE)
	    {
    	     ret = sscanf(save,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
    	     if (-1 == ret) goto out;
	     ident_query[0]=0x2;
	     ident_query[1]=BCH(time_from->tm_mday);
	     ident_query[2]=BCH(time_from->tm_mon);
	     ident_query[3]=BCH(time_from->tm_year-2000);
	     len=4;
    	     if (-1 == generate_sequence (dev, 7, CMD_WRITE_PARAM_SLAVE, 0xf017, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) goto out;
	     if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
	     buf_free(&dev->buf);
	     ident_query[0]=0x0;
	     ident_query[1]=BCH(time_from->tm_sec);
	     ident_query[2]=BCH(time_from->tm_min);
	     ident_query[3]=BCH(time_from->tm_hour);
	     len=4;
    	     if (-1 == generate_sequence (dev, 7, CMD_WRITE_PARAM_SLAVE, 0xf018, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) goto out;
	     if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
	     buf_free(&dev->buf);
	     ret=0;
	     goto out;
    	    }

	switch	(type)
	    {
		case	T19_TYPE_FLOAT:		sscanf (save,"%f",&value);
						memcpy	(ident_query,&value,4);
						if (!dev->quiet)
							fprintf (stderr,"%f (%x %x %x %x)\n",value,ident_query[0],ident_query[1],ident_query[2],ident_query[3]);
						len=4;
						break;
		case	T19_TYPE_INT:		for (i=0; i<strlen(save); i++)
						    ident_query[i]=atoi(&save[i]);
						len=i;
						break;
		case	T19_TYPE_NOTDEFINED:
		default:			goto out;
	    }
	//printf ("%s %d\n",ident_query,len);
	if (-1 == generate_sequence (dev, 7, CMD_WRITE_PARAM_SLAVE, addr, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
	buf_free(&dev->buf);
	ret=0;
out:
	regfree(&ctx->re);
	//free(ctx->send_query);
	/* restore handler */
	dev->opers->parse_msg = fnsave;
	buf_free(&dev->buf);
	ctx_zero(ctx);
	return ret;
}

static int  tekon19_date(struct device *dev, const char *ret, char **save)
{
	int	rt=-1;
	uint16_t	addr=0,type=0,param=500;

	rt=get_param_addr (dev, param, &addr, 0);
	if (rt==0)	{
		 if (strlen (ret)==0)	{
	    	     rt = get_param_addr (dev, param, &type, 5);
	    	     if (rt==0)
    	    	        rt = tekon19_get_string_param(dev, addr, save, type, param);
		    }
		 else	{
	    	     rt = get_param_addr (dev, param, &type, 5);
	    	     if (rt==0)
    	    	        rt = tekon19_set_string_param(dev, addr, (char *)ret, type, param);
	    	     *save=malloc (10);
    	    	     if (rt==0)
	    		snprintf (*save,10,"ok");
	    	     else
			snprintf (*save,10,"error");
		    }
		}
	return	rt;
}

/*
 * Interface
 */
static struct vk_operations tekon19_opers = {
	.init	= tekon19_init,
	.free	= tekon19_free,
	.get	= tekon19_get,
	.set	= tekon19_set,
	.date	= tekon19_date,

	.send_msg  = tekon19_send_msg,
	.parse_msg = tekon19_parse_msg,
	.check_crc = tekon19_check_crc,

	.h_archiv = tekon19_h_archiv,
	.m_archiv = tekon19_m_archiv,
	.d_archiv = tekon19_d_archiv,

	.events = tekon19_events
};

void
tekon19_get_operations(struct vk_operations **p_opers)
{
	*p_opers = &tekon19_opers;
}

static int
tekon19_init(struct device *dev)
{
	struct tekon19_ctx *spec;
	int ret=-1;

	spec = calloc(1, sizeof(*spec));

	if (!spec) {
		set_system_error("calloc");
		ret = -1;
	} else {
		dev->spec = spec;
		ret =   (-1 == tekon19_get_ident(dev));
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
tekon19_free(struct device *dev)
{
	struct tekon19_ctx *spec = get_spec(dev);

	free(spec);
	dev->spec = NULL;
}

static int
tekon19_get_ns (int no, int nom, char* code)
{
    int i;
    //fprintf (stderr,"ns %d %d\n",no,nom);
    for (i=0; i<ARRAY_SIZE(nscodeT19); i++)
	if (no == nscodeT19[i].id && (nscodeT19[i].nid==255 || nscodeT19[i].nid==nom))
	    {
	    snprintf (code,75,"%s",nscodeT19[i].name);
	    return 1;
	    }
    return -1;
}

static int
tekon19_get(struct device *dev, int param, char **ret)
{
	int 		rv=-1;
	uint16_t	addr=0,type=0;
	rv=get_param_addr (dev, param, &addr, 0);
	if (rv==0)
	    {
	     rv = get_param_addr (dev, param, &type, 5);
	     if (rv==0)		{
    	    	     rv = tekon19_get_string_param(dev, addr, ret, type, param);
    	    	     rv=0;
    	    	    }
    	    }
	return rv;
}

static int
tekon19_set(struct device *dev, int param, char *ret, char **save)
{
	int 	rv=0;
	uint16_t	addr=0,type=0;
	rv=get_param_addr (dev, param, &addr, 0);
	if (rv==0)
	    {
	     rv = get_param_addr (dev, param, &type, 5);
	     if (rv==0)
        	     rv = tekon19_set_string_param(dev, addr, ret, type, param);
	     *save=malloc (10);
    	     if (rv==0)
	    	    snprintf (*save,10,"ok");
	     else
		    snprintf (*save,10,"error");
	     rv=0;
    	    }
	return rv;
}

static int
make_archive(struct device *dev, int no, AnsLog* alog, int type, struct archive **save, uint16_t recs)
{
	struct archive *archives = NULL, *lptr = NULL, *ptr;
	int nrecs,i=0,ret;
	struct 	tm *tim=malloc (sizeof (struct tm));
	uint16_t	ind=0;
	float	value;

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

			 for (no=0; no<ARCH_MAX_NPARAM; no++)
			 if (alog->type[no][i])
	    			{
				 value =*(float*)(alog->data[no][i]);
				 if (value<MAX_VALUE && value>MIN_VALUE)
				    ptr->params[no] =*(float*)(alog->data[no][i]);
				 snprintf(ptr->datetime, sizeof(ptr->datetime),"%s",alog->time[0][i]);
				 if (!dev->quiet) fprintf (stderr,"[%d/%d][%s] %f\n",i,no,ptr->datetime,ptr->params[no]);
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
tekon19_h_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct tekon19_ctx *ctx;
	int 		ret,ret_fr,ret_to;
	uint16_t 	len,adr=0,try,i;
	static char 	ident_query[100];
	time_t 		tim,sttime,fntime;
	AnsLog		alog;
	uint16_t	index=0,vsk=0;

	ctx = get_spec(dev);
	ctx->crc_flag = 0;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);

	struct 	tm *time_to=malloc (sizeof (struct tm));
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	tim=time(&tim);
	localtime_r(&tim,time_cur);

	ret=checkdate (TYPE_HOURS,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
	localtime_r(&fntime,time_to);

        if (time_to->tm_year%4==0) vsk=0; else vsk=1;
        index=24*(((time_to->tm_year-100)*365+(time_to->tm_year-100)/4+time_to->tm_yday+vsk)%ARCHIVE_DEEP)+time_to->tm_hour;

	if (!dev->quiet)
		fprintf (stderr,"[%d] || %d %d %d\n",index,time_from->tm_year+1900,time_from->tm_yday,time_from->tm_hour);
	alog.quant_param=0;

	if (ret==0)
	while (fntime>=sttime)
	    {
            localtime_r (&fntime,time_to);
	    sprintf (alog.time[0][alog.quant_param],"%04d-%02d-%02d,%02d:00:00",time_to->tm_year+1900,time_to->tm_mon+1,time_to->tm_mday,time_to->tm_hour);
	    //printf ("[%d] index=%d [%s]\n",alog.quant_param,index,alog.time[0][alog.quant_param]);

	    for (i=0; i<ARCH_MAX_NPARAM; i++)
	    	{
	    	    ret=get_param_addr (dev, i, &adr, TYPE_HOURS);
	    	    alog.type[i][alog.quant_param]=0;
	    	    if (adr<1 || -1 == ret) continue;
		    try=MAX_TRY;
	    	    while (try--)	{
			    buf_free(&dev->buf);
			    if (-1 == generate_sequence (dev, 1, CMD_READ_INDEX_PARAM, adr, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
			    ctx->send_query = ident_query;
			    ctx->send_query_sz = len;
			    if (-1 == dev_query(dev)) continue;
			    if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, alog.data[i][alog.quant_param], ANALYSE, 0, 0)) continue;
			    alog.type[i][alog.quant_param]=1;
			    break;
			    }
		}
	    if (index==0) break;
	    if (alog.quant_param>=ARCHIVE_NUM_MAX) break;
	    fntime-=60*60;
	    alog.quant_param++;
	    index--;
	    }
	if (alog.quant_param == 0)  {
		*save=NULL;
		ret=0;
		}
	else
	    ret = make_archive(dev, no, &alog, TYPE_HOURS, save, 0);
    	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int 
tekon19_d_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct 		tekon19_ctx *ctx;
	int 		ret,ret_fr,ret_to,i,try;
	uint16_t 	len,adr=0;
	static char 	ident_query[100];
	time_t 		tim,sttime,fntime;
	AnsLog		alog;
	uint16_t	index=0;

	ctx = get_spec(dev);
	ctx->crc_flag = 0;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);

	struct 	tm *time_to=malloc (sizeof (struct tm));
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	tim=time(&tim);
	localtime_r(&tim,time_cur);

	ret=checkdate (TYPE_DAYS,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
	fntime-=24*60*60;

	alog.quant_param=0;
	if (ret==0)
	while (fntime>=sttime)
	    {
            localtime_r (&fntime,time_to);
    	    index=time_to->tm_yday;
	    sprintf (alog.time[0][alog.quant_param],"%04d-%02d-%02d,00:00:00",time_to->tm_year+1900,time_to->tm_mon+1,time_to->tm_mday);
	    if (!dev->quiet)
		fprintf (stderr,"index=%d [%s] (%ld/%ld)\n",index,alog.time[0][alog.quant_param],fntime,sttime);

	    for (i=0; i<ARCH_MAX_NPARAM; i++)
	    	{
	    	    ret=get_param_addr (dev, i, &adr, TYPE_DAYS);
	    	    alog.type[i][alog.quant_param]=0;
	    	    if (adr<1 || -1 == ret) continue;
		    try=MAX_TRY;
	    	    while (try--)	{
			    buf_free(&dev->buf);
			    if (-1 == generate_sequence (dev, 1, CMD_READ_INDEX_PARAM, adr, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
			    ctx->send_query = ident_query;
			    ctx->send_query_sz = len;
			    if (-1 == dev_query(dev)) continue;
			    if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, alog.data[i][alog.quant_param], ANALYSE, 0, 0)) continue;
			    alog.type[i][alog.quant_param]=1;
			    break;
			}
		}
	    if (index==0) break;
	    alog.quant_param++;
	    if (alog.quant_param>=ARCHIVE_NUM_MAX) break;
	    fntime-=24*60*60;
	    index--;
	    }
	if (alog.quant_param == 0)  {
		*save=NULL;
		ret=0;
		}
	else
	    ret = make_archive(dev, no, &alog, TYPE_DAYS, save, 0);
    	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int 
tekon19_m_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct 		tekon19_ctx *ctx;
	int 		ret,ret_fr,ret_to,i;
	uint16_t 	len,adr=0,month=0,try;
	static char 	ident_query[100];
	time_t 		tim,sttime,fntime;
	AnsLog		alog;
	uint16_t	index=0;

	ctx = get_spec(dev);
	ctx->crc_flag = 0;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);

	struct 	tm *time_to=malloc (sizeof (struct tm));
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	tim=time(&tim);
	localtime_r(&tim,time_cur);

	alog.quant_param=0;
	ret=checkdate (TYPE_MONTH,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
	if (ret==0)	{
		month=(fntime-sttime)/(24*30*3600);
    		index=(time_to->tm_year%4)*12+time_to->tm_mon;
    		if (index==0)	{
    			index=47;
    			time_to->tm_mon=11;
    			time_to->tm_year--;
    		    }
    		else index--;
    	    }

	if (ret==0 && month>0)
	while (month)
	    {
	    sprintf (alog.time[0][alog.quant_param],"%04d-%02d-01,00:00:00",time_to->tm_year+1900,time_to->tm_mon+1);
	    if (!dev->quiet)
		fprintf (stderr,"index=%d [%s]\n",index,alog.time[0][alog.quant_param]);
	    for (i=0; i<ARCH_MAX_NPARAM; i++)
	    	{
	    	    ret=get_param_addr (dev, i, &adr, TYPE_MONTH);
	    	    alog.type[i][alog.quant_param]=0;
	    	    if (adr<1 || -1 == ret) continue;
		    try=MAX_TRY;
	    	    while (try--)	{
			    buf_free(&dev->buf);
			    alog.type[i][alog.quant_param]=0;
			    if (-1 == generate_sequence (dev, 1, CMD_READ_INDEX_PARAM, adr, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
			    ctx->send_query = ident_query;
			    ctx->send_query_sz = len;
			    if (-1 == dev_query(dev)) continue;
			    if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, alog.data[i][alog.quant_param], ANALYSE, 0, 0)) continue;
			    alog.type[i][alog.quant_param]=1;
			    break;
			}
		}
	    if (index==0) break;
	    if (alog.quant_param>=ARCHIVE_NUM_MAX) break;
	    alog.quant_param++;
	    if (time_to->tm_mon>0) time_to->tm_mon--;
	    else { time_to->tm_mon=11; time_to->tm_year--; }
	    if (index>0) index--;
	    month--;
	    }
	if (alog.quant_param == 0)  {
		*save=NULL;
		ret=0;
		}
	else
	    ret = make_archive(dev, no, &alog, TYPE_MONTH, save, 0);
    	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_from);
	free (time_cur);
	free (time_to);
	return ret;
}

static int
make_event(char *times, char *events, struct events **save)
{
 struct events *ev;
 ev = malloc(sizeof(*ev));
 if (!ev) {
	set_system_error("malloc");
	 return -1;
	}
 ev->num = 1; 	// ???
 ev->event = 1;	// ???
 snprintf(ev->datetime, sizeof(ev->datetime),"%s",times);
 ev->next = NULL;
 *save = ev;
 return 1;
}


static int
tekon19_events(struct device *dev, const char *from, const char *to, struct events **save)
{
	struct 	tekon19_ctx *ctx;
	AnsLog 	alog;
	struct 	events *ev, *pev=NULL, *top=NULL;
	int 	ret=1,i;
	uint16_t len,index=0;
	char	code[100];
	static char ident_query[100], answer[1024];
	time_t 	sttime,fntime;

	ctx = get_spec(dev);
	ctx->crc_flag = 1;

	struct 	tm *time_from=malloc (sizeof (*time_from));
	struct 	tm *time_to=malloc (sizeof (*time_to));
	struct 	tm *time_cur=malloc (sizeof (*time_cur));

	ret = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
	time_from->tm_mon-=1; time_from->tm_year-=1900;
	sttime=mktime (time_from);

	ret = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
	time_to->tm_mon-=1; time_to->tm_year-=1900;
	fntime=mktime (time_to);

        index=0;
	while (index<10)
	    {
	    if (-1 == generate_sequence (dev, 6, CMD_READ_INDEX_PARAM, 0x901, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
	    ctx->send_query = ident_query;
	    ctx->send_query_sz = len;
	    if (-1 == dev_query(dev)) break;
	    if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) break;
	    buf_free(&dev->buf);
	    //printf ("%d %d %d\n",answer[0],answer[1],answer[2]);
	    if (answer[1]<52 && answer[2]<23)
		{
		 sprintf (alog.time[0][alog.quant_param],"%04d-%02d-%02d",BCD(answer[3])+2000,BCD(answer[2]),BCD(answer[1]));
		 if (-1 == generate_sequence (dev, 6, CMD_READ_INDEX_PARAM, 0x902, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		 ctx->send_query = ident_query;
		 ctx->send_query_sz = len;
		 if (-1 == dev_query(dev)) break;
		 if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) break;
		 sprintf (alog.time[0][alog.quant_param],"%s,%02d:%02d:%02d",alog.time[0][alog.quant_param],BCD(answer[3]),BCD(answer[2]),BCD(answer[1]));
		 buf_free(&dev->buf);

		 if (-1 == generate_sequence (dev, 6, CMD_READ_INDEX_PARAM, 0x903, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		 ctx->send_query = ident_query;
		 ctx->send_query_sz = len;
		 if (-1 == dev_query(dev)) break;
		 if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) break;
		 buf_free(&dev->buf);
		 i=BCD(answer[0]);

		 if (-1 == generate_sequence (dev, 6, CMD_READ_INDEX_PARAM, 0x904, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		 ctx->send_query = ident_query;
		 ctx->send_query_sz = len;
		 if (-1 == dev_query(dev)) break;
		 if (-1 == analyse_sequence (dev, dev->buf.p, dev->buf.len, alog.data[0][alog.quant_param], ANALYSE, 0, 0)) break;
		 buf_free(&dev->buf);

    	         //printf ("[%d] [%s] (0x%x,0x%x,0x%x,0x%x) (%d)\n",index,alog.time[0][alog.quant_param],alog.data[0][alog.quant_param][0],alog.data[0][alog.quant_param][1],alog.data[0][alog.quant_param][2],alog.data[0][alog.quant_param][3],i);
		 if (i>0 && i<255) 
		    ret=tekon19_get_ns (i, BCD(alog.data[0][alog.quant_param][0]), code);
		 if (ret==-1)
		    fprintf (stderr,"unknown error\n");
		 else
		    {
		     //printf ("[%d] [%s] (0x%x,0x%x,0x%x,0x%x) (%s)\n",index,alog.time[0][alog.quant_param],alog.data[0][alog.quant_param][0],alog.data[0][alog.quant_param][1],alog.data[0][alog.quant_param][2],alog.data[0][alog.quant_param][3],code);
		     snprintf (alog.data[0][alog.quant_param],120,"%s (0x%x,0x%x,0x%x,0x%x)",code,(uint8_t)alog.data[0][alog.quant_param][0],(uint8_t)alog.data[0][alog.quant_param][1],(uint8_t)alog.data[0][alog.quant_param][2],(uint8_t)alog.data[0][alog.quant_param][3]);
		     alog.type[0][alog.quant_param]=i;
		     if (alog.quant_param>=ARCHIVE_NUM_MAX) break;
		     alog.quant_param++;
		     }
		}
	     index++;
	    }
	buf_free(&dev->buf);

	for (i = 0; i < alog.quant_param; i++) {
	        ev = malloc(sizeof(*ev));
	        if (!ev) {
			set_system_error("malloc");
			regfree(&ctx->re);
 			ctx_zero(ctx);
			free (time_from);
			free (time_cur);
			free (time_to);
			return -1;
			}
		ev->num = i;
		ev->event = alog.type[0][i];
		snprintf(ev->datetime, sizeof(ev->datetime),"%s",alog.time[0][i]);
		if (!dev->quiet)
			fprintf (stderr,"[%s] (%d) %s\n",ev->datetime,ev->event,alog.data[0][i]);
		ev->next = NULL;
		if(pev)
		    pev->next = ev;
		else
		    top = ev;
	        pev = ev;
	        ret=0;
	    }

	 if (alog.quant_param == 0)  {
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
tekon19_send_msg(struct device *dev)
{
	struct tekon19_ctx *ctx = get_spec(dev);
	return dev_write(dev, ctx->send_query, ctx->send_query_sz);
}

static int
tekon19_parse_msg(struct device *dev, char *p, int psz)
{
    struct tekon19_ctx *ctx;
    int ret=-1;
    char answer[1024];

    ctx = get_spec(dev);
// if (!dev->quiet)
//	fprintf(stderr, "\rParsing: %10d bytes", psz);
    ret=analyse_sequence (dev, p, psz, answer, ANALYSE, 0, 0);
    if (ret>0)
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
tekon19_parse_crc_msg(struct device *dev, char *p, int psz)
{
    struct tekon19_ctx *ctx;
    int ret;
    char answer[1024];
    ctx = get_spec(dev);
    if (!dev->quiet)
	fprintf(stderr, "\rParsing: %10d bytes", psz);

    ret=analyse_sequence (dev, p, psz, answer, ANALYSE, 0, 0);
    if (ret>0)
	{
	if (!dev->quiet)
		fprintf(stderr, "\nParsing crc ok\n");
	ret = 0;
	return ret;
	}
    usleep (10000);
    return 1;
}

static int
tekon19_check_crc(struct device *dev, char *p, int psz)
{
    struct tekon19_ctx *ctx;
    int ret;
    char answer[1024];

    ctx = get_spec(dev);
    if (ctx->crc_flag) {
		//printf ("tekon19_check_crc\n");
		ret=analyse_sequence (dev, p, psz, answer, ANALYSE, 0, 0);
		if (ret) return 0;
		else return -1;
	} else {
		ret = 0;
	}
    return ret;
}

//static int  generate_sequence (struct device *dev, uint8_t type, uint8_t func, const char* padr, uint8_t nchan, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence);
// function generate send sequence for logika
static int 
generate_sequence (struct device *dev, uint8_t type, uint8_t func, uint16_t padr, uint16_t index, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence, uint16_t* len)
{
    char 	buffer[150];
    uint16_t 	i=0, ln=0;

    if (no!=1 && no!=2) 
	{
	 if (!dev->quiet) 
		fprintf (stderr,"unknown protocol (%d)\n",npipe);
	 return	-1;
	}
    if (no==1)
    switch (type)
	{
        case 0:    	sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c%c",dev->devaddr,((no&0x3)<<5)|0x80,0,0,4,0,2,padr&0xff,(padr&0xff00)>>8,0,0,0,0);
	    		if (!dev->quiet) 
	    			fprintf (stderr,"[tek1] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",no,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12]);
			ln=13;
	    		break;
	case 1:    	sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c%c",dev->devaddr,((no&0x3)<<5)|0x80,0,0,6,0,8,padr&0xff,(padr&0xff00)>>8,index&0xff,(index&0xff00)>>8,0,0);
	    		if (!dev->quiet) 
	    			fprintf (stderr,"[tek1] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",no,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12]);
			ln=13;
	    		break;
	case 5:    	sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c%c",dev->devaddr,((no&0x3)<<5)|0x80,0,0,4,0,2,padr&0xff,(padr&0xff00)>>8,0,0,0,0);
	    		if (!dev->quiet) 
	    			fprintf (stderr,"[tek1] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",no,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12]);
			ln=13;
	    		break;
	case 6:		for (i=0;i<23;i++) buffer[i]=0;
			buffer[24]=0xff; buffer[28]=0xff; buffer[32]=0x2; buffer[33]=0xe0; buffer[34]=0x41; buffer[35]=0xdf; ln=36;
        		if (!dev->quiet) 
        			fprintf (stderr,"[tek1] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",no,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12]);
			break;
        case 7:		sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c%c",0,0x80,0,0,4,0,2,0,0xf0,0,0,0,0);
    			ln=13;
        		if (!dev->quiet) 
        			fprintf (stderr,"[tek1] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",no,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12]);
			break;
        case 8:		sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c%c",0,0xa0,0,0,4,0,2,0,0xf0,0,0,0,0);
    			ln=13;
            		if (!dev->quiet) 
            			fprintf (stderr,"[tek1] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",no,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12]);
            		break;
	default:	if (!dev->quiet) 
				fprintf (stderr,"unknown frame request %d\n",type);
			return	-1;
	}
    if (no==2)
    switch (type)
	{
        case 0:		sprintf (buffer,"%c%c%c%c%c%c%c",0x10,0x4D,adapter_adr,(uint8_t)func,dev->devaddr&0xFF,padr&0xff,(padr&0xff00)>>8);
            		buffer[7]=calc_bcc ((uint8_t *)buffer+1, 6);
            		buffer[8]=0x16;
            		ln=9;
	    		if (!dev->quiet) 
	    			fprintf (stderr,"[tek2] wr[%d] [0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8]);
	    		break;
			// 68 0a 0a 68 44 02 14 06 01 08 01 09 77 00 EA 16 read journal
	case 1: 	// 68 07 07 68 46 01 15 01 09 FF 00 65 16
			sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c%c",0x68,0x9,0x9,0x68,0x44,adapter_adr,0x19,dev->devaddr&0xFF,padr&0xff,(padr&0xff00)>>8,index&0xff,(index&0xff00)>>8,1);
    			buffer[13]=calc_bcc ((uint8_t *)buffer+4, 9);
    			buffer[14]=0x16;
			ln=15;
        		if (!dev->quiet)
        			fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13]);
			break;
    	case 2:		sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c",0x68,0x7,0x7,0x68,0x40,adapter_adr,0x14,0x3,dev->devaddr&0xFF,0x5,0x2);
    			buffer[11]=calc_bcc ((uint8_t *)buffer+4, 7);
    			buffer[12]=0x16;
    	        	if (!dev->quiet)
    	        		fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12]);
			ln=13;
			break;
    	case 3:		sprintf (buffer,"%c%c%c%c%c%c%c%c%c",0x10,0x40,0x1,0x11,0x2,0x1c,0xf0,0x60,0x16);
    	        	if (!dev->quiet) 
    	        		fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8]);
			ln=9;
			break;
    	case 4:		sprintf (buffer,"%c%c%c%c%c%c%c%c%c",0x10,0x40,0x1,0x11,0x2,0x61,0x80,0x35,0x16);
    	        	if (!dev->quiet) 
    	        		fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8]);
			ln=9;
			break;
    	case 5:		sprintf (buffer,"%c%c%c%c%c%c%c%c%c",0x10,0x40,0x1,0x17,0x1,0x0,0x0,0x59,0x16);
    	        	if (!dev->quiet) 
    	        		fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8]);
			ln=9;
			break;
	case 6: 	sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c%c%c",0x68,0xa,0xa,0x68,0x44,adapter_adr,CMD_WRITE_PARAM_SLAVE,0x6,dev->devaddr&0xFF,0x8,padr&0xff,(padr&0xff00)>>8,index&0xff,(index&0xff00)>>8);
    			buffer[14]=calc_bcc ((uint8_t *)buffer+4, 10);
    			buffer[15]=0x16;
			ln=16;
        		if (!dev->quiet) 
        			fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13]);
			break;

	case 7: 	sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c",0x68,0x8+(*len),0x8+(*len),0x68,0x44,adapter_adr,(uint8_t)func,0x4+(*len),dev->devaddr&0xFF,0x3,padr&0xff,(padr&0xff00)>>8);
			memcpy (buffer+12,sequence,*len);
			ln=*len+13;
    			buffer[*len+12]=calc_bcc ((uint8_t *)buffer+4, *len+8);
    			ln=*len+14; //printf ("sd=%d\n",ln);
    			buffer[*len+13]=0x16;
			ln=*len+14;
        		if (!dev->quiet) 
        			fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13]);
			break;

	case 10:	sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c%c%s",0x68,0xc,0xc,0x68,0x4a,dev->devaddr&0xFF,(uint8_t)func,0x8,adapter_adr,0x3,padr&0xff,(padr&0xff00)>>8,0x2,sequence);
	            	buffer[17]=calc_bcc ((uint8_t *)buffer+4, 13+4);
        		buffer[18]=0x16;
    			ln=19;
    			if (!dev->quiet) 
    				fprintf (stderr,"[tek2] out[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13],buffer[14],buffer[15],buffer[16],buffer[17],buffer[18]);
    			break;
	default:	if (!dev->quiet) 
				fprintf (stderr,"unknown frame request %d\n",type);
			return	-1;
        }
     memcpy (sequence,buffer,ln);
     *len=ln;
     return 0;
}

//----------------------------------------------------------------------------------------
static int
analyse_sequence (struct device *dev, char* data, uint16_t len, char* dat, uint8_t analyse, uint8_t id, uint16_t padr)
{
    uint16_t	crc=0;          //(* CRC checksum *)
    //for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)data[i],(uint8_t)data[i]);
    if (analyse==NO_ANALYSE)
	if (data[len-1]!=0x16 || data[len]==0x16)
	    return 0;
    if (len==1 && (uint8_t)data[0]==0xa2)
	    return 1;

    if (len>5)
        {
	 if (data[len-1]!=0x16 || data[len]==0x16) return 0;
//    	    for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)data[i],(uint8_t)data[i]);
            if (PROTOCOL==2)	{
        	     if (!dev->quiet) fprintf (stderr,"[tek2] rd[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",len,(uint8_t)data[0],(uint8_t)data[1],(uint8_t)data[2],(uint8_t)data[3],(uint8_t)data[4],(uint8_t)data[5],(uint8_t)data[6],(uint8_t)data[7],(uint8_t)data[8],(uint8_t)data[9]);
        	    }
	    else	{
		     if (!dev->quiet) fprintf (stderr,"[tek2] rd[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",len,(uint8_t)data[0],(uint8_t)data[1],(uint8_t)data[2],(uint8_t)data[3],(uint8_t)data[4],(uint8_t)data[5],(uint8_t)data[6],(uint8_t)data[7],(uint8_t)data[8],(uint8_t)data[9],(uint8_t)data[10],(uint8_t)data[11],(uint8_t)data[12]);
		    }
	    if (PROTOCOL==2)
		{
                if (data[0]==0x10)
	    	    {
    	             crc=calc_bcc ((uint8_t*)data+1, len-3);
    	             //if (!dev->quiet) printf ("%x %x\n",crc,(uint8_t)data[len-2]);
        	     if (crc==(uint8_t)data[len-2])
            	        {
            	         memcpy (dat,data+0x3,4);
	                 return len;
	                }
	             return -1;
    		    }
                if (data[0]==0x68)
        	    {
                     crc=calc_bcc ((uint8_t*)data+4, len-6);
    	             //if (!dev->quiet) printf ("%x %x\n",crc,(uint8_t)data[len-2]);
                     if (crc==(uint8_t)data[len-2]) 
                        {
                         memcpy (dat,data+0x6,len-6);
                         return len-8;
                         }
                     return -1;
		    }
        	}
	    if (PROTOCOL==1)
		{
	         // 00 60 75 4E 02 01 00 02 00 43 00 A9 F0
	         memcpy (dat,data+0x3,data[1]);
	         return len;
	    }
         return 0;
        }
    return	-1;
}

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
		if (type==TYPE_EVENTS) *sttime-=3600*24*365;
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
		*fntime-=3600*24;
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
		if (type==TYPE_MONTH) *sttime-=3600*24*365*2;
		if (type==TYPE_EVENTS) *sttime-=3600*24*365;
		*fntime-=3600*24;
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
