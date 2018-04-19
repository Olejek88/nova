#include "eval.h"
//#define	DEVICE_CFG	"tekon19.cfg"
//#define	PASSWORD	2305
#define	PASSWORD	0
#define	PROTOCOL	2
#define	ARCHIVE_DEEP	64

#define	T17_TYPE_NOTDEFINED	0	//	0 - not defined
#define	T17_TYPE_FLOAT		1	//	1 - float
#define	T17_TYPE_INT		2	//	2 - int (bcd)
#define	T17_TYPE_STRING		3	//	3 - string
#define	T17_TYPE_DATE		4	//	4 - date (+time +1 param)

// Specification T.10.06.59RD //In russian to english transcription
#define CMD_CNTRL_FAILURE		0x00 //Control of failures
#define CMD_READ_PARAM			0x01 //Read parameter
#define CMD_READ_EXT_MEM		0x02 //Read external memory
#define CMD_READ_INT_MEM		0x03 //Read internal memory
#define CMD_READ_PRG_MEM		0x04 //Read program memory
#define CMD_WRITE_PARAM			0x05 //Write param
#define CMD_WRITE_EXT_MEM		0x06 //Write to external memeory
#define CMD_WRITW_INT_MEM		0x07 //Write to internal memory
#define CMD_START			0x08 //Start
#define CMD_STOP			0x09 //Stop
#define CMD_END_FULL_ACCESS		0x0b //End full access
#define CMD_WORK_BIT_PARAM		0x0c //Work with bit paramaeter
#define CMD_REPRG_DATA			0x0d //Reprogramming data
#define CMD_CLEAR_MEM			0x0e //Clear memory
#define CMD_CHANGE_PRG			0x0f //Change programm
#define CMD_EXCHANGE_SUPERFLO		0x10 //Make exchange with Superflo
#define CMD_READ_PARAM_SLAVE		0x11 //Redad parameter from slave device
#define CMD_READ_ARC_EVENTS		0x12 //Read archive of events
#define CMD_READ_PARAM_ARR		0x13 //Read package of parameters
#define CMD_WRITE_PARAM_SLAVE		0x14 //Write parameter to slave device

// Extended specification T.10.06.59RD-D1 //In russian to english transcription
#define CMD_READ_INDEX_PARAM		0x15 // Read index parameter
#define CMD_WRITE_INDEX_PARAM		0x16 // Write index parameter
#define CMD_SET_ACCESS_LEVEL		0x17 // Set access level
#define CMD_CLEAR_INDEX_PARAM		0x18 // Clear index parameter

#define CMD_ACCESS_LEVEL_USER		0x01
#define CMD_ACCESS_LEVEL_ADJUSTER	0x02
#define CMD_ACCESS_LEVEL_VENDOR		0x03

#define T_UNDEFINED			0x00

#define T_BYTE				0x01 << 24
#define T_WORD				0x02 << 24
#define T_FLOAT				0x03 << 24
#define T_TMASK				0x0f << 24

#define T_COMMON			0x10 << 24
#define T_SENS				0x20 << 24
#define T_TUBE				0x30 << 24
#define T_ARC				0x40 << 24
#define T_STMASK			0x70 << 24

#define ARCHIVE_NUM_MAX		500

#define ANALYSE			1
#define NO_ANALYSE		0

#define TYPE_CURRENTS		0
#define TYPE_HOURS		1
#define TYPE_DAYS		2
#define TYPE_MONTH		4
#define TYPE_INCREMENTS		7
#define TYPE_EVENTS		9
#define TYPE_CONST		10

#define	T17_SERIAL		0x403c
#define WRITE_PASS		0x4007
#define DEVICE_PASS		0x4008
//-----------------------------------------------------------------------------
struct _AnsLog {
  uint8_t  checksym;	// checksum status (true - ok, false - bad)
  uint8_t  from;	// source address (SPG)
  uint8_t  to;		// reciever address (controller)
  uint8_t  func;	// answer function
  char 	head[100];	// answer header
  uint8_t  pipe;	// channels or pipe number
  uint8_t  nadr;	// parametr or array number
  uint8_t  crc;		// checksum

  uint8_t  from_param;	// [index] from which parametr read
  uint8_t  quant_param;	// [index] parametrs quant
  uint16_t	ind[ARCH_MAX_NPARAM];
  char  data [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][150];	// [для всех запросов на чтение] значения параметров
  char  type [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][30];	// [для всех запросов на чтение] еденицы измерения
  char  time [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][30];	// [для всех запросов на чтение] метка времени
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
struct _Archive currents17 [] =
    {	{ARCH_VNORM,12,0x8014,"Мгновенный объемный расход в М3/час (нормальные)",1,0x1,"М3/ч"},
	{ARCH_VWORK,16,0x8020,"Мгновенный рабочий расход в М3/час (рабочие)",1,0x1,"М3/ч"},
	{ARCH_PAVG,18,0x8024,"Мгновенное давление газа на входе в КПа",1,0x1,"КПа"},
	{ARCH_DPAVG,19,0x801f,"Мгновенное значение разности давления вход выход в КПа",1,0x1,"КПа"},
	{ARCH_TAVG,20,0x8021,"Мгновенное значение температуры на входе в Цельсии",1,0x1,"C"},
	{ARCH_VAGG_NORM,21,0x801e,"Мгновенное нарастающее значение объема (нормальные)",1,0x1,"М3"},
	{ARCH_DAVG,25,0x8025,"Мгновенное значение плотности",1,0x1,""},
	{ARCH_KAVG,26,0x8027,"Мгновенное значение коэффициента сжимаемости",1,0x1,""},
	{ARCH_HAVG,27,0x8028,"Мгновенное значение теплота сгорания",1,0x1,"Ккал/М3"},

	{51,105,0x4310,"Барометрическое давление подстановочное значение",1,0x1,"КПа"},
	{52,106,0x4210,"Давление газа на входе подстановочное значение",1,0x1,"КПа"},
	{53,108,0x4010,"Температура окружающей среды подстановочное значение",1,0x1,"C"},
	{54,109,0x4110,"Температура газа на входе подстановочное значение",1,0x1,"C"},
	{55,118,0x8010,"Содержание Азота",1,0x1,"C"},
	{56,119,0x800f,"Содержание двуокиси углерода",1,0x1,"C"},
	{57,500,0x4015,"Локальное время  корректора (от начала эпохи)",1,0x4,""},
	{60,504,0x8034,"Нарастающий счетчик времени учета ресурсов на момент времени",1,0x1,""},
	{61,506,0x4002,"Расчетный часа (смешение в секундах от начала суток)",1,0x2,"ч"},
	{62,507,0x4002,"Расчетные сутки (смещение в секундах от начала месяца",1,0x2,"с"},
	{63,509,0x4102,"Коррекция времени вычислителя корректора в сутки",1,0x2,"с"}};

struct _Archive hours17 [] =
    {	{ARCH_VNORM,12,0x0,"Часовой архив объемный расход в М3/час (нормальные)",1,0x1,"М3/ч"},
	{ARCH_VWORK,16,0x1,"Часовой архив рабочий расход в М3/час (рабочие)",1,0x1,"М3/ч"},
	{ARCH_PAVG,18,0x2,"Часовой архив давление газа на входе в КПа",1,0x1,"КПа"},
	{ARCH_TAVG,20,0x3,"Часовой архив значение температуры на входе в Цельсии",1,0x1,"C"},
	{ARCH_VAGG_NORM,21,0x4,"Часовой архив нарастающее значение объема (нормальные)",1,0x1,"М3"}};

struct _Archive days17 [] =
    {	{ARCH_VNORM,12,0x0,"Дневной архив объемный расход в М3/час (нормальные)",1,0x1,"М3/ч"},
	{ARCH_VWORK,16,0x1,"Дневной архив рабочий расход в М3/час (рабочие)",1,0x1,"М3/ч"},
	{ARCH_PAVG,18,0x2,"Дневной архив давление газа на входе в КПа",1,0x1,"КПа"},
	{ARCH_TAVG,20,0x3,"Дневной архив значение температуры на входе в Цельсии",1,0x1,"C"},
	{ARCH_VAGG_NORM,21,0x4,"Дневной архив нарастающее значение объема (нормальные)",1,0x1,"М3"}};

struct _Archive month17 [] =
    {	{ARCH_VNORM,12,0x0,"Архив по месяцам объемный расход в М3/час (нормальные)",1,0x1,"М3/ч"},
	{ARCH_VWORK,16,0x1,"Архив по месяцам рабочий расход в М3/час (рабочие)",1,0x1,"М3/ч"},
	{ARCH_PAVG,18,0x2,"Архив по месяцам давление газа на входе в КПа",1,0x1,"КПа"},
	{ARCH_TAVG,20,0x3,"Архив по месяцам значение температуры на входе в Цельсии",1,0x1,"C"},
	{ARCH_VAGG_NORM,21,0x4,"Архив по месяцам нарастающее значение объема (нормальные)",1,0x1,"М3"}};
//-----------------------------------------------------------------------------
struct _NScode {
    uint8_t	id;		// номер порядковый
    uint8_t	nid;		// номер порядковый
    char	name[200];	// текстовое название параметра
};

struct _NScode nscodeT17 [] =
{	{1, 0, "Отключение питания"},
	{1, 1, "Включение питания"},
	{1, 2, "Перешивка программы"},
	{2, 255, "Изменение отказов (по параметру 0500)"},
	{4, 255, "Запись простого параметра через канал"},
	{64,255, "Коррекция простого параметра через меню"},
	{8, 255, "Очистка параметра"},
	{10,255, "Фиксация индексного параметра CAN BUS"},
	{14,255, "Запись индексного параметра"},
	{21,255, "Обращение задачи к неизвестному параметру"},
	{20,255, "Попытка записи фоновой задачи в РПЗУ"},
	{30,255, "Работа / останов программы"},
	{44,255, "Изменение пароля с дисплея"},
	{54,255, "Синхронизация времени"},
	{80, 0, "Начальный запуск"},
	{80, 1, "Очистка памяти"},
	{80, 2, "Тест внешнего ОЗУ"}};