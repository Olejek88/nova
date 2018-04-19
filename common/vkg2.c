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
#include "vkg2.h"
#include <wchar.h>
#include <locale.h>
#include <math.h>

struct vkg2_ctx {

	char *send_query;
	int  send_query_sz;

	regex_t re;

	char crc_flag;

	uint16_t crc;
};

#define get_spec(dev)	((struct vkg2_ctx *)((dev)->spec))
                                     
/* */
static int  vkg2_init(struct device *);
static void vkg2_free(struct device *);
static int  vkg2_get(struct device *, int, char **);
static int  vkg2_set(struct device *, int, char *, char **);
static int  vkg2_parse_msg(struct device *, char *, int);
static int  vkg2_parse_crc_msg(struct device *, char *, int);
static int  vkg2_check_crc(struct device *, char *, int);
static int  vkg2_send_msg(struct device *);
static int  vkg2_get_time(struct device *dev, struct tm* cur_time);

static int  vkg2_h_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  vkg2_m_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  vkg2_d_archiv(struct device *, int, const char *, const char *,
				struct archive **);
static int  vkg2_events(struct device *, const char *, const char *,
				struct events **);
static int  generate_sequence (struct device *dev, uint8_t type, uint8_t func, uint16_t padr, uint16_t index, uint8_t npipe, int no, struct tm* from, struct tm* to, char* sequence, uint16_t* len);
static int  analyse_sequence (uint16_t, uint8_t, char*, uint16_t, char*, uint8_t, uint8_t, uint16_t);
static int  checkdate (uint8_t	type, int ret_fr, int ret_to, struct tm* time_from, struct tm* time_to, struct tm* time_cur, time_t *tim, time_t *sttime, time_t *fntime);

static uint16_t
calc_bcc (uint8_t *data, int n)
{
    uint16_t crc;
    uint8_t shift_cnt;
    uint8_t *ptr; 
    uint16_t byte_cnt = n;
    ptr=data;

    crc=0xffff;
    for(; byte_cnt>0; byte_cnt--)
	{
	 crc=(uint16_t)((crc/256)*256+((crc%256)^(*ptr++)));
	 for(shift_cnt=0; shift_cnt<8; shift_cnt++)
	    {
	     if((crc&0x1)==1)
	        crc=(uint16_t)((crc>>1)^0xa001);
	     else
		crc>>=1;
	    }
	}
    return crc;
}

static int
set_ctx_regex(struct vkg2_ctx *ctx, const char *restr)
{
	regex_t re;
	int rv;

	rv = regcomp(&re, restr, REG_EXTENDED);
	if (0 == rv) {
		ctx->re = re;
	} else {
		set_regex_error(rv, &re);
		rv = -1;
	}
	return rv;
}

static void
ctx_zero(struct vkg2_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

static int
vkg2_get_ident(struct device *dev)
{
	static char ident_query[100];
	struct 	vkg2_ctx *ctx;
	char 	vkg2_sw[100],answer[100];

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

	if (-1 == generate_sequence (dev, 0, ReadHoldingRegisters, SWversion*256, 1, 0, 0, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto free_regex;
	if (-1 == analyse_sequence (dev->devaddr, 3, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) goto out;
	buf_free(&dev->buf);

	sprintf (vkg2_sw,"%x.%x",(uint8_t)answer[0],(uint8_t)answer[1]);
	devlog(dev, "\tVKG software version: %s\n", vkg2_sw);
	ret = 0;

free_regex:
	regfree(&ctx->re);
out:
	ctx_zero(ctx);
	buf_free(&dev->buf);
	return ret;
}

static int
vkg2_get_time(struct device *dev, struct tm* cur_time)
{
	struct 	vkg2_ctx *ctx;
	int	ret=-1;
	static 	char ident_query[100],	answer[1024];
	uint16_t	len;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));

	ctx = get_spec(dev);
	ctx->crc_flag = 0;

	while (1)	{
		if (-1 == generate_sequence (dev, 0, ReadHoldingRegisters, CurrentDate*256, 5, 0, 0, time_from, time_to, ident_query, &len)) break;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
    		if (-1 == dev_query(dev)) break;
		if (-1 == analyse_sequence (dev->devaddr, ReadHoldingRegisters, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0)) break;
		buf_free(&dev->buf);
		cur_time->tm_year=(answer[0]+1)*256+answer[1];
		cur_time->tm_mon=answer[3];
		cur_time->tm_mday=answer[5];
		cur_time->tm_hour=answer[7];
		cur_time->tm_min=answer[9];
		cur_time->tm_sec=answer[11];
		//printf ("[time] %x %x-%x\n",answer[0],answer[1],answer[2]);
		if (!dev->quiet) fprintf (stderr,"[time] %d-%02d-%02d,%02d:%02d:00\n",cur_time->tm_year,cur_time->tm_mon,cur_time->tm_mday,cur_time->tm_hour,cur_time->tm_min);
		ret=0;
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
vkg2_set_time(struct device *dev, struct tm* cur_time)
{
	struct 	vkg2_ctx *ctx;
	int	ret=-1;
	static 	char ident_query[100];
	uint16_t	len;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));

	ctx = get_spec(dev);
	ctx->crc_flag = 0;

	while (1)	{
		ident_query[0]=cur_time->tm_year/256;
		ident_query[1]=cur_time->tm_year%256;
		ident_query[2]=0;
		ident_query[3]=cur_time->tm_mon;
		ident_query[4]=0;
		ident_query[5]=cur_time->tm_mday;
		ident_query[6]=0;
		ident_query[7]=cur_time->tm_hour;
		ident_query[8]=0;
		ident_query[9]=cur_time->tm_min;
		if (-1 == generate_sequence (dev, 6, PresetSingleRegister, WriteDate*256, 4, 0, 0, time_from, time_to, ident_query, &len)) break;
		ctx->send_query = ident_query;
		ctx->send_query_sz = len;
    		if (-1 == dev_query(dev)) break;
		buf_free(&dev->buf);
		ret=0; break;
	}
	regfree(&ctx->re);
	ctx_zero(ctx);
	buf_free(&dev->buf);
	free (time_from);
	free (time_to);
	return ret;
}

static int  vkg2_date(struct device *dev, const char *ret, char **save)
{
	int	rt=-1;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	char *date;

	if (strlen (ret)==0)
	    {
	     rt = vkg2_get_time(dev, time_from);
	     date=malloc (20);
    	     snprintf (date,20,"%d-%02d-%02d,%02d:%02d:%02d",time_from->tm_year, time_from->tm_mon, time_from->tm_mday, time_from->tm_hour, time_from->tm_min, time_from->tm_sec);
	     *save=date;     
	    }
	else
	    {
		//rt = sscanf(*save,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);
		//rt = vkg2_set_time(dev, time_from);
		rt=-1;
		*save=malloc (20);
    	    	if (rt==0)
	    		snprintf (*save,10,"ok");
	    	else
			snprintf (*save,10,"no date");
		rt=-1;
	    }
	free (time_from);
	return	rt;
}

static int
vkg2_get_string_param(struct device *dev, uint16_t addr, char **save, uint8_t type, uint16_t padr, uint16_t no)
{
	struct 	vkg2_ctx *ctx;
	float	value;
	char	*value_string;
	uint16_t	value_uint=0;
	static char ident_query[100],	answer[1024];
	void 		*fnsave;
	int 		ret;
	uint16_t	len,i;
	struct 	tm *time_from=malloc (sizeof (struct tm));
	struct 	tm *time_to=malloc (sizeof (struct tm));

	ctx = get_spec(dev);
	ret = -1;

	fnsave = dev->opers->parse_msg;
	dev->opers->parse_msg = vkg2_parse_crc_msg;

	if (no<100)
	    {
	     if (-1 == generate_sequence (dev, 0, ReadHoldingRegisters, (Pipe+padr*0x40)*256+9, 18, 0, 0, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) goto out;
	     len = analyse_sequence (dev->devaddr, ReadHoldingRegisters, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr);
	     if (-1 == len) goto out;
	    }
	else
	    {
    	     if (-1 == generate_sequence (dev, 0, ReadHoldingRegisters, (GasParameters)*256, 0, 0, 0, time_from, time_to, ident_query, &len)) goto out;
	     ctx->send_query = ident_query;
	     ctx->send_query_sz = len;
	     if (-1 == dev_query(dev)) goto out;
	     len = analyse_sequence (dev->devaddr, ReadHoldingRegisters, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr);
	    }
	    
	value_string=malloc (100);
	switch	(type)
	    {
		case	VKG_TYPE_NOTDEFINED:	break;
		case	VKG_TYPE_FLOAT:		ident_query[0]=answer[addr*4+3];
						ident_query[1]=answer[addr*4+2];
						ident_query[2]=answer[addr*4+1];
						ident_query[3]=answer[addr*4];
						value =*(float*)(ident_query);
						snprintf (value_string,100,"%f",value);
						*save=value_string;
						if (!dev->quiet) fprintf (stderr,"%s [%x %x]\n",value_string,answer[addr*4],answer[addr*4+1]);
						ret=0;
						break;
		case	VKG_TYPE_INT:		for (i=0; i<4;i++)
	    					    value_uint+=(answer[i+padr*4])*(pow(256,i));
	    					snprintf (value_string,100,"%d",value_uint);
	    					*save=value_string;
	    					if (!dev->quiet) fprintf (stderr,"%s\n",value_string);
	    					ret=0;
						break;
		default:			break;
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
vkg2_set_string_param(struct device *dev, uint16_t addr, char *save, uint8_t type, uint16_t padr)
{
	struct vkg2_ctx *ctx;
	static char 	ident_query[100], answer[1024];
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
	dev->opers->parse_msg = vkg2_parse_crc_msg;
	sprintf (ident_query,"000000000000");
	len=12;
	if (-1 == generate_sequence (dev, 0, PresetSingleRegister, WritePass*256, 6, 0, 0, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->devaddr, ReadHoldingRegisters, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
	buf_free(&dev->buf);

	if (-1 == generate_sequence (dev, 0, ReadHoldingRegisters, (GasParameters)*256, 0, 0, 0, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	len = analyse_sequence (dev->devaddr, ReadHoldingRegisters, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr);
	buf_free(&dev->buf);

	switch	(type)
	    {
		case	VKG_TYPE_FLOAT:		if (!dev->quiet)
						    fprintf (stderr,"[%d] (%x %x %x %x) (%x %x %x %x) (%x %x %x %x) (%x %x %x %x)\n",addr,answer[0],answer[1],answer[2],answer[3],answer[4],answer[5],answer[6],answer[7],answer[8],answer[9],answer[10],answer[11],answer[12],answer[13],answer[14],answer[15]);
						sscanf (save,"%f",&value);
						memcpy	(ident_query,&value,4);
						if (addr>3) addr=3;
						for (i=0;i<=3;i++)
						    answer[addr*4+(3-i)]=ident_query[i];
						memcpy (ident_query,answer,16);
						if (!dev->quiet)
						    fprintf (stderr,"[%d] (%x %x %x %x) (%x %x %x %x) (%x %x %x %x) (%x %x %x %x)\n",addr,answer[0],answer[1],answer[2],answer[3],answer[4],answer[5],answer[6],answer[7],answer[8],answer[9],answer[10],answer[11],answer[12],answer[13],answer[14],answer[15]);
						//fprintf (stderr,"%f (%x %x %x %x)\n",value,ident_query[0],ident_query[1],ident_query[2],ident_query[3]);
						len=16;
						break;
		case	VKG_TYPE_INT:		for (i=0; i<strlen(save); i++)
						    ident_query[i]=atoi(&save[i]);
						len=i;
						break;
		case	VKG_TYPE_NOTDEFINED:
		default:			goto out;
	    }
	if (-1 == generate_sequence (dev, 0, PresetSingleRegister, WriteParameters*256, 8, 0, 0, time_from, time_to, ident_query, &len)) goto out;
	ctx->send_query = ident_query;
	ctx->send_query_sz = len;
	if (-1 == dev_query(dev)) goto out;
	if (-1 == analyse_sequence (dev->devaddr, ReadHoldingRegisters, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, padr)) goto out;
	//printf ("%x %x %x",dev->buf.p[0],dev->buf.p[1],dev->buf.p[2]);
	if ((uint8_t)dev->buf.p[1]==0x10) ret=0;
	if (!dev->quiet)
	    if ((uint8_t)dev->buf.p[1]==0x90) fprintf (stderr,"[0x90] доступ к записи настроек закрыт");
	buf_free(&dev->buf);
out:
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
static struct vk_operations vkg2_opers = {
	.init	= vkg2_init,
	.free	= vkg2_free,
	.get	= vkg2_get,
	.set	= vkg2_set,
	.date	= vkg2_date,

	.send_msg  = vkg2_send_msg,
	.parse_msg = vkg2_parse_msg,
	.check_crc = vkg2_check_crc,

	.h_archiv = vkg2_h_archiv,
	.m_archiv = vkg2_m_archiv,
	.d_archiv = vkg2_d_archiv,

	.events = vkg2_events
};

void
vkg2_get_operations(struct vk_operations **p_opers)
{
	*p_opers = &vkg2_opers;
}

static int
vkg2_init(struct device *dev)
{
	struct vkg2_ctx *spec;
	int ret=-1;

	spec = calloc(1, sizeof(*spec));

	if (!spec) {
		set_system_error("calloc");
		ret = -1;
	} else {
		dev->spec = spec;

		ret =   (-1 == vkg2_get_ident(dev));
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
vkg2_free(struct device *dev)
{
	struct vkg2_ctx *spec = get_spec(dev);

	free(spec);
	dev->spec = NULL;
}

static int
vkg2_get_ns (int no, int nom, char* code)
{
    int i;
    printf ("ns %d %d\n",no,nom);
    for (i=0; i<ARRAY_SIZE(errorcode); i++)
	if (no == errorcode[i].id)
	    {
	    snprintf (code,75,"%s",errorcode[i].name);
	    return 1;
	    }
    return -1;
}

static int
vkg2_get(struct device *dev, int param, char **ret)
{
	int 		rv=-1;
	uint16_t	i=0;

	//printf ("d=%d\n",param);
	for (i=0; i<ARRAY_SIZE(currentsVKG); i++)
	if (param == currentsVKG[i].no) {
	    rv = vkg2_get_string_param(dev, currentsVKG[i].adr, ret, currentsVKG[i].type, currentsVKG[i].knt, currentsVKG[i].no);
    	    return 0;
	}
	//printf ("%d\n",sizeof(ret));
    	//strcpy (ret,mas);
	return rv;
}

static int
vkg2_set(struct device *dev, int param, char *ret, char **save)
{
	int 		rv=0,i=0;
	for (i=0; i<ARRAY_SIZE(currentsVKG); i++)
		if (currentsVKG[i].no==param) {
		    rv = vkg2_set_string_param(dev, currentsVKG[i].adr, ret, currentsVKG[i].type, param);
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
	int nrecs,i,ret,np=0;
	struct 	tm *tim=malloc (sizeof (struct tm));

	ret = -1;
	nrecs = alog->quant_param;
	if (nrecs <= 0) return -1;
	if (!dev->quiet)
		fprintf(stderr, "RECS: %d\n", nrecs);

	for (i = 0; i < nrecs; ++i) 
		{
		 if ((ptr = alloc_archive()) != NULL) 
			{
			 ptr->num = i;

			 for (np=0; np<ARRAY_SIZE(archivesVKG); np++)
				{
    				 snprintf(ptr->datetime, sizeof(ptr->datetime),"%s",alog->time[archivesVKG[np].id][i]);
    				 ptr->params[archivesVKG[np].id] = atof (alog->data[archivesVKG[np].id][i]);
    				 //if (!dev->quiet) fprintf (stderr,"[%s] %f\n",ptr->datetime,ptr->params[archivesVKG[np].id]);
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
vkg2_h_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct 		vkg2_ctx *ctx;
	int 		ret,ret_fr,ret_to;
	uint16_t 	len,i=0,ind=0;
	static char 	ident_query[100],answer[1024];
	AnsLog		alog;
	float		value;
	time_t 		tim,sttime,fntime,crtime;

	ctx = get_spec(dev);
	ctx->crc_flag = 0;
	alog.quant_param=0;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);

	struct 	tm *time_to=malloc (sizeof (struct tm));
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	tim=time(&tim);
	localtime_r(&tim,time_cur);
	crtime=mktime (time_cur);

	ret=checkdate (TYPE_HOURS,ret_fr,ret_to,time_from,time_to,time_cur,&crtime,&sttime,&fntime);

	if (sttime && fntime && ret==0)
	while (sttime<=fntime)	{
		//alog.quant_param=0;
		//ret = vkg2_get_time(dev, time_cur);
		//printf ("%ld - %ld",sttime,fntime);
		localtime_r(&sttime,time_cur);
		ident_query[0]=(time_cur->tm_year+1900)/256; ident_query[1]=(time_cur->tm_year+1900)%256;
		ident_query[2]=0; ident_query[3]=time_cur->tm_mon+1;
		ident_query[4]=0; ident_query[5]=time_cur->tm_mday;
		ident_query[6]=0; ident_query[7]=time_cur->tm_hour;
		len=8;
		
		//time_cur->tm_year-=1900;
		//time_cur->tm_mon-=1;
		//crtime=mktime (time_cur);

		 if (-1 == generate_sequence (dev, 0, PresetSingleRegister, WriteDate*256, 4, 0, 0, time_from, time_to, ident_query, &len)) continue;
		 ctx->send_query = ident_query;
		 ctx->send_query_sz = len;
		 if (-1 == dev_query(dev) || (uint8_t)dev->buf.p[1]==0x90) {  
			 buf_free(&dev->buf);
			 sttime+=3600;
			 continue;
			} 
	         buf_free(&dev->buf);
			
		 if (-1 == generate_sequence (dev, 0, ReadInputRegisters, (Pipe+0x40)*256+9*1, 18, 0, 0, time_from, time_to, ident_query, &len)) continue;
		 ctx->send_query = ident_query;
		 ctx->send_query_sz = len;
		 if (dev_query(dev)>=0)
		 if (analyse_sequence (dev->devaddr, ReadInputRegisters, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0) >= 0)	
			{
			 for (i=0; i<ARRAY_SIZE(archivesVKG); i++)
				{
				 sprintf (alog.time[archivesVKG[i].id][ind],"%d-%02d-%02d,%02d:00:00",time_cur->tm_year+1900,time_cur->tm_mon+1,time_cur->tm_mday,time_cur->tm_hour);
				 ident_query[0]=answer[archivesVKG[i].adr*4+3];
				 ident_query[1]=answer[archivesVKG[i].adr*4+2];
				 ident_query[2]=answer[archivesVKG[i].adr*4+1];
				 ident_query[3]=answer[archivesVKG[i].adr*4];
				 value =*(float*)(ident_query);
				 sprintf (alog.data[archivesVKG[i].id][ind],"%f",value);
			         if (!dev->quiet) fprintf (stderr,"[%d/%d] [%s] [%s]\n",ind,i,alog.time[archivesVKG[i].id][ind],alog.data[archivesVKG[i].id][ind]);
			        }
			}
		 //crtime=mktime (time_cur);
		 sttime+=3600;
		 //localtime_r(&crtime,time_cur);
		 alog.quant_param++;
	         buf_free(&dev->buf);
	         if (ind>ARCHIVE_NUM_MAX) break;
	         ind++;
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
vkg2_d_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	struct 		vkg2_ctx *ctx;
	uint16_t 	len,i=0,ind=0,tri=0;
	static char 	ident_query[100],	answer[1024];
	AnsLog		alog;
	float		value;
	int 		ret,ret_fr,ret_to;
	time_t 		tim,sttime,fntime;

	struct 	tm *time_from=malloc (sizeof (struct tm));
	ret_fr = sscanf(from,"%d-%d-%d,%d:%d:%d",&time_from->tm_year, &time_from->tm_mon, &time_from->tm_mday, &time_from->tm_hour, &time_from->tm_min, &time_from->tm_sec);

	struct 	tm *time_to=malloc (sizeof (struct tm));
	ret_to = sscanf(to,"%d-%d-%d,%d:%d:%d",&time_to->tm_year, &time_to->tm_mon, &time_to->tm_mday, &time_to->tm_hour, &time_to->tm_min, &time_to->tm_sec);

	struct 	tm *time_cur=malloc (sizeof (struct tm));
	tim=time(&tim);
	localtime_r(&tim,time_cur);
//	crtime=mktime (time_cur);

	ctx = get_spec(dev);
	ctx->crc_flag = 0;
	alog.quant_param=0;

	ret=checkdate (TYPE_DAYS,ret_fr,ret_to,time_from,time_to,time_cur,&tim,&sttime,&fntime);

	if (sttime && fntime && ret==0)
	while (sttime<=fntime)	{
		//alog.quant_param=0;
		localtime_r(&sttime,time_cur);

		ident_query[0]=(time_cur->tm_year+1900)/256; ident_query[1]=(time_cur->tm_year+1900)%256;
		ident_query[2]=0; ident_query[3]=time_cur->tm_mon+1;
		ident_query[4]=0; ident_query[5]=time_cur->tm_mday;
		ident_query[6]=0; ident_query[7]=0;
		ident_query[8]=0; ident_query[9]=0;
		len=8;
		//time_cur->tm_year-=1900;
		//time_cur->tm_mon-=1;
		//crtime=mktime (time_cur);

		 if (-1 == generate_sequence (dev, 0, PresetSingleRegister, WriteDate*256, 4, 0, 0, time_from, time_to, ident_query, &len)) continue;
		 ctx->send_query = ident_query;
		 ctx->send_query_sz = len;
		 if (-1 == dev_query(dev) || (uint8_t)dev->buf.p[1]==0x90) {  
			 buf_free(&dev->buf);
			 sttime+=3600*24;
			 continue;
			}
	         buf_free(&dev->buf);
			
		 if (-1 == generate_sequence (dev, 0, ReadInputRegisters, (Pipe+0x40)*256+9*1, 18, 0, 0, time_from, time_to, ident_query, &len)) continue;
		 ctx->send_query = ident_query;
		 ctx->send_query_sz = len;
		 if (dev_query(dev)>=0) 
		 if (analyse_sequence (dev->devaddr, ReadInputRegisters, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0) >= 0)	
			{
			 for (i=0; i<ARRAY_SIZE(archivesVKG); i++)
				{
				 sprintf (alog.time[archivesVKG[i].id][ind],"%d-%02d-%02d,00:00:00",time_cur->tm_year+1900,time_cur->tm_mon+1,time_cur->tm_mday);
				 ident_query[0]=answer[archivesVKG[i].adr*4+3];
				 ident_query[1]=answer[archivesVKG[i].adr*4+2];
				 ident_query[2]=answer[archivesVKG[i].adr*4+1];
				 ident_query[3]=answer[archivesVKG[i].adr*4];
				 value =*(float*)(ident_query);
				 sprintf (alog.data[archivesVKG[i].id][ind],"%f",value);
				 //if (!dev->quiet) printf ("[%s] [%s]\n",alog.time[archivesVKG[i].id][ind],alog.data[archivesVKG[i].id][ind]);
			        }
			}
		 sttime+=3600*24;
		 alog.quant_param++;
	         buf_free(&dev->buf);
	         if (ind>ARCHIVE_NUM_MAX) break;
	         ind++;
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
vkg2_m_archiv(struct device *dev, int no, const char *from, const char *to, struct archive **save)
{
	int 		ret;
	AnsLog		alog;
	alog.quant_param=0;
	ret = make_archive(dev, no, &alog, TYPE_MONTH, save);
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
vkg2_events(struct device *dev, const char *from, const char *to, struct events **save)
{
	struct vkg2_ctx *ctx;
	AnsLog alog;
	struct 	events *ev, *pev=NULL, *top=NULL;
	int ret=1,i,leng=0,ind=0;
	uint16_t len;
	static char ident_query[100], answer[1024];
	time_t 	tim,sttime,fntime;
	int 	ret_fr,ret_to;

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

	if (sttime && fntime && ret==0)
	while (sttime<=fntime)	{
		//alog.quant_param=0;
		localtime_r(&sttime,time_cur);

		ident_query[0]=(time_cur->tm_year+1900)/256; ident_query[1]=(time_cur->tm_year+1900)%256;
		ident_query[2]=0; ident_query[3]=time_cur->tm_mon+1;
		ident_query[4]=0; ident_query[5]=time_cur->tm_mday;
		ident_query[6]=0; ident_query[7]=0;
		len=8;
		//time_cur->tm_year-=1900;
		//time_cur->tm_mon-=1;
		//crtime=mktime (time_cur);

		 if (-1 == generate_sequence (dev, 0, PresetSingleRegister, WriteDate*256, 4, 0, 0, time_from, time_to, ident_query, &len)) continue;
		 ctx->send_query = ident_query;
		 ctx->send_query_sz = len;
		 if (-1 == dev_query(dev) || (uint8_t)dev->buf.p[1]==0x90) {
		     sttime+=3600*24;
		     buf_free(&dev->buf);
		     continue; 
		    }
	         buf_free(&dev->buf);

		 if (-1 == generate_sequence (dev, 0, ReadInputRegisters, (0x5)*256+11*1, 11, 0, 0, time_from, time_to, ident_query, &len)) continue;
		 ctx->send_query = ident_query;
		 ctx->send_query_sz = len;
		 if (dev_query(dev)>=0) 
		 if (analyse_sequence (dev->devaddr, ReadInputRegisters, dev->buf.p, dev->buf.len, answer, ANALYSE, 0, 0) > 0)	
			{
			 for (i=0; i<11; i++)
				{
				 leng=(uint8_t)answer[i*2]*256+(uint8_t)answer[i*2+1];
				 if (leng>0)
				    {
				     sprintf (alog.time[0][ind],"%04d-%02d-%02d,00:00:00",time_cur->tm_year+1900,time_cur->tm_mon+1,time_cur->tm_mday);
				     sprintf (alog.data[0][ind],"%d",i);
				     if (!dev->quiet)
			                fprintf (stderr,"[%s] %s [%d] \n",alog.time[0][ind],errorcode[i].name,leng);
			             //alog.quant_param++;
			             ind++;
			            }
			        }
			}
		 sttime+=3600*24;
	         buf_free(&dev->buf);
	         //ind++;
	    }
	alog.quant_param=ind;
	
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
vkg2_send_msg(struct device *dev)
{
	struct vkg2_ctx *ctx = get_spec(dev);
	return dev_write(dev, ctx->send_query, ctx->send_query_sz);
}

static int
vkg2_parse_msg(struct device *dev, char *p, int psz)
{
    //struct vkg2_ctx *ctx;
    int ret=-1;
    char answer[1024];

    //ctx = get_spec(dev);
    if (!dev->quiet)
	fprintf(stderr, "\rParsing: %10d bytes", psz);
    ret=analyse_sequence (dev->devaddr, 0 ,p, psz, answer, ANALYSE, 0, 0);
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
vkg2_parse_crc_msg(struct device *dev, char *p, int psz)
{
    //struct vkg2_ctx *ctx;
    int ret;
    char answer[1024];
    //ctx = get_spec(dev);
    if (!dev->quiet)
	fprintf(stderr, "\rParsing: %10d bytes", psz);

    ret=analyse_sequence (dev->devaddr, 0 ,p, psz, answer, ANALYSE, 0, 0);
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
vkg2_check_crc(struct device *dev, char *p, int psz)
{
    struct vkg2_ctx *ctx;
    int ret;
    char answer[1024];

    ctx = get_spec(dev);
    if (ctx->crc_flag) {
		ret=analyse_sequence (dev->devaddr, 0, p, psz, answer, ANALYSE, 0, 0);
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
    uint16_t 	ln=0,crc;

    switch (func)
	{
        case 0x3:	sprintf (buffer,"%c%c%c%c%c%c",dev->devaddr&0xFF,(uint8_t)func,(padr&0xff00)>>8,padr&0xff,(index&0xff00)>>8,index&0xff);
    			crc=calc_bcc ((uint8_t *)buffer, 6);
            		buffer[6]=crc%256;
            		buffer[7]=crc/256;
            		ln=8;
	    		if (!dev->quiet)
	    		    fprintf (stderr,"[vkg2] wr[%d] [0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7]);
	    		break;
        case 0x4:	sprintf (buffer,"%c%c%c%c%c%c",dev->devaddr&0xFF,(uint8_t)func,(padr&0xff00)>>8,padr&0xff,(index&0xff00)>>8,index&0xff);
    			crc=calc_bcc ((uint8_t *)buffer, 6);
            		buffer[6]=crc%256;
            		buffer[7]=crc/256;
            		ln=8;
	    		if (!dev->quiet)
	    		    fprintf (stderr,"[vkg2] wr[%d] [0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7]);
	    		break;
	case 0x10: 	sprintf (buffer,"%c%c%c%c%c%c%c",dev->devaddr&0xFF,(uint8_t)func,(padr&0xff00)>>8,padr&0xff,(index&0xff00)>>8,index&0xff,*len);
			memcpy (buffer+7,sequence,*len);
			ln=*len+8;
			crc=calc_bcc ((uint8_t *)buffer, *len+7);
    			buffer[*len+7]=crc%256;
    			buffer[*len+8]=crc/256;
    			ln=*len+9;
        		if (!dev->quiet)
        		    fprintf (stderr,"[vkg2] wr[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",ln,buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],buffer[8],buffer[9],buffer[10],buffer[11]);
			break;
	default:	if (!dev->quiet)
			    fprintf (stderr,"unknown func request %d\n",type);
			return	-1;
        }
     memcpy (sequence,buffer,ln);
     *len=ln;
     return 0;
}

//----------------------------------------------------------------------------------------
static int
analyse_sequence (uint16_t adr, uint8_t funct, char* data, uint16_t len, char* dat, uint8_t analyse, uint8_t id, uint16_t padr)
{
    uint16_t	crc=0;
//    uint8_t	i=0;
    //for (i=0; i<len; i++)   printf ("in[%d]:0x%x (%c)\n",i,(uint8_t)data[i],(uint8_t)data[i]);

    if (analyse==NO_ANALYSE)
	if ((data[0]==adr && data[1]==funct) || funct==0)
	    return 0;
    
    if (len>5)
        {
    	    if ((data[0]==adr) || funct==0)
	        {
    	         crc=calc_bcc ((uint8_t*)data, len-2);
	         //printf ("[%d] %x (%x)\n",len,crc,(uint8_t)data[len-1]*256+(uint8_t)data[len-2]);
        	 if (crc==(uint16_t)((uint8_t)data[len-1]*256+(uint8_t)data[len-2]))
            	        {
            	         //fprintf (stderr,"[vkg2] rd[%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]\n",len,(uint8_t)data[0],(uint8_t)data[1],(uint8_t)data[2],(uint8_t)data[3],(uint8_t)data[4],(uint8_t)data[5],(uint8_t)data[6],(uint8_t)data[7]);
            	         memcpy (dat,data+3,data[2]);
	                 return data[2];
	                }
	         if (len>80) return 1;
	         return -1;
    		}
        }
    return	-1;
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
		if (type==TYPE_DAYS) *sttime-=3600*24*65;
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
		if (type==TYPE_DAYS) *sttime-=3600*24*65;
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
