#include "eval.h"

#define ARCHIVE_NUM_MAX		100
#define MAX_EVENTS		100

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

  char  data [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][120];	// [для всех запросов на чтение] значения параметров
  char  time [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][30];	// [для всех запросов на чтение] метка времени
};
typedef struct _AnsLog AnsLog;
//-----------------------------------------------------------------------------
#define	GET742_HOURS	0x48
#define	GET742_DAYS	0x59
#define	GET742_MONTHS	0x4D
#define	GET742_RAM	0x52
#define	GET742_FLASH	0x45
#define	GET742_PARAM	0x72
#define	SET742_PARAM	0x44
#define	START_EXCHANGE	0x3F

#define	M4_GET742_ARCHIVE	0x61
#define	M4_SET742_PARAM		0x77
#define	M4_GET742_PARAM		0x72

#define DLE 	0x10  	// символ-префикс.
#define HT  	0x09  	// код горизонтальной табуляции
#define ZERO  	0x0 	// просто ноль
#define UK  	0x16  	// управляющий код конца кадра
#define	PNUM	0x4A

#define	TAG_FLOAT	0x41
#define	OCTET_STRING	0x4
#define	ARCHDATE_TAG	0x49
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
struct _Archive currents742 [] =
    {	{ARCH_PAVG,18,1026,"Измеренное значение давления газа (P1)",1,0x1,"МПа"},
	{ARCH_DPAVG,19,1028,"Измеренное значение перепада давления газа (dP1)",1,0x1,"МПа"},
	{ARCH_TAVG,20,1027,"Измеренное значение температуры газа (T1)",1,0x1,"С"},
	{ARCH_VWORK,16,1024,"Объемный расход газа рабочие условия (Qр1)",1,0x1,"м3/ч"},
	{ARCH_VNORM,12,1025,"Объемный расход газа (Q1)",1,0x1,"м3/ч"},
	{ARCH_KAVG,26,1032,"Среднее значение коэффициента сжимаемости",1,0x1,""}, //0x2
	
	{ARCH_VAGG_WORK,23,2048,"Тотальный объем в рабочих условиях по трубе 1",1,0,"м3"},
	{ARCH_VAGG_NORM,21,2049,"Тотальный объем в стандартных условиях по трубе 1",1,0,"м3"}};

struct _Archive archive742 [] =
    {	{ARCH_PAVG,18,72,"Среднее значение давления газа (P1)",1,0x2,"МПа"},
	{ARCH_TAVG,20,78,"Среднее значение температуры газа (T1)",1,0x2,"С"},
	{ARCH_DPAVG,19,84,"Среднее значение перепада давления (dP1)",1,0x2,"МПа"},
	{ARCH_VWORK,16,90,"Интегральный объем газа рабочие условия (Qр1)",1,0x2,"м3/ч"},
	{ARCH_VNORM,12,96,"Интегральный объем газа (Q1)",1,0x2,"м3/ч"},
	{ARCH_KAVG,26,108,"Среднее значение коэффициента сжимаемости",1,0x2,""},
	{ARCH_PBAR_AVG,28,42,"Среднее значение барометрического давления",1,0x2,"мм.рт.с"},
	{ARCH_VAGG_NORM,21,48,"Стандартный объем",1,0x2,"м3"}};

struct _Archive const742 [] =
{ 	{1,101,25,"Константное значение атмосферного давления",1,0,"мм.рт.ст."},
	{14,114,11,"Доля азота",1,0,"%"},
	{15,115,12,"Доля диоксида углерода",1,0,"%"},
	{1,112,10,"Процент влаги",1,0,"%"},	

	{2,102,9,"Константное значение абсолютного давления",1,1,"МПа"},
	{5,105,24,"Константное значение температуры",1,1,"C"},
	{6,106,9,"Константа плотности газа",1,0,"кг/м3"},
	{8,108,10,"Константа влажности газа",1,0,"%"},

	{21,502,3,"Время пуска счета ( запуск учета ресурсов)",1,0,"с"},
	{22,506,7,"Расчетный час ( смешение в секундах от начала суток)",1,0,"с"},
	{23,507,6,"Расчетные сутки ( смещение в секундах от начала месяца",1,0,"с"}};
//-----------------------------------------------------------------------------
struct _NScode {
    uint8_t	id;		// номер порядковый
    char	shrt[20];	// текстовое название параметра - короткое
    char	name[200];	// текстовое название параметра
};
struct _NScode nscode742 [] =
{	{0, "НС00", "Разряд батареи (напряжение батареи меньше порога 3,2 В"},
	{1, "НС01", "ЗАРЕЗЕРВИРОВАНО"},
	{2, "НС02", "Перегрузка по цепям питания датчиков давления (только для модели 02)"},
	{3, "НС03", "Активный уровень сигнала на дискретном входе D2"},
	{4, "НС04", "Сигнал Qр по каналу т1 меньше нижнего предела"},
	{5, "НС05", "Сигнал Qр по каналу т2 меньше нижнего предела"},
	{6, "НС06", "Сигнал Qр по каналу т1 превысил верхний предел"},
	{7, "НС07", "Сигнал Qр по каналу т2 превысил верхний предел"},
	{8, "НС08", "ЗАРЕЗЕРВИРОВАНО"},
	{9, "НС09", "Сигнал на входе ПД1 вне диапазона"},
	{10, "НС10", "Сигнал на входе ПД2 вне диапазона"},
	{11, "НС11", "Сигнал на входе ПД3 вне диапазона"},
	{12, "НС12", "Сигнал на входе ПД4 вне диапазона"},
	{13, "НС13", "Сигнал на входе ПД5 вне диапазона"},
	{14, "НС14", "Температура t1 вне диапазона -52...+92 °C"},
	{15, "НС15", "Температура t2 вне диапазона -52...+92 °C"},
	{16, "НС16", "Параметр P1 вышел за пределы уставок Ув, Ун."},
	{17, "НС17", "Параметр ΔP1 вышел за пределы уставок Ув, Ун."},
	{18, "НС18", "Параметр Qр1 вышел за пределы уставок Ув, Ун."},
	{19, "НС19", "Параметр P2 вышел за пределы уставок Ув, Ун."},
	{20, "НС20", "Параметр ΔP2 вышел за пределы уставок Ув, Ун."},
	{21, "НС21", "Параметр Qр2 вышел за пределы уставок Ув, Ун."},
	{22, "НС22", "Параметр ΔP3 вышел за пределы уставок Ув, Ун."},
	{23, "НС23", "Параметр P3 вышел за пределы уставок Ув, Ун."},
	{24, "НС24", "Параметр P4 вышел за пределы уставок Ув, Ун."},
	{25, "НС25", "Текущее суточное значение V по каналу ОБЩ превышает норму поставки"},
	{26, "НС26", "Отрицательное значение Кп по каналу 1"},
	{27, "НС27", "Отрицательное значение Кп по каналу 2"}};