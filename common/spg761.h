#include "eval.h"

#define ARCHIVE_NUM_MAX		100

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
//struct _AnsLog;
struct _AnsLog {
    uint8_t	checksym;	// checksum status (true - ok, false - bad)
    uint8_t	from;		// source address (SPG)
    uint8_t	to;		// reciever address (controller)
    uint8_t	func;		// answer function FNC=03h FNC=7Fh FNC=7Fh FNC=16h
    char	head[100];	// answer header (нахуй не нужен по-большому счету)
    uint8_t	pipe;		// channels or pipe number
    uint8_t	nadr;		// parametr or array number
    uint8_t	crc;		// checksum

    uint8_t	from_param;	// [index] from which parametr read
    uint8_t	quant_param;	// [index] parametrs quant

    char	data [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][70];	// [для всех запросов на чтение] значения параметров
    char	type [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][30];	// [для всех запросов на чтение] еденицы измерения
    char	time [ARCH_MAX_NPARAM][ARCHIVE_NUM_MAX][30];	// [для всех запросов на чтение] метка времени
};
typedef struct _AnsLog AnsLog;
//-----------------------------------------------------------------------------
#define FF  0xC	  // form feed
#define SOH 0x01  // начало заголовка,
#define ISI 0x1F  // указатель кода функции FNC,
#define STX 0x02  // начало тела сообщения,
#define ETX 0x03  // конец тела сообщения.
#define DLE 0x10  // символ-префикс.
#define HT  0x09  // код горизонтальной табуляции
#define ZERO  0x0 // просто ноль
#define UK  0x16  // управляющий код конца кадра

#define FNC_read		0x1D // Чтение параметров
#define FNC_time_array		0x0E // Чтение временных массивов
#define FNC_write		0x03 // Запись параметра
#define FNC_writeindex		0x14 // Запись индексного параметра
#define FNC_ptzp		0x7F // Подтверждение записи
#define FNC_readindex		0x0C // Чтение элементов индексного массива
#define FNC_answer		0x14 // Заголовок ответа на на чтение индексов
#define FNC_answer_time		0x16 // Заголовок ответа на на чтение временных массивов
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
struct _Archive archive_currents761 [] =
    {	{ARCH_VNORM,12,150,"Результат преобразования измеренных значений объемного расхода.", 1, 0, "м3/ч"},
    	{ARCH_VSUBS_NORM,13,121,"Константное (договорное) значение объемного расхода газа при рабочих условиях", 1, 0, "м3/ч"},
        {ARCH_VWORK,16,158,"Объемный расход газа при рабочих условиях", 1, 0, "м3/ч"},
    	{ARCH_PAVG,18,154,"Измеренное значение давления", 1, 0, "МПа"},
        {ARCH_DPAVG,19,150,"Результат преобразования измеренных значений перепада давления", 1, 0, "кПа"},
    	{ARCH_TAVG,20,156,"Измеренное значение температуры газа", 1, 0, "С"},
        {ARCH_VAGG_NORM,21,162,"Объем газа при стандартных условиях", 1, 0, "м3"},
	{ARCH_VAGG_WORK,23,163,"Объем газа при рабочих условиях", 1, 0, "м3"},
        {ARCH_DAVG,25,167,"Измеренное значение плотности", 1, 0, "кг/м3"},
        {ARCH_KAVG,27,166,"Измеренное значение удельной объемной теплоты сгорания", 1, 0, "МДж/м3"},
        {ARCH_TENV_AVG,29,165,"Измеренное значение температуры наружного воздуха", 1, 0, "С"}};

struct _Archive archive_hour761 [] =
    {	{ARCH_TAVG,20,200,"Архив часовой значений температуры газа", 1, 1, "С"},
	{ARCH_PAVG,18,205,"Архив часовой значений давления газа", 1000, 1, "МПа"},
        {ARCH_VNORM,12,215,"Архив часовой значений объема трансп. газа", 1, 1, "м3"},
        {ARCH_MNORM,17,210,"Архив часовой значений массы трансп. газа", 1, 4, "кг"},
    	{ARCH_MNORM,16,220,"Архив часовой значений массы трансп. газа рабочий", 1, 1, "кг"},
        {ARCH_KAVG,27,225,"Архив часовой значений средневзвешенной удельной объемной теплоты сгорания", 1, 1, "кПа"}};
//        {ARCH_TENV_AVG,29,82,"Архив часовой значений температуры наружного воздуха", 1, 0, "С"}};

struct _Archive archive_day761 [] =
    {	{ARCH_TAVG,20,201,"Архив суточный значений температуры газа", 1, 2, "С"},
    	{ARCH_PAVG,18,206,"Архив суточный значений давления газа", 1000, 2, "МПа"},
        {ARCH_VNORM,12,216,"Архив суточный значений объема трансп. газа", 1, 2, "м3"},
        {ARCH_MNORM,17,211,"Архив суточный значений массы трансп. газа", 1, 4, "кг"},
    	{ARCH_MNORM,16,221,"Архив суточный значений массы рабочий трансп. газа", 1, 2, "кг"},
        {ARCH_KAVG,27,226,"Архив суточный значений удельной объемной теплоты сгорания газа", 1000, 2, "МДж/м3"}};

struct _Archive archive_month761 [] =
    {	{ARCH_TAVG,20,202,"Архив по месяцам значений температуры газа", 1, 4, "С"},
        {ARCH_PAVG,18,207,"Архив по месяцам значений давления газа", 1000, 4, "МПа"},
	{ARCH_VNORM,12,217,"Архив по месяцам значений объема трансп. газа", 1, 4, "м3"},
        {ARCH_MNORM,17,212,"Архив по месяцам значений массы трансп. газа", 1, 4, "кг"},
	{ARCH_VWORK,16,222,"Архив по месяцам рабочий массы трансп. газа",1,0x1,"М3/ч"},
	{ARCH_KAVG,27,227,"Архив по месяцам значений удельной объемной теплоты сгорания газа", 1000, 4, "МДж/м3"}};

struct _Archive archive_const761 [] =
{ 	{1,105,37,"Константное значение атмосферного давления",1,0,"мм.рт.ст."},
	{2,106,113,"Константное значение абсолютного давления",1,1000,"МПа"},
	{3,107,110,"Константное значение перепада давления",1,1000,"кПа"},
	{4,108,40,"Константное значение температуры наружного воздуха",1,0,"C"},
	{5,109,114,"Константное значение температуры",1,1000,"C"},
	{6,110,107,"Константа плотности газа",1,1000,"кг/м3"},
	{7,111,149,"Коэффициент сжимаемости газа",1,1009,"б/р"},
	{8,112,105,"Константа влажности газа",1,1000,"%"},
	{9,113,125,"Доля метана",1,1000,"%"},
	{10,114,125,"Доля этана",1,1001,"%"},
	{11,115,125,"Доля пропана",1,1002,"%"},
	{12,116,125,"Доля н-бутана",1,1003,"%"},
	{13,117,125,"Доля и-бутана",1,1004,"%"},
	{14,118,125,"Доля азота",1,1005,"%"},
	{15,119,125,"Доля диоксида углерода",1,1006,"%"},
	{16,120,125,"Доля сероводорода",1,1007,"%"},
	{17,121,106,"Константа удельной объемной теплоты сгорания",1,1000,"МДж/м3"},
	{20,500,60,"Локальное время  корректора (от начала эпохи) 00:00:00  1 января 1970 года",1,560,""},
	{21,502,20,"Время пуска счета ( запуск учета ресурсов)",1,520,""},
	{22,506,24,"Расчетный часа ( смешение в секундах от начала суток)",1,0,"с"},
	{23,507,25,"Расчетные сутки ( смещение в секундах от начала месяца",1,0,"с"},
	{24,509,22,"Коррекция времени вычислителя корректора в сутки (секунд)",1,1000,"с"}};

struct _Archive archive_current_potr761 [] =
{	{ARCH_VNORM,12,348,"Объемный расход газа при стандартных условиях по потребителю", 1, 0, "м3/ч"},
        {ARCH_VWORK,16,350,"Объемный расход газа при рабочих условиях", 1, 0, "м3/ч"},
	{ARCH_VAGG_NORM,21,358,"Объем газа при стандартных условиях", 1, 0, "м3"},
        {ARCH_VAGG_WORK,23,360,"Объем газа при рабочих условиях", 1, 0, "м3"}};
//-----------------------------------------------------------------------------