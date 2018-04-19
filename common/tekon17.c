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
#include "tekon17.h"
#include <wchar.h>
#include <locale.h>
#include <math.h>


static	uint8_t	adapter_adr=0x1;

struct tekon17_ctx {

	char *send_query;
	int  send_query_sz;

	regex_t re;

	char crc_flag;

	uint16_t crc;
};

#define get_spec(dev)	((struct tekon17_ctx *)((dev)->spec))
                                     
/* */
static int  tekon17_init(struct device *);
static void tekon17_free(struct device *);
static int  tekon17_get(struct device *, int, char **);
static int  tekon17_set(struct device *, int, char *, char **);
static int  tekon17_parse_msg(struct device *, char *, int);
static int  tekon17_parse_crc_msg(struct device *, char *, int);
static int  tekon17_check_crc(struct device *, char *, int);
static int  tekon17_send_msg(struct device *);
static int  tekon17_get_time(struct device *dev, struct tm* cur_time);
static int  tekon17_date(struct device *dev, const char *, char **);

static int  tekon17_h_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  tekon17_m_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  tekon17_d_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  tekon17_events(struct device *, const char *, const char *,
				struct events **);
static int  generate_sequence (struct device *dev, uint8_t type, uint8_t func, uint16_t padr, uint16_t index, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence, uint16_t* len);
static int  analyse_sequence (char*, uint16_t, char*, uint8_t, uint8_t, uint16_t);
static int  FloatToChar (float value, uint8_t *Data);

static int  checkdate (uint8_t	type, int ret_fr, int ret_to, struct tm* time_from, struct tm* time_to, struct tm* time_cur, time_t *tim, time_t *sttime, time_t *fntime);
static	double cChartoFloat (char *Data);

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
checkdate2 (time_t crtime, time_t sttime, time_t fntime)
{
	if (crtime>=sttime && crtime<=fntime)	{
	     //printf ("!!!check %ld %ld %ld\n",sttime,crtime,fntime);
	     return 1;
	    }
	//printf ("check %ld %ld %ld\n",sttime,crtime,fntime);
	return 0;
}

static void
ctx_zero(struct tekon17_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

static int
tekon17_get_ident(struct device *dev)
{
	static char ident_query[100];
	struct 	tekon17_ctx *ctx;
	char 	t17_id[100],answer[100];

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

	if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, T17_SERIAL, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto free_regex;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) goto out;
	buf_free(&dev->buf);

	sprintf (t17_id,"%x%x",(uint8_t)answer[1],(uint8_t)answer[0]);
	devlog(dev, "\tT17 serial=%s\n", t17_id);
	ret = 0;

free_regex:
	regfree(&ctx->re);
out:
	ctx_zero(ctx);
	buf_free(&dev->buf);
	return ret;
}

static int
tekon17_get_time(struct device *dev, struct tm* cur_time)
{
	struct 	tekon17_ctx *ctx;
	int	ret=-1;
	static 	char ident_query[100],	answer[1024];
	uint16_t	len;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));

	ctx = get_spec(dev);
	ctx->crc_flag = 0;

	while (1)	{
		if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, 0x4017, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
    		if (-1 == dev_query(dev)) break;
		if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) break;
		buf_free(&dev->buf);
		cur_time->tm_year=answer[0]*100+answer[1];

		if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, 0x4016, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
    		if (-1 == dev_query(dev)) break;
		if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) break;
		buf_free(&dev->buf);
		cur_time->tm_mday=answer[0];
		cur_time->tm_mon=answer[1];

		if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, 0x4015, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
    		if (-1 == dev_query(dev)) break;
		if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) break;
		buf_free(&dev->buf);
		cur_time->tm_hour=answer[0];
		cur_time->tm_min=answer[1];
		cur_time->tm_sec=0;
		if (!dev->quiet)
		    fprintf (stderr,"[time] %d-%02d-%02d,%02d:%02d:00\n",cur_time->tm_year,cur_time->tm_mon,cur_time->tm_mday,cur_time->tm_hour,cur_time->tm_min);
		ret=1;
		break;
	}
	regfree(&ctx->re);
	ctx_zero(ctx);
	buf_free(&dev->buf);
	free (time_from);
	free (time_to);
	return ret;
}

static int
tekon17_set_time(struct device *dev, struct tm* cur_time)
{
	struct 	tekon17_ctx *ctx;
	int	ret=-1,index=0;
	static 	char ident_query[100];
	uint16_t	len;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));

	ctx = get_spec(dev);
	ctx->crc_flag = 0;

	generate_sequence (dev, 0, CMD_STOP, 0, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len);
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) return -1;
	buf_free(&dev->buf);

	while (1)	{
		generate_sequence (dev, 6, CMD_WRITE_PARAM, 0x4007, 0xffff-0x4015, 0, PROTOCOL, time_from, time_to, ident_query, &len);
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) break;
		buf_free(&dev->buf);

		index=cur_time->tm_hour*256+cur_time->tm_min; 
		len=2;
		if (-1 == generate_sequence (dev, 6, CMD_WRITE_PARAM, 0x4015, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
    		if (-1 == dev_query(dev)) break;
		buf_free(&dev->buf);

		generate_sequence (dev, 6, CMD_WRITE_PARAM, 0x4007, 0xffff-0x4016, 0, PROTOCOL, time_from, time_to, ident_query, &len);
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) break;
		buf_free(&dev->buf);

		index=cur_time->tm_mday*256+cur_time->tm_mon;
		len=2;
		if (-1 == generate_sequence (dev, 6, CMD_WRITE_PARAM, 0x4016, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
    		if (-1 == dev_query(dev)) break;
		buf_free(&dev->buf);

		generate_sequence (dev, 6, CMD_WRITE_PARAM, 0x4007, 0xffff-0x4017, 0, PROTOCOL, time_from, time_to, ident_query, &len);
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) break;
		buf_free(&dev->buf);

		index=(cur_time->tm_year/100)*256+(cur_time->tm_year-2000);
		len=2;
		if (-1 == generate_sequence (dev, 6, CMD_WRITE_PARAM, 0x4017, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
    		if (-1 == dev_query(dev)) break;
		ret=0; break;
	}
	generate_sequence (dev, 0, CMD_START, 0, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len);
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) return -1;
	buf_free(&dev->buf);

	regfree(&ctx->re);
	ctx_zero(ctx);
	buf_free(&dev->buf);
	free (time_from);
	free (time_to);
	return ret;
}

static int  tekon17_date(struct device *dev, const char *ret, char **save)
{
	int	rt=-1;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	char *date;
	//printf ("ret=%s",ret);	
	if (strlen (ret)==0)	{
		rt = tekon17_get_time(dev, time_from);
	        date=malloc (20);
		snprintf (date,20,"%d-%02d-%02d,%02d:%02d:%02d",time_from->tm_year, time_from->tm_mon, time_from->tm_mday, time_from->tm_hour, time_from->tm_min, time_from->tm_sec);
	    	*save=date;
	    	rt=0;     
	    }
	 else	{
             rt = sscanf(ret,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
             if (rt==6)	{
		     rt =tekon17_set_time(dev, time_from);
	    	     *save=malloc (10);
    		     snprintf (*save,10,"ok");
    		     rt=0;
    		    }
    	    else	{
	        //rt = tekon19_set_string_param(dev, addr, (char *)ret, type, param);
    	         *save=malloc (10);
	         snprintf (*save,10,"error");
	        }
	    }
	return	rt;
}


static int
tekon17_get_string_param(struct device *dev, uint16_t addr, char **save, uint8_t type, uint16_t padr)
{
	struct tekon17_ctx *ctx;
	float	value;
	char	*value_string;
	uint16_t	value_uint=0;
	static char ident_query[100],	answer[1024];
	void 		*fnsave;
	int 		ret;
	uint16_t	len,i;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	//time_t 	tim;
	//tim=time(&tim);			// default values
	//time_from=localtime(&tim); 	// get current system time
	//time_to=localtime(&tim); 	// get current system time

	ctx = get_spec(dev);
	ret = -1;

	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = tekon17_parse_crc_msg;

	if (type != T17_TYPE_DATE)	{
		if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, addr, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) goto out;
		len = analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr);
		if (-1 == len) goto out;
		}

	//value_string=malloc (100);
	switch	(type)
	    {
		case	T17_TYPE_DATE:		if (tekon17_get_time(dev, time_from))	{
    	        				     if (!dev->quiet)
    	        				        fprintf (stderr,"%s\n",value_string);
    	        				     value_string=malloc (100);
    	        				     snprintf (value_string,90,"%d-%02d-%02d,%02d:%02d:%02d",time_from->tm_year, time_from->tm_mon, time_from->tm_mday, time_from->tm_hour, time_from->tm_min, time_from->tm_sec);
    	        				     *save=value_string;
						     ret = 0;
						    }
						break;

		case	T17_TYPE_NOTDEFINED:	break;
		case	T17_TYPE_FLOAT:		value=cChartoFloat (answer);
						value_string=malloc (100);
						snprintf (value_string,100,"%f",value);
						*save=value_string;
						if (!dev->quiet)
						    fprintf (stderr,"%s [%x %x %x %x]\n",value_string,answer[0],answer[1],answer[2],answer[3]);
						ret=0;
						break;
		case	T17_TYPE_INT:		value_string=malloc (100);
						for (i=0; i<len;i++)
	    					    value_uint+=(answer[i])*(pow(256,i));
	    					snprintf (value_string,100,"%d",value_uint);	    					
	    					*save=value_string;
	    					if (!dev->quiet)
	    					    fprintf (stderr,"%s\n",value_string);
	    					ret=0;
						break;
		default:			//value_string=NULL;
						ret=0;
						break;
	    }
out:
	regfree(&ctx->re);
	/* restore handler */
	dev->opers->parse_msg = fnsave;
	free (time_from);
	free (time_to);
	buf_free(&dev->buf);
	ctx_zero(ctx);
	return ret;
}

static int
tekon17_set_string_param(struct device *dev, uint16_t addr, char *save, uint8_t type, uint16_t padr)
{
	struct tekon17_ctx *ctx;
	static char 	ident_query[100],	answer[1024];
	static uint8_t	str_value[10];
	void 		*fnsave;
	int 		ret;
	uint16_t	len,index;
	float	value;
	struct 	tm *time_from,*time_to;
	time_t 	tim;
	tim=time(&tim);			// default values
	time_from=localtime(&tim); 	// get current system time
	time_to=localtime(&tim); 	// get current system time

	ctx = get_spec(dev);
	ret = -1;

	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = tekon17_parse_crc_msg;

	buf_free(&dev->buf);
	if (-1 == generate_sequence (dev, 3, CMD_STOP, 0, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;

	buf_free(&dev->buf);
	if (-1 == generate_sequence (dev, 4, CMD_WRITE_PARAM, WRITE_PASS, 0xFFFF-DEVICE_PASS, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;

	buf_free(&dev->buf);
	if (-1 == generate_sequence (dev, 4, CMD_WRITE_PARAM, DEVICE_PASS, PASSWORD, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;


	if (type!=T17_TYPE_DATE)	{
		buf_free(&dev->buf);
		if (-1 == generate_sequence (dev, 4, CMD_WRITE_PARAM, WRITE_PASS, 0xFFFF-addr, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (-1 == dev_query(dev)) goto out;
		if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
		}

	switch	(type)
	    {
		case	T17_TYPE_DATE:		ret = sscanf(save,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
    	        				if (ret && 0 == tekon17_set_time(dev, time_from))
						    ret = 0;
						goto out;
		case	T17_TYPE_FLOAT:		sscanf (save,"%f",&value);
						FloatToChar (value, str_value);
						memcpy	(ident_query,str_value,4);
						printf ("%f (%x %x %x %x)\n",value,ident_query[0],ident_query[1],ident_query[2],ident_query[3]);
						len=4;
						break;
		case	T17_TYPE_INT:		ident_query[0]=atoi(save)%256;
						ident_query[1]=atoi(save)/256;
						printf ("%x|%x %d %s %d\n",ident_query[0],ident_query[1],len,save,atoi(save));
						len=2;
						break;
		case	T17_TYPE_NOTDEFINED:
		default:			goto out;
	    }
	    
	buf_free(&dev->buf);
	index=8;
	if (-1 == generate_sequence (dev, 17, CMD_WRITE_PARAM, addr, index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;

/*
	buf_free(&dev->buf);
	if (-1 == generate_sequence (dev, 7, CMD_WRITE_PARAM, addr, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
*/
out:
	buf_free(&dev->buf);
	if (-1 == generate_sequence (dev, 3, CMD_START, addr, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
	buf_free(&dev->buf);

	regfree(&ctx->re);
//	free(ctx->send_query);
	dev->opers->parse_msg = fnsave;
	buf_free(&dev->buf);
	ctx_zero(ctx);
	return ret;
}

/*
 * Interface
 */
static struct vk_operations tekon17_opers = {
	.init	= tekon17_init,
	.free	= tekon17_free,
	.get	= tekon17_get,
	.set	= tekon17_set,
	.date	= tekon17_date,

	.send_msg  = tekon17_send_msg,
	.parse_msg = tekon17_parse_msg,
	.check_crc = tekon17_check_crc,

	.h_archiv = tekon17_h_archiv,
	.m_archiv = tekon17_m_archiv,
	.d_archiv = tekon17_d_archiv,

	.events = tekon17_events
};

void
tekon17_get_operations(struct vk_operations **p_opers)
{
	*p_opers = &tekon17_opers;
}

static int
tekon17_init(struct device *dev)
{
	struct tekon17_ctx *spec;
	int ret=-1;

	spec = calloc(1, sizeof(*spec));

	if (!spec) {
		set_system_error("calloc");
		ret = -1;
	} else {
		dev->spec = spec;
		ret =   (-1 == tekon17_get_ident(dev));
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
tekon17_free(struct device *dev)
{
	struct tekon17_ctx *spec = get_spec(dev);

	free(spec);
	dev->spec = NULL;
}

static int
tekon17_get_ns (int no, int nom, char* code)
{
    int i;
    //printf ("ns %d %d\n",no,nom);
    for (i=0; i<ARRAY_SIZE(nscodeT17); i++)
	if (no == nscodeT17[i].id && (nscodeT17[i].nid==255 || nscodeT17[i].nid==nom))
	    {
	    snprintf (code,75,"%s",nscodeT17[i].name);
	    return 1;
	    }
    return -1;
}

static int
tekon17_get(struct device *dev, int param, char **ret)
{
	int 		rv=0;
	uint16_t	i=0;

	//printf ("d=%d\n",param);
	for (i=0; i<ARRAY_SIZE(currents17); i++)
	if (param == currents17[i].no) {
	    rv = tekon17_get_string_param(dev, currents17[i].adr, ret, currents17[i].type, currents17[i].type);
    	    return 0;
	}
	//printf ("%d\n",sizeof(ret));
    	//strcpy (ret,mas);
	return rv;
}

static int
tekon17_set(struct device *dev, int param, char *ret, char **save)
{
	int 		rv=0,i=0;
	for (i=0; i<ARRAY_SIZE(currents17); i++)
		if (currents17[i].no==param) {
		    rv = tekon17_set_string_param(dev, currents17[i].adr, ret, currents17[i].type, param);
		    }
	return rv;
}

static int
make_archive(struct device *dev, int no, AnsLog* alog, int type, struct archive **save)
{
	struct archive *archives = NULL, *lptr = NULL, *ptr;
	int nrecs,i,ret,np=0;
	struct 	tm *tim=malloc (sizeof (struct tm));

	ret = -1;
	nrecs = alog->ind[0];
	if (nrecs <= 0) return -1;
	if (!dev->quiet)
		fprintf(stderr, "RECS: %d\n", nrecs);

	
	for (i = 0; i < nrecs; ++i) 
		{
		 if ((ptr = alloc_archive()) != NULL) 
			{
			 ptr->num = i;

			 if (type==TYPE_HOURS)
			 for (np=0; np<ARRAY_SIZE(hours17); np++)
				{
    				 snprintf(ptr->datetime, sizeof(ptr->datetime),"%s",alog->time[hours17[np].id][i]);
    				 ptr->params[hours17[np].id] = atof (alog->data[hours17[np].id][i]);
    				 if (!dev->quiet)
    				    fprintf (stderr,"[%s] %f\n",ptr->datetime,ptr->params[hours17[np].id]);
    				}
    			 if (type==TYPE_DAYS)
			 for (np=0; np<ARRAY_SIZE(days17); np++)
				{
    				 snprintf(ptr->datetime, sizeof(ptr->datetime),"%s",alog->time[days17[np].id][i]);
    				 ptr->params[days17[np].id] = atof (alog->data[days17[np].id][i]);
    				 if (!dev->quiet)
    				    fprintf (stderr,"[%s] %f\n",ptr->datetime,ptr->params[days17[np].id]);
    				}
    			 if (type==TYPE_MONTH)
			 for (np=0; np<ARRAY_SIZE(month17); np++)
				{
    				 snprintf(ptr->datetime, sizeof(ptr->datetime),"%s",alog->time[month17[np].id][i]);
    				 ptr->params[month17[np].id] = atof (alog->data[month17[np].id][i]);
    				 if (!dev->quiet)
    				    fprintf (stderr,"[%s] %f\n",ptr->datetime,ptr->params[month17[np].id]);
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
	*save = archives;
	free (tim);
	return ret;
}

static int 
tekon17_h_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct 		tekon17_ctx *ctx;
	int 		ret,day;
	int 		ret_fr,ret_to;

	uint16_t 	len,adr=0,i=0,hour=0;
	static char 	ident_query[100],answer[1024];
	AnsLog		alog;
	float		value;
	uint16_t	index=0,marker=0xff;

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	time_t 	tim,sttime,fntime,crtime;

	ctx = get_spec(dev);
	ctx->crc_flag = 0;
	alog.quant_param=0;

	if (generate_sequence (dev, 0, CMD_READ_PARAM, 0x4114, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len) == 0)	{
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
		if (0 == dev_query(dev)) 
		    if (analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0) > 0)	{
			marker=answer[0];
			//printf ("marker=%d\n",marker);
			}
		buf_free(&dev->buf);
		}

	ret = tekon17_get_time(dev, time_cur);
	if (ret == 1 && marker!=0xff)	{
		//fntime=mktime (time_to);
		//sttime=mktime (time_from);
		ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
    	        ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
    		tim=time(&tim);
		localtime_r(&tim,time_cur);
		ret=checkdate (TYPE_HOURS,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
		//localtime_r(&fntime,time_to);

		for (i=0; i<ARRAY_SIZE(hours17); i++)
		    {
			alog.ind[i]=0; hour=0;
			alog.quant_param=0;
			ret = tekon17_get_time(dev, time_cur);
			time_cur->tm_year-=1900;
			time_cur->tm_mon-=1;
			crtime=mktime (time_cur);
			for (day=0x60;day>=0x0;day-=0x20)		//	last 3 days
			    {
		            if (checkdate2(crtime,sttime,fntime))
				{
			        index=0xE000+day+(hours17[i].adr);
			        if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, index, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
			        ctx->send_query = ident_query;
			        ctx->send_query_sz = len;
    			        if (-1 == dev_query(dev)) continue;
    			        len = analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0);
			        buf_free(&dev->buf);
			        if (len<1) continue;
			        for (adr=0,hour=0;adr<len-2;adr+=4)
			    	    {
				     sprintf (alog.time[hours17[i].id][alog.ind[i]],"%04d-%02d-%02d,%02d:00:00",time_cur->tm_year+1900,time_cur->tm_mon+1,time_cur->tm_mday,hour);
			             if (answer[adr]!=0x0)	{
			        	value=cChartoFloat (answer+adr);
					sprintf (alog.data[hours17[i].id][alog.ind[i]],"%f",value);
					if (!dev->quiet)
			        	    fprintf (stderr,"[%s] %s [0x%x 0x%x 0x%x 0x%x ] [%d/%d]\n",alog.time[hours17[i].id][alog.ind[i]],alog.data[days17[i].id][alog.ind[i]],answer[adr],answer[adr+1],answer[adr+2],answer[adr+3],hours17[i].id,alog.ind[i]);
					alog.ind[i]++;
			    		}
			    	     else	{
					 sprintf (alog.data[hours17[i].id][alog.ind[i]],"no data");
					 if (!dev->quiet)
			    		    fprintf (stderr,"[%s] - [0x%x 0x%x 0x%x 0x%x ] [%d/%d]\n",alog.time[hours17[i].id][alog.ind[i]],answer[adr],answer[adr+1],answer[adr+2],answer[adr+3],hours17[i].id,alog.ind[i]);
			    		 alog.ind[i]++;
			    		}
				     hour++;
			    	    }
			        }
			    crtime=mktime (time_cur);
			    crtime-=3600*24;
			    localtime_r(&crtime,time_cur);
			    //time_cur->tm_year+=1900;
			    time_cur->tm_hour=0;
			    }

    			if (marker>=0x20)
				index=0xE000+(marker-0x20)*256+0xA0+(hours17[i].adr);
			else
				index=0xE000+(marker)*256+0x80+(hours17[i].adr);
				
			for (day=0;day<60;day++)	{
		                if (checkdate2(crtime,sttime,fntime))
					{
					 if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, index, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
			    		 ctx->send_query = ident_query;
			    		 ctx->send_query_sz = len;
    			    		 if (-1 == dev_query(dev)) {
    			    		    buf_free(&dev->buf);
    			    		    continue;
    			    		    }
    			    		 len = analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0);
			    		 buf_free(&dev->buf);
			    		 if (len<10) continue;
			    		 //printf ("!stind=%d\n",stind);
			    		 for (hour=0,adr=0;adr<len;adr+=4)
			    		    {
				    	     sprintf (alog.time[hours17[i].id][alog.ind[i]],"%04d-%02d-%02d,%02d:00:00",time_cur->tm_year+1900,time_cur->tm_mon+1,time_cur->tm_mday,hour);
			            	     if (answer[adr]!=0x0)	{
			        		    value=cChartoFloat (answer+adr);
						    sprintf (alog.data[hours17[i].id][alog.ind[i]],"%f",value);
						    if (!dev->quiet)
			        			fprintf (stderr,"[%s] %s [0x%x 0x%x 0x%x 0x%x ] [%d/%d]\n",alog.time[hours17[i].id][alog.ind[i]],alog.data[days17[i].id][alog.ind[i]],answer[adr],answer[adr+1],answer[adr+2],answer[adr+3],hours17[i].id,alog.ind[i]);
						    alog.ind[i]++;
			    			    }
			    	    	     else	{
						    sprintf (alog.data[hours17[i].id][alog.ind[i]],"no data");
						    if (!dev->quiet)
			    				fprintf (stderr,"[%s] - [0x%x 0x%x 0x%x 0x%x ] [%d/%d]\n",alog.time[hours17[i].id][alog.ind[i]],answer[adr],answer[adr+1],answer[adr+2],answer[adr+3],hours17[i].id,alog.ind[i]);
			    			    alog.ind[i]++;
			    			    }
				    	     hour++;
			    		    }
					}
				index-=256;
		        	crtime-=3600*24;
				localtime_r(&crtime,time_cur);
				//time_cur->tm_year+=1900;
				time_cur->tm_hour=0;
			    }
			//printf ("stind=%d\n",stind);
			//if (stind>maxind)
			//	maxind=stind;
		    }
	     //alog.quant_param=maxind;
	    }
	ret = make_archive(dev, no, &alog, TYPE_HOURS, save);
    	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_cur);
	free (time_from);
	free (time_to);
	return ret;
}

static int 
tekon17_d_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct 		tekon17_ctx *ctx;
	int 		ret;
	int 		ret_fr,ret_to;
	uint16_t 	len,adr=0,i=0,day=0,ind=0,stind=0;
	static char 	ident_query[100],	answer[1024];
	AnsLog		alog;
	uint16_t	index=0;
	float		value;
	time_t 	tim,sttime,fntime,crtime;

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	ctx = get_spec(dev);
	ctx->crc_flag = 0;
	alog.quant_param=0;

	ret = tekon17_get_time(dev, time_cur);
	if (ret == 1)	{
		ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
    	        ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
    		tim=time(&tim);
		localtime_r(&tim,time_cur);
		ret=checkdate (TYPE_DAYS,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
		localtime_r(&sttime,time_from);
		localtime_r(&fntime,time_to);
		
		ret = tekon17_get_time(dev, time_cur);
		//printf ("%d-%02d-%02d,%02d:%02d:%02d - %d-%02d-%02d,%02d:%02d:%02d - %d-%02d-%02d,%02d:%02d:%02d\n",time_from->tm_year+1900, time_from->tm_mon+1, time_from->tm_mday, time_from->tm_hour, time_from->tm_min, 0, time_cur->tm_year, time_cur->tm_mon, time_cur->tm_mday, time_cur->tm_hour, time_cur->tm_min, 0, time_to->tm_year+1900, time_to->tm_mon+1, time_to->tm_mday, time_to->tm_hour, 0, 0);
		for (i=0; i<ARRAY_SIZE(days17); i++)
		    {
		     time_from->tm_mday=1;
		     index=0xE0C0+(time_from->tm_mday-1)*256+(0x6+days17[i].adr);
		     if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, index, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
		     ctx->send_query = ident_query;
		     ctx->send_query_sz = len;
		     if (-1 == dev_query(dev)) {
		         buf_free(&dev->buf);
		         continue;
		        }
		     len = analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0);
		     buf_free(&dev->buf);
		     if (len<1) break;
		     alog.quant_param=0;
		     //printf ("time_from->tm_mday=%d | time_from->tm_mon=%d\n",time_from->tm_mday,time_from->tm_mon);
		     for (day=time_from->tm_mday,adr=0,ind=0;day<32;adr+=4,day++)
		     if (time_from->tm_mon==time_to->tm_mon-1)
		        {
			 time_from->tm_mday=day;
		         crtime=mktime (time_from);
		         if (checkdate2(crtime,sttime,fntime))
		            {
			     sprintf (alog.time[days17[i].id][ind],"%04d-%02d-%02d,00:00:00",time_from->tm_year+1900,time_from->tm_mon+1,day);
		             if (answer[adr]!=0x0)	{
		        	    value=cChartoFloat (answer+adr);
				    sprintf (alog.data[days17[i].id][ind],"%f",value);
		        	    //printf ("[%s] %s [0x%x 0x%x 0x%x 0x%x ] [%d/%d]\n",alog.time[days17[i].id][ind],alog.data[days17[i].id][ind],answer[adr],answer[adr+1],answer[adr+2],answer[adr+3],hours17[i].id,ind);
				    if (!i) alog.quant_param++;
				    ind++;
		    		    }
		    	     else	{
				    sprintf (alog.data[days17[i].id][ind],"no data");
		    		    //printf ("[%s] - [0x%x 0x%x 0x%x 0x%x ] [%d/%d]\n",alog.time[days17[i].id][ind],answer[adr],answer[adr+1],answer[adr+2],answer[adr+3],hours17[i].id,ind);
		    		    }
		    	    }
		        }
			time_from->tm_mon--;
		         //localtime_r (&sttime,time_from);
			 //time_from->tm_year=time_from->tm_year+1900;
			 //time_from->tm_mon++;
		    }
		stind=ind;
        	//time_from->tm_mday=time_to->tm_mday;
        	time_from->tm_mon=time_to->tm_mon;
        	time_from->tm_year=time_to->tm_year;
		for (i=0; i<ARRAY_SIZE(days17); i++)
			    {
			     if (time_cur->tm_year==time_from->tm_year && time_cur->tm_mon==time_from->tm_mon)
			    	    {
			    	     index=0xE0C0+(time_from->tm_mday-1)*256+(days17[i].adr);
			    	     day=time_from->tm_mday;
			    	    }
			     else	{
			    	     index=0xE0C0+(days17[i].adr);
			    	     day=1;
			    	     }
			     if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, index, 0, 0, PROTOCOL, time_to, time_to, ident_query, &len)) break;
			     ctx->send_query = ident_query;
			     ctx->send_query_sz = len;
    			     if (-1 == dev_query(dev))	{
    			    	   buf_free(&dev->buf);
    			           continue;
    			         }
    			     len = analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0);
			     buf_free(&dev->buf);
			     if (len<1) break;
			     for (adr=0,ind=stind;day<=time_to->tm_mday;adr+=4)
			        {
			         time_from->tm_mday=day;
		        	 crtime=mktime (time_from);
		                 if (checkdate2(crtime,sttime,fntime))
					{
					 sprintf (alog.time[days17[i].id][ind],"%04d-%02d-%02d 00:00:00",time_to->tm_year+1900,time_to->tm_mon+1,day);
			        	 if (answer[adr]!=0x0)	{
			        		 value=cChartoFloat (answer+adr);
						 sprintf (alog.data[days17[i].id][ind],"%f",value);
			        		 //printf ("[%s] %s [0x%x 0x%x 0x%x 0x%x ] [%d/%d]\n",alog.time[days17[i].id][ind],alog.data[days17[i].id][ind],answer[adr],answer[adr+1],answer[adr+2],answer[adr+3],hours17[i].id,ind);
						 if (!i) alog.quant_param++;
						 ind++;
			    			}
			    		 else	{
						 sprintf (alog.data[days17[i].id][ind],"no data");
			    			 //printf ("[%s] - [0x%x 0x%x 0x%x 0x%x ] [%d/%d]\n",alog.time[days17[i].id][ind],answer[adr],answer[adr+1],answer[adr+2],answer[adr+3],hours17[i].id,ind);
			    			}
			    		}
				 day++;
			        }
			}
		alog.quant_param=alog.ind[0]=ind;
	    }
	ret = make_archive(dev, no, &alog, TYPE_DAYS, save);
    	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_cur);
	free (time_from);
	free (time_to);
	return ret;
}

static int 
tekon17_m_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct 		tekon17_ctx *ctx;
	int 		ret;
	int 		ret_fr,ret_to;
	uint16_t 	len,i=0,ind=0,mon=0;
	static char 	ident_query[100],	answer[1024];
	AnsLog		alog;
	uint16_t	index=0;
	float		value=0;
	struct 	tm *time_cur=malloc (sizeof (struct tm));
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));
	ctx = get_spec(dev);
	ctx->crc_flag = 0;
	alog.quant_param=0;
	time_t 	tim,sttime,fntime;

	ret = tekon17_get_time(dev, time_cur);
	if (ret == 1)	{
		ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
    	        ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
    		tim=time(&tim);
		localtime_r(&tim,time_cur);
		ret=checkdate (TYPE_MONTH,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);
		
    		ret = tekon17_get_time(dev, time_cur);
		time_from->tm_year=time_from->tm_year+1900;
		time_to->tm_year=time_to->tm_year+1900;
		//printf ("%d=%d | %d=%d\n",time_cur->tm_year,time_from->tm_year,time_from->tm_mon,time_to->tm_mon);

		if (time_cur->tm_year==time_from->tm_year)
			{
			for (i=0; i<ARRAY_SIZE(month17); i++)
			    {
				ind=0;
			        //for (mon=time_from->tm_mon;mon<time_to->tm_mon;mon++)
			        for (mon=1;mon<=12;mon++)
				    {
			    	     index=0xC080+(mon-1)*256+(month17[i].adr);
			    	     if (-1 == generate_sequence (dev, 0, CMD_READ_PARAM, index, 0, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
			    	     ctx->send_query = ident_query;
			    	     ctx->send_query_sz = len;
    			    	     if (-1 == dev_query(dev)) {
    			    	        buf_free(&dev->buf);
    			    	        continue;
    			    	        }
    			    	     len = analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0);
			    	     buf_free(&dev->buf);
			    	     if (len<1) break;
			    	     sprintf (alog.time[month17[i].id][ind],"%04d-%02d-01 00:00:00",time_from->tm_year,mon);
			             if (answer[0]!=0x0)	{
			    	    	    value=cChartoFloat (answer);
				    	    sprintf (alog.data[month17[i].id][ind],"%f",value);
				    	    }
				     else
					 sprintf (alog.data[month17[i].id][ind],"no data");
				     //printf ("%s|%s\n",alog.time[month17[i].id][ind],alog.data[month17[i].id][ind]);				     
			    	     ind++;
			    	    }
			    	//if (ind>maxind) maxind=ind;
			    }
			}
	    }
	alog.quant_param=alog.ind[0]=12;
	ret = make_archive(dev, no, &alog, TYPE_MONTH, save);
    	buf_free(&dev->buf);
 	regfree(&ctx->re);
	ctx_zero(ctx);
	free (time_cur);
	free (time_from);
	free (time_to);
	return ret;
}

static int
make_event(char *times, char *events, struct events **save, uint16_t enm)
{
 struct events *ev;
 ev = malloc(sizeof(*ev));
 if (!ev) {
	set_system_error("malloc");
	 return -1;
	}
 ev->num = enm;
 ev->event = 1;
 snprintf(ev->datetime, sizeof(ev->datetime),"%s",times);
 ev->next = NULL;
 *save = ev;
 return 1;
}


static int
tekon17_events(struct device *dev, const char *from, const char *to, struct events **save)
{
	struct tekon17_ctx *ctx;
	AnsLog alog;
	struct 	events *ev, *pev=NULL, *top=NULL;
	int ret=-1,i;
	uint16_t len,index=0;
	char	code[100];
	static char ident_query[100], answer[1024];
	//time_t sttime,fntime;

	ctx = get_spec(dev);
	ctx->crc_flag = 1;

	struct 	tm *time_from=malloc (sizeof (*time_from));
	struct 	tm *time_to=malloc (sizeof (*time_to));
	struct 	tm *time_cur=malloc (sizeof (*time_cur));

	ret = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
	time_from->tm_mon-=1; time_from->tm_year-=1900;
	//sttime=mktime (time_from);

	ret = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);
	time_to->tm_mon-=1; time_to->tm_year-=1900;
	//fntime=mktime (time_to);

        index=0;
	while (index<=7)
	    {
	    if (-1 == generate_sequence (dev, 6, CMD_READ_INDEX_PARAM, 0x4046+(index*0x100), index, 0, PROTOCOL, time_from, time_to, ident_query, &len)) break;
	    ctx->send_query = ident_query;
	    ctx->send_query_sz = len;
	    if (-1 == dev_query(dev)) break;
	    ret = analyse_sequence (dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0);
	    if (ret==-1 || dev->buf.len<10) 
		{
		 buf_free(&dev->buf);
		 index++;
		 ret=-1;
		 continue;
		}
	    buf_free(&dev->buf);
	    for (i=0;i<ret;i+=8)
		{
		if (answer[i]>0x0)	{
		    if (!dev->quiet)
    			fprintf (stderr,"%d %d %d\n",answer[0],answer[1],answer[2]); }
		 if (i>0 && i<255) 
		    ret=tekon17_get_ns (i, answer[5], code);
		 if (ret==-1)
		    fprintf (stderr,"unknown error\n");
		 else
		    {
		     //printf ("[%d] [%s] (0x%x,0x%x,0x%x,0x%x) (%s)\n",index,alog.time[0][alog.quant_param],alog.data[0][alog.quant_param][0],alog.data[0][alog.quant_param][1],alog.data[0][alog.quant_param][2],alog.data[0][alog.quant_param][3],code);
		     snprintf (alog.data[0][alog.quant_param],120,"%s (0x%x,0x%x,0x%x,0x%x)",code,(uint8_t)alog.data[0][alog.quant_param][0],(uint8_t)alog.data[0][alog.quant_param][1],(uint8_t)alog.data[0][alog.quant_param][2],(uint8_t)alog.data[0][alog.quant_param][3]);
		     if (alog.quant_param>=ARCHIVE_NUM_MAX) break;
		     alog.quant_param++;
		     }
		}
	     ret = 1;
	     index++;
	    }
	buf_free(&dev->buf);
	ev = malloc(sizeof(*ev));
	if (!ev) {
		set_system_error("malloc");
	 	regfree(&ctx->re);
		ctx_zero(ctx);
		ret = -1;
	}

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
		ev->event = atoi(alog.data[0][i]);
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
	return ret;
}

static int
tekon17_send_msg(struct device *dev)
{
	struct tekon17_ctx *ctx = get_spec(dev);
	return dev_write(dev, ctx->send_query, ctx->send_query_sz);
}

static int
tekon17_parse_msg(struct device *dev, char *p, int psz)
{
    //struct tekon17_ctx *ctx;
    int ret=-1;
    char answer[1024];

    //ctx = get_spec(dev);
    if (!dev->quiet)
	fprintf(stderr, "\rParsing: %10d bytes", psz);
    ret=analyse_sequence (p, psz, answer, ANALYSE, 0, 0);
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
tekon17_parse_crc_msg(struct device *dev, char *p, int psz)
{
    //struct tekon17_ctx *ctx;
    int ret;
    char answer[1024];
    //ctx = get_spec(dev);
    if (!dev->quiet)
	fprintf(stderr, "\rParsing: %10d bytes", psz);

    ret=analyse_sequence (p, psz, answer, ANALYSE, 0, 0);
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
tekon17_check_crc(struct device *dev, char *p, int psz)
{
    struct tekon17_ctx *ctx;
    int ret;
    char answer[1024];

    ctx = get_spec(dev);
    if (ctx->crc_flag) {
		//printf ("tekon17_check_crc\n");
		ret=analyse_sequence (p, psz, answer, ANALYSE, 0, 0);
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
        case 0:		sprintf (buffer,"%c%c%c%c%c%c%c",0x10,0x40,dev->devaddr&0xFF,(uint8_t)func,(padr&0xff00)>>8,padr&0xff,0);
            		buffer[7]=calc_bcc ((uint8_t *)buffer+1, 6);
            		buffer[8]=0x16;
            		ln=9;
	    		if (!dev->quiet)
	    		    fprintf (stderr,"[tek2] wr[%d] [0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8]);
	    		break;
	case 1: 	// 68 07 07 68 46 01 15 01 09 FF 00 65 16
			sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c%c",0x68,0x9,0x9,0x68,0x44,adapter_adr,0x19,dev->devaddr&0xFF,padr&0xff,(padr&0xff00)>>8,index&0xff,(index&0xff00)>>8,1);
    			buffer[13]=calc_bcc ((uint8_t *)buffer+4, 9);
    			buffer[14]=0x16;
			ln=15;
			if (!dev->quiet)
        		    fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13]);
			break;
        case 3:		sprintf (buffer,"%c%c%c%c%c%c%c",0x10,0x40,dev->devaddr&0xFF,(uint8_t)func,0,0,0);
            		buffer[7]=calc_bcc ((uint8_t *)buffer+1, 6);
            		buffer[8]=0x16;
            		ln=9;
	    		if (!dev->quiet)
				fprintf(stderr,"[tek2] wr[%d] [0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8]);
	    		break;

	case 4:		sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c",0x68,0x7,0x7,0x68,0x40,dev->devaddr&0xFF,0x5,(padr&0xff00)>>8,padr&0xff,(index&0xff00)>>8,index&0xff);
    			buffer[11]=calc_bcc ((uint8_t *)buffer+4, 7);
    			buffer[12]=0x16;
			ln=13;
        		if (!dev->quiet)
				fprintf(stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13]);
			break;
			
	case 6: 	sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c",0x68,0x7,0x7,0x68,0x40,dev->devaddr&0xFF,CMD_WRITE_PARAM,(padr&0xff00)>>8,padr&0xff,(index&0xff00)>>8,index&0xff);
    			buffer[11]=calc_bcc ((uint8_t *)buffer+4, 7);
    			buffer[12]=0x16;
			ln=13;
			if (!dev->quiet)
        		    fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12]);
			break;
	case 7: 	sprintf (buffer,"%c%c%c%c%c%c%c%c%c",0x68,0x5+(*len),0x5+(*len),0x68,0x44,dev->devaddr&0xFF,(uint8_t)func,(padr&0xff00)>>8,padr&0xff);
			memcpy (buffer+9,sequence,*len);
			ln=*len+10;
    			buffer[*len+9]=calc_bcc ((uint8_t *)buffer+4, *len+5);
    			ln=*len+11; printf ("sd=%d\n",ln);
    			buffer[*len+10]=0x16;
			ln=*len+11;
			if (!dev->quiet)
        		    fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13]);
			break;			

	case 17: 	sprintf (buffer,"%c%c%c%c%c%c",0x10,0x40,dev->devaddr&0xFF,(uint8_t)func,(padr&0xff00)>>8,padr&0xff);
			memcpy (buffer+6,sequence,*len);
			ln=*len+7;
    			buffer[*len+6]=calc_bcc ((uint8_t *)buffer+1, *len+2);
    			ln=*len+8; printf ("sd=%d\n",ln);
    			buffer[*len+7]=0x16;
			ln=*len+8;
			break;
			
	case 8: 	sprintf (buffer,"%c%c%c%c%c%c%c%c%c%c%c%c",0x68,0x8,0x8,0x68,0x44,adapter_adr,CMD_SET_ACCESS_LEVEL,0x2,00,22,22,22);
    			buffer[12]=calc_bcc ((uint8_t *)buffer+4, 10);
    			buffer[13]=0x16;
			ln=14;
        		if (!dev->quiet) 
        			fprintf (stderr,"[tek2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13]);
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
analyse_sequence (char* data, uint16_t len, char* dat, uint8_t analyse, uint8_t id, uint16_t padr)
{
    uint16_t	crc=0;          //(* CRC checksum *)
    uint8_t	i=0;
    for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)data[i],(uint8_t)data[i]);

    if (analyse==NO_ANALYSE)
	if (data[len-1]!=0x16 || data[len]==0x16)
	    return 0;
    if (len==1 && (uint8_t)data[0]==0xa2)
	    return 1;

    if (len>5)
        {
	 if (data[len-1]!=0x16 || data[len]==0x16) return 0;
    	    //for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)data[i],(uint8_t)data[i]);
            //if (PROTOCOL==2)
    	    //printf ("\n[tek2] rd[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",len,(uint8_t)data[0],(uint8_t)data[1],(uint8_t)data[2],(uint8_t)data[3],(uint8_t)data[4],(uint8_t)data[5],(uint8_t)data[6],(uint8_t)data[7],(uint8_t)data[8],(uint8_t)data[9]);
	    //else
	    //	printf ("\n[tek2] rd[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",len,(uint8_t)data[0],(uint8_t)data[1],(uint8_t)data[2],(uint8_t)data[3],(uint8_t)data[4],(uint8_t)data[5],(uint8_t)data[6],(uint8_t)data[7],(uint8_t)data[8],(uint8_t)data[9],(uint8_t)data[10],(uint8_t)data[11],(uint8_t)data[12]);
	    if (PROTOCOL==2)
		{
                if (data[0]==0x10)
	    	    {
    	             crc=calc_bcc ((uint8_t*)data+1, len-3);
    	             //printf ("%x %x\n",crc,(uint8_t)data[len-2]);
        	     if (crc==(uint8_t)data[len-2])
            	        {
            	         memcpy (dat,data+0x3,4);
	                 return len-5;
	                }
	             return -1;
    		    }
                if (data[0]==0x68)
        	    {
                     crc=calc_bcc ((uint8_t*)data+4, len-6);
    	             //printf ("%x %x\n",crc,(uint8_t)data[len-2]);
                     if (crc==(uint8_t)data[len-2]) 
                        {
                         memcpy (dat,data+0x6,len-6);
                         //for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)data[i],(uint8_t)data[i]);
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
//----------------------------------------------------------------------------------------
double cChartoFloat (char *Data)
{
	uint8_t sign;
	int	exp,j;
	double res=0,zn=0.5, tmp;
	uint8_t mask;
	uint16_t i;
	if (*(Data+1)&0x80) sign=1; else sign=0;
	exp = ((*(Data+0)&0xff))-128;
	for (j=1;j<=3;j++)
    	    {
            mask = 0x80;
	    for (i=0;i<=7;i++)
	    	{
	    	 if (j==1&&i==0) { mask = mask/2;}
	    	 else {
	    		res = (*(Data+j)&mask)*zn/mask + res;
	    		mask = mask/2; zn=zn/2;
	    	    }
	    	}
	    }
	res = res * pow (2,exp);
	//printf ("}} %f %d (%x %x %x %x)\n",res,exp,*(Data+3),*(Data+2),*(Data+1),*(Data+0));
	tmp = 1*pow (10,-15);
	if (res<tmp) res=0;
	if (sign) res = -res;
	return res;
}
//----------------------------------------------------------------------------------------
static int 
FloatToChar (float value, uint8_t *Data)
{
	uint8_t sign;
	double res;
	uint8_t val[4];
	uint32_t exp,mant;

	memcpy (val,&value,4);
	sign=(val[3]&0x80)>>7;
	exp=(val[3]&0x7f)*2+(val[2]&0x80)/0x80;
	mant=((val[2])*0x10000+val[1]*0x100+val[0]);
	mant/=2;
	Data[0]=exp+2;
	Data[1]=sign*0x80+0x40+(mant&0x3f0000)/0x10000;
	Data[2]=(mant&0xff00)/256;
	Data[3]=(mant&0xff);

/*
	memcpy (val,&value,4);
	//printf ("%x %x %x %x\n",val[0],val[1],val[2],val[3]);
	sign=(val[3]&0x80)>>7;
	exp=(val[3]&0x7f)*2+(val[2]&0x80)/0x80;
	mant=((val[2])*0x10000+val[1]*0x100+val[0])/2;
	//printf ("s%x e%x(%d) m%x(%d)\n",sign,exp,exp,mant,mant);

	Data[0]=exp+2;
	Data[1]=sign*0x80+0x40+(mant&0x7f0000)/0x10000;
	Data[2]=(mant&0xff00)/256;
	Data[3]=(mant&0xff);
	res=cChartoFloat ((char*)Data);*/
	//printf ("res=%f (%x %x %x %x)\n",res,*(Data+0),*(Data+1),*(Data+2),*(Data+3));
	return 1;
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
		if (type==TYPE_DAYS) time_from->tm_mday=1;
		time_from->tm_hour=time_cur->tm_hour;
		time_to->tm_year-=1900;
		time_to->tm_mon-=1;
		*sttime=mktime (time_from);
		*fntime=mktime (time_to);
		if (type==TYPE_HOURS) *sttime-=3600*24*45;
		if (type==TYPE_DAYS) *sttime-=3600*24*30*1;
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
		if (type==TYPE_DAYS) time_from->tm_mday=1;
		time_from->tm_hour=time_cur->tm_hour;
		time_to->tm_year=time_cur->tm_year;
		time_to->tm_mon=time_cur->tm_mon;
		time_to->tm_mday=time_cur->tm_mday;
		time_to->tm_hour=time_cur->tm_hour;
		*sttime=mktime (time_from);
		*fntime=mktime (time_to);
		if (type==TYPE_HOURS) *sttime-=3600*24*45;
		if (type==TYPE_DAYS) *sttime-=3600*1*24*30;
		if (type==TYPE_MONTH) *sttime-=3600*24*300;
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
