#include "eval.h"

#define ARCHIVE_NUM_MAX		1500

#define	VKG_TYPE_NOTDEFINED	0	//	0 - not defined
#define	VKG_TYPE_FLOAT		1	//	1 - float
#define	VKG_TYPE_INT		2	//	2 - int (bcd)
#define	VKG_TYPE_STRING		3	//	3 - string
#define	VKG_TYPE_DATE		4	//	4 - date (+time +1 param)

#define ReadHoldingRegisters	0x3
#define ReadInputRegisters	0x4
#define PresetSingleRegister  	0x10

#define	WriteDate		0xb
#define	WritePass		0x19
#define	WriteParameters		0x1b

#define Pipe			0x1
#define	Errors			0x5
#define	CurrentDate		0xb
#define	Configuration		0xA
#define	SWversion		0xE
#define	GasParameters		0x1B

#define ANALYSE			1
#define NO_ANALYSE		0

#define TYPE_CURRENTS		0
#define TYPE_HOURS		1
#define TYPE_DAYS		2
#define TYPE_MONTH		4
#define TYPE_INCREMENTS		7
#define TYPE_EVENTS		9
#define TYPE_CONST		10
//-----------------------------------------------------------------------------
typedef struct _Archive arch;

struct _Archive {
    uint8_t	id;		// номер порядковый
    uint16_t	no;		// номер параметра 
    uint16_t	adr;		// адрес в пространстве Логики 
    char	name[200];	// текстовое название параметра
    float	knt;		// коэффициент пересчета величины
    uint16_t	type;		// тип элемента (1-часовой, 0-суточный, 2-итоговые, 0-текущие, 2-итоговые)
    char	meas[15];	// текстовое название еденицы измерения
};
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

  char  data [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][30];	// [для всех запросов на чтение] значения параметров
  char  time [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][30];	// [для всех запросов на чтение] метка времени
};
typedef struct _AnsLog AnsLog;
//-----------------------------------------------------------------------------
struct _Archive currentsVKG [] =
    {	{ARCH_VNORM,12,0x7,"Мгновенный объемный расход в М3/час (нормальные)",1,0x1,"М3/ч"},
	{ARCH_VWORK,16,0x8,"Мгновенный рабочий расход в М3/час (рабочие)",1,0x1,"М3/ч"},
	{ARCH_PAVG,18,0x4,"Мгновенное давление газа на входе в КПа",0,0x1,"КПа"},
	{ARCH_DPAVG,19,0x6,"Мгновенное значение разности давления вход выход в КПа",1,0x1,"КПа"},
	{ARCH_TAVG,20,0x3,"Мгновенное значение температуры на входе в Цельсии",1,0x1,"C"},
	{ARCH_DAVG,25,0x9,"Мгновенное значение плотности",1,0x1,""},

	{ARCH_VAGG_NORM,21,0x7,"Накопительный объемный расход в М3/час (нормальные)",0x2,0x1,"М3"},
	{ARCH_VAGG_WORK,23,0x8,"Накопительный рабочий расход в М3/час (рабочие)",0x2,0x1,"М3"},

	{51,105,0x3,"Барометрическое давление подстановочное значение",1,0x1,"КПа"},
	{66,106,0x2,"Константа плотности газа",1,1,"кг/м3"},
	{55,118,0x1,"Содержание Азота",1,0x1,"%"},
	{56,119,0x0,"Содержание двуокиси углерода",1,0x1,"C"}};

struct _Archive archivesVKG [] =
    {	{ARCH_VNORM,12,0x7,"Архив объемный расход в М3/час (нормальные)",1,0x1,"М3/ч"},
	{ARCH_VWORK,16,0x8,"Архив рабочий расход в М3/час (рабочие)",1,0x1,"М3/ч"},
	{ARCH_PAVG,18,0x4,"Архив давление газа на входе в КПа",1,0x1,"КПа"},
	{ARCH_TAVG,20,0x3,"Архив значение температуры на входе в Цельсии",1,0x1,"C"},
	{ARCH_DPAVG,19,0x6,"Архив перепала давления газа на входе в КПа",1,0x1,"кПа"}};
//-----------------------------------------------------------------------------
struct _ERcode {
    uint8_t	id;		// номер порядковый
    char	name[200];	// текстовое название параметра
};

struct _ERcode errorcode [] =
{	{0, "Температура больше максимума"},
	{1, "Температура меньше минимума"},
	{2, "Давление больше максимума"},
	{3, "Давление меньше минимума"},
	{4, "Расход больше максимума"},
	{5, "Расход меньше минимума"},
	{6, "Расход ниже порога отсечки"},
	{7, "Недопустимое физическое состояние газа"},
	{8, "Настройка нуля"},
	{9, "Отсутствие счета"},
	{10, "Отсутствие питания"}};