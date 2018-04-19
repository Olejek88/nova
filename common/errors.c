#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "errors.h"

/*
 *
 */
static int error_id;
static int save_errno;
static char error_msg[1024];

/* */
int
get_error_id(void)
{
	return error_id;
}

const char *
get_error_msg(void)
{
	return error_msg;
}

int
get_last_errno(void)
{
	return save_errno;
}

void
set_error(int id, const char *msg)
{
	snprintf(error_msg, sizeof(error_msg), "%s", msg);
	error_id = id;
}

void
set_regex_error(int errcode, const regex_t *re)
{
	error_id = ERR_SYSTEM;
	regerror(errcode, re, error_msg, sizeof(error_msg));
}

void
set_system_error(const char *func)
{
	strcpy(error_msg, func);
	error_id = ERR_SYSTEM;
	save_errno = errno;
}

