#include "eval.h"

#define	PIPE			1
#define	IM2300_TYPE_NOTDEFINED	0	//	0 - not defined
#define	IM2300_TYPE_FLOAT	1	//	1 - float
#define	IM2300_TYPE_INT		2	//	2 - int (bcd)
#define	IM2300_TYPE_STRING	3	//	3 - string
#define	IM2300_TYPE_DATE	4	//	4 - date (+time +1 param)

#define	IM2300_MAX_DATA		24
#define	IM2300_MAX_PARAMS	64

#define	IM2300_MAX_BLOCK	0x26

#define ARCHIVE_NUM_MAX		500
#define MAX_EVENTS		300

#define ANALYSE			1
#define NO_ANALYSE		0

#define TYPE_CURRENTS		0
#define TYPE_HOURS		1
#define TYPE_DAYS		2
#define TYPE_MONTH		4
#define TYPE_INCREMENTS		7
#define TYPE_EVENTS		9
#define TYPE_CONST		10

#define	READ_CURRENTS		0x91
#define READ_PASS		0x98
#define READ_ARCHIVES		0x92
#define	READ_CONSTANT		0x94
#define	READ_DATETIME		0x95
#define	READ_EVENTS		0x9F
#define	READ_CONFIGURATION	0x9C

#define	WRITE_PARAMS		0x12
#define WRITE_CONSTS		0x13
#define WRITE_DATE		0x14

#define	READ_DAYS		0xA4
#define	READ_MONTH		0xA5

//-----------------------------------------------------------------------------
static uint8_t	REGPARAMS=0;
static time_t 	tim,sttime,fntime;
static char	im2300_name[10];
static uint8_t	ARCHIVES_DM=0;
static uint16_t wait_len=0;
//-----------------------------------------------------------------------------
struct _AnsLog {
  uint8_t checksym;	// checksum status (true - ok, false - bad)
  uint8_t  from;	// source address (SPG)
  uint8_t  to;		// reciever address (controller)
  uint8_t  func;	// answer function
  char 	head[100];	// answer header
  uint8_t  pipe;	// channels or pipe number
  uint8_t  nadr;	// parametr or array number
  uint8_t crc;		// checksum

  uint8_t  from_param;	// [index] from which parametr read
  uint8_t  quant_param;	// [index] parametrs quant

  char  data [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][80];	// [для всех запросов на чтение] значения параметров
  char  time [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][30];	// [для всех запросов на чтение] метка времени
  char  flag [ARCHIVE_NUM_MAX];				// [для всех запросов на чтение] флаг
};
typedef struct _AnsLog AnsLog;
//-----------------------------------------------------------------------------
typedef struct _Archive arch;

struct _Archive {
    uint8_t	id;		// номер порядковый
    uint16_t	no;		// номер параметра 
    uint16_t	adr;		// адрес в пространстве Логики 
    char	name[200];	// текстовое название параметра
    float	knt;		// коэффициент пересчета величины
    uint16_t	type;		// тип элемента (0-часовой, 1-суточный, 2-декадный, 3-по месяцам, 4-сменный)
    char	meas[15];	// текстовое название еденицы измерения
};
//-----------------------------------------------------------------------------
struct _Archive currentsIM [ARCH_MAX_NPARAM];
struct _Archive archivesIM [ARCH_MAX_NPARAM];

struct _Archive constIM [] =
{ 	{1,101,17,"Константное значение атмосферного давления",1,0,"мм.рт.ст."},
	{2,102,20,"Константное значение абсолютного давления",1,0,"МПа"},
	{5,105,21,"Константное значение температуры",1,0,"C"},
	{6,106,3,"Константа плотности газа",1,0,"кг/м3"},
	{8,108,2,"Константа влажности газа",1,0,"%"},
	{14,114,5,"Доля азота",1,5,"%"},
	{15,115,4,"Доля диоксида углерода",1,6,"%"},
	{22,506,22,"Расчетный час ( смешение в секундах от начала суток)",1,0,"с"}};

//-----------------------------------------------------------------------------
struct _NScode {
    uint8_t	id;		// номер порядковый
    uint16_t	kod;		// код параметра
    char	name[200];	// текстовое название параметра
};
struct _NScode nscodeIM [] =
{	{0, 0x1, "Запрос записи врмени"},
	{1, 0x2, "Запись времени"},
	{2, 0x4, "Запись паспорта"},
	{3, 0x8, "Запись установок пользователя"},
	{4, 0x10, "Запись констант"},
	{5, 0x20, "Запись констант без смены КЗ"},
	{6, 0x40, "Сброс"},
	{7, 0x80, "Вкл. режима повышенной точности"},
	{8, 0x100, "Выкл. режима повышенной точности"},
	{9, 0x200, "Запись аппаратной конфигурации"},
	{10, 0x400, "Запись флага оплаты"}};