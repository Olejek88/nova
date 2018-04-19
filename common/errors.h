#ifndef _ERRORS_H_
#define _ERRORS_H_

#include <sys/types.h>
#include <regex.h>

enum errors_numbers {
	ERR_OK,
	ERR_SYSTEM,
	ERR_TIMEOUT,
	ERR_LOST_DEVICE,
	ERR_BADMSG,
	ERR_BUSY,
	ERR_CONNECT,
	ERR_BADTRANSPORT
};

/* */
int          get_error_id(void);
int          get_last_errno(void);
const char * get_error_msg(void);

void set_error(int, const char *);
void set_system_error(const char *);
void set_regex_error(int, const regex_t *);

#endif/*_ERRORS_H_*/
