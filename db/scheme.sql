
CREATE TABLE 'mparam' (
	'param_code' INTEGER  NOT NULL,
	'value' REAL,
	'last_read' INTEGER,
	'read_interval' INTEGER,
	'min_value' REAL,
	'max_value' REAL,
	'vktype' TEXT,
	PRIMARY KEY(param_code, vktype)
);

CREATE TABLE 'archives' (
	'vktype' TEXT,
	'atype' TEXT,
	'curr_tm' TEXT,
	vnorm REAL,
	vsubs_norm REAL,
	mnorm REAL,
	msubs_norm REAL,
	vwork REAL,
	vsubs_work REAL,
	pavg REAL,
	dpavg REAL,
	tavg REAL,
	pbar_avg REAL,
	tenv_avg REAL,
	vagg_norm REAL,
	magg_norm REAL,
	vagg_work REAL,
	sensor_val REAL,
	davg REAL,
	kavg REAL,
	havg REAL,
	PRIMARY KEY(vktype, atype, curr_tm)
);

CREATE TABLE 'events' (
	'vktype' TEXT,
	'value' INTEGER NOT NULL,
	'curr_tm' TEXT NOT NULL,
	PRIMARY KEY(curr_tm, vktype)
);

