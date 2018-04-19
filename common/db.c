#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sqlite3.h>
#include <time.h>
#include <math.h>
#include "db.h"
#include "device.h"
#include "eval.h"


static int
is_row_exists_db(sqlite3 *db, const char *sql, int *rval)
{
	sqlite3_stmt *stmt;
	int ret, sr;

	if (SQLITE_OK != sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL)) {
		fprintf(stderr, "Error: %s\n", sqlite3_errmsg(db));
		ret = -1;
	} else {
		ret = 0;

		sr = sqlite3_step(stmt);
		if (SQLITE_DONE == sr)
			*rval = 0;
		else if (SQLITE_ROW == sr)
			*rval = 1;
		else
			ret = -1;

		sqlite3_finalize(stmt);
	}
	return ret;
}

static int
is_param_expired(sqlite3 *db, const char *vk, int code, int *rval)
{
	char sql[512];

	snprintf(sql, sizeof(sql),
		"SELECT 1 FROM mparam "
		"WHERE last_read + read_interval < %ld AND param_code = %d "
		" AND vktype = '%s' ",
		(long)(time(NULL)),
		code,
		vk
	);

	return is_row_exists_db(db, sql, rval);
}

static int
is_param_exists(sqlite3 *db, const char *vk, int code, int *rval)
{
	char sql[512];

	snprintf(sql, sizeof(sql),
		"SELECT 1 FROM mparam WHERE vktype = '%s' and param_code = %d",
		vk, code
	);
	return is_row_exists_db(db, sql, rval);
}

static int
upd_param_db(sqlite3 *db, const char *vk, int code, float value)
{
	char sql[1024];
	char *errmsg;
	int rv;

	snprintf(sql, sizeof(sql),
		"UPDATE mparam SET last_read = %ld, value = %f WHERE "
		"param_code = %d AND vktype = '%s'",
		(long)(time(NULL)),
		value,
		code,
		vk
	);
	rv = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
	if (SQLITE_OK == rv) {
		rv = 0;
	} else {
		rv = -1;
		fprintf(stderr, "Unable to save in Database: %s\n", errmsg);
	}
	sqlite3_free(errmsg);
	return rv;
}

static int
ins_param_db(sqlite3 *db, const char *vk, int code, float value)
{
	char sql[1024];
	char *errmsg;
	int rv;

	snprintf(sql, sizeof(sql),
		"INSERT INTO mparam(vktype,param_code,value,last_read,read_interval)"
		"VALUES('%s', %d, %f, %ld, 3600)",
		vk, code, value, (long)time(NULL)
	);
	rv = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
	if (SQLITE_OK == rv) {
		rv = 0;
	} else {
		rv = -1;
		fprintf(stderr, "Unable to save in Database %s\n", errmsg);
	}
	sqlite3_free(errmsg);
	return rv;
}

int
open_db(sqlite3 **pDb, const char *s)
{
	int ret;

	if (SQLITE_OK == sqlite3_open(s, pDb)) {
		ret = 0;
	} else {
		ret = -1;
	}
	return ret;
}

void
close_db(sqlite3 *Db)
{
	if (Db)
		sqlite3_close(Db);
}

int
save_param(struct device *dev, int code, float value)
{
	int exists, expired, rv;

	if (-1 == is_param_exists(dev->db, dev->vktype, code, &exists)) {
		rv = -1;
	} else if (!exists) {
		rv = ins_param_db(dev->db, dev->vktype, code, value);
	} else if (-1 == is_param_expired(dev->db, dev->vktype, code, &expired)) {
		rv = -1;
	} else if (expired) {
		rv = upd_param_db(dev->db, dev->vktype, code, value);
	} else {
		rv = 0;
	}
	return rv;
}

static int
save_archive(struct device *dev, const char *atype, struct archive *arch)
{
	sqlite3_stmt *stmt;
	char sql[8096];
	int i, rv;

	snprintf(sql, sizeof(sql),
		"SELECT 1 FROM archives WHERE "
		"vktype = '%s' AND atype = '%s' AND "
		"curr_tm = '%s'",
		dev->vktype, atype,
		arch->datetime
	);
	if (-1 == is_row_exists_db(dev->db, sql, &rv))
		return -1;
	if (rv)
		snprintf(sql, sizeof(sql),
			"UPDATE archives SET "
				"vnorm = ?1,"
				"vsubs_norm = ?2,"
				"mnorm = ?3,"
				"msubs_norm = ?4,"
				"vwork = ?5,"
				"vsubs_work = ?6,"
				"pavg = ?7,"
				"dpavg = ?8,"
				"tavg = ?9,"
				"pbar_avg = ?10,"
				"tenv_avg = ?11,"
				"vagg_norm = ?12,"
				"magg_norm = ?13,"
				"vagg_work = ?14,"
				"sensor_val = ?15,"
				"davg = ?16,"
				"kavg = ?17 "
			"WHERE vktype = '%s' AND atype = '%s' AND curr_tm = '%s'",
			dev->vktype, atype, arch->datetime
		);
	else
		snprintf(sql, sizeof(sql),
			"INSERT INTO archives"
			"(vnorm,vsubs_norm,mnorm,msubs_norm,vwork,vsubs_work,"
			" pavg,dpavg,tavg,pbar_avg,tenv_avg,vagg_norm,magg_norm,"
			" vagg_work,sensor_val,davg,kavg,vktype,atype,curr_tm)"
			"VALUES"
			"(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,"
			" ?16,?17,'%s','%s','%s')",
			dev->vktype, atype, arch->datetime
		);

	if (SQLITE_OK != sqlite3_prepare_v2(dev->db, sql, strlen(sql), &stmt, NULL)) {
		fprintf(stderr, "Error: %s\n", sqlite3_errmsg(dev->db));
		return -1;
	}

	for (i = 0; i < ARCH_MAX_NPARAM; ++i) {
		if (isnanf(arch->params[i]))
			sqlite3_bind_null(stmt, i + 1);
		else
			sqlite3_bind_double(stmt, i + 1, arch->params[i]);
	}
	if (SQLITE_DONE != sqlite3_step(stmt)) {
		fprintf(stderr, "Error: %s\n", sqlite3_errmsg(dev->db));
		rv = -1;
	} else {
		rv = 0;
	}

	sqlite3_finalize(stmt);

	return rv;
}

int
save_all_archive(struct device *dev, const char *atype, struct archive *archs)
{
	struct archive *p;

	for (p = archs; p; p = p->next) {
		if (-1 == save_archive(dev, atype, p))
			return -1;
	}
	return 0;
}

static int
save_event(struct device *dev, struct events *ev)
{
	char sql[1024];
	char *errmsg;
	int rv;

	snprintf(sql, sizeof(sql),
		"SELECT 1 FROM events WHERE "
		"vktype = '%s' AND curr_tm = '%s'",
		dev->vktype, ev->datetime
	);
	if (-1 == is_row_exists_db(dev->db, sql, &rv))
		return -1;

	if (rv)
		snprintf(sql, sizeof(sql),
			"UPDATE events SET value = %d WHERE "
			"vktype = '%s' AND curr_tm = '%s'",
			ev->event, dev->vktype, ev->datetime
		);
	else
		snprintf(sql, sizeof(sql),
			"INSERT INTO events(value,curr_tm,vktype)"
			"VALUES (%d,'%s', '%s')",
			ev->event, ev->datetime, dev->vktype
		);

	if (SQLITE_OK != sqlite3_exec(dev->db, sql, NULL, NULL, &errmsg)) {
		fprintf(stderr, "Error: %s\n", errmsg);
		rv = -1;
	} else {
		rv = 0;
	}
	sqlite3_free(errmsg);
	return rv;
}

int
save_all_events(struct device *dev, struct events *ev)
{
	while (ev) {
		if (-1 == save_event(dev, ev))
			return -1;
		ev = ev->next;
	}
	return 0;
}

