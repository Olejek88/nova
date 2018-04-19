#ifndef _DATABASE_H_
#define _DATABASE_H_

#include <sqlite3.h>
#include "device.h"
#include "eval.h"

int  open_db(sqlite3 **, const char *);
void close_db(sqlite3 *);

int  save_param(struct device *, int, float);
int  save_all_archive(struct device *, const char *, struct archive *);
int  save_all_events(struct device *, struct events *);

#endif/*_DATABASE_H_*/
