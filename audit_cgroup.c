/*  Copyright (c) 2021, Marian Marinov
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; version 2 of the
    License.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335 USA */

/**
  @file

  This plugin allows to put threads in separate cgroups if cgroups are mounted.

  It checks if /sys/fs/cgroup/cpu and /sys/fs/cgroup/memory are present and
  for each cgroup type it then checks if the mysql cgroup is present.

  If neither cpu, nor memory are present the plugin set it self as disabled.

  If the mysql cgroup is present, it then checks if mapping for the current
  user is present in the mysql DB. Then it checks if user cgroup limits
  were set in the DB. If so, it checks if we already have a cgroup.
  If we don't have a cgroup for the current user, we create it.
  After creation we are initializing the groups with the parameters from
  the DB.

  Upon disconnect, the plugin remove the thread_id from the current cgroup.
*/

#include <my_global.h>
#include <mysql/plugin_audit.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#define CGROUP_PLUGIN_VERSION "0.13"
#define CGROUP_HEX_VERSION 0x000d
#define CGROUP_DEBUG
#define CGROUP_CPU	"/sys/fs/cgroup/cpu/mysql"
#define CGROUP_MEM	"/sys/fs/cgroup/memory/mysql"
#define CGROUP_BLK	"/sys/fs/cgroup/blkio/mysql"

// Global variables
#ifdef CGROUP_DEBUG
FILE *f;
#endif
int cgroup_enabled;
static my_bool sys_cgroup_enabled = TRUE;

static void update_cgroup_enabled(MYSQL_THD thd  __attribute__((unused)),
				struct st_mysql_sys_var *var  __attribute__((unused)),
				void *var_ptr  __attribute__((unused)),
				const void *save  __attribute__((unused))) {
	if (*(my_bool*) save == TRUE) {
		sys_cgroup_enabled = TRUE;
		cgroup_enabled = 1;
	} else {
		sys_cgroup_enabled = FALSE;
		cgroup_enabled = 0;
	}
};

static MYSQL_SYSVAR_BOOL(
	enabled,						// name of the plugin variable plugin_name_VAR
	sys_cgroup_enabled,				// mysql internal variable state
	PLUGIN_VAR_OPCMDARG,			// set it as optional
	"Turn on/off cGroup limits.",	// description
	NULL,
	update_cgroup_enabled,			// function to handle the change of value
	TRUE							// initial value, if not defined
);

static int create_dir(char *cg_dir) {
	if (!mkdir(cg_dir, O_RDWR) && errno != EEXIST)
			return 1;
	return 0;
}

static int put_thread_in_cg(char *tasks_file) {
	FILE *cg;
	pid_t tid = gettid();
	cg = fopen(tasks_file, "a");
	if (!cg) {
#ifdef CGROUP_DEBUG
		fprintf(f, "unable to open cgroup tasks file: %s\n", tasks_file);
		fflush(f);
#endif
		return 1;
	}
	fprintf(cg, "%d\n", tid);
	fclose(cg);
	return 0;
}

static int set_cgroup(const struct mysql_event_connection *ev) {
	char *cgroup_path = NULL;
	int path_len = 36;	// the len of CGROUP_MEM + / + @ + / + the tasks file + null char
	int ret = 0;

	path_len += ev->user_length;
	cgroup_path = malloc(path_len);
	memset(cgroup_path, '\0', path_len);

	// do nothing for root
	if (strncmp(ev->user, "root\0", 5) == 0)
		goto out;

	// /sys/fs/cgroup/cpu/mysql/user@server
	sprintf(cgroup_path, "%s/%s@%s", CGROUP_CPU, ev->user, ev->host);
	if (create_dir(cgroup_path) == 1) {
		ret = 1;
		goto out;
	}
	// /sys/fs/cgroup/memory/mysql/user@server
	sprintf(cgroup_path, "%s/%s@%s", CGROUP_MEM, ev->user, ev->host);
	if (create_dir(cgroup_path) == 1) {
		ret = 2;
		goto out;
	};
	sprintf(cgroup_path, "%s/%s@%s/tasks", CGROUP_CPU, ev->user, ev->host);
	if (put_thread_in_cg(cgroup_path) == 1) {
		ret = 3;
		goto out;
	}

	sprintf(cgroup_path, "%s/%s@%s/tasks", CGROUP_MEM, ev->user, ev->host);
	if (put_thread_in_cg(cgroup_path) == 1)
		ret = 4;

	out:
		free(cgroup_path);
	return ret;
}

static int set_limits(const struct mysql_event_connection *ev) {
	int ret = 0;

	return ret;
}

static int cgroup_init(void *arg __attribute__((unused))) {
	// only enable the module if both cgroups are already created
	if (access(CGROUP_CPU, X_OK) && access(CGROUP_MEM, X_OK)) {
		cgroup_enabled = 1;
	} else {
		cgroup_enabled = 0;
	}
	// initialize global variables here
#ifdef CGROUP_DEBUG
	f = fopen("cgroup.log", "a");
	if (!f)
		return 1;

	fprintf(f, "audit_cgroup: Loaded plugin - version %s\n", CGROUP_PLUGIN_VERSION);
	if (cgroup_enabled)
		fprintf(f, "audit_cgroup: enabled\n");
	else
		fprintf(f, "audit_cgroup: disabled as cgroup %s or %s is missing\n", CGROUP_CPU, CGROUP_MEM);

	fflush(f);
#endif

	return 0;
}

static int cgroup_deinit(void *arg __attribute__((unused))) {
	cgroup_enabled = 0;
#ifdef CGROUP_DEBUG
	fprintf(f, "audit_cgroup: Removed plugin - version %s\n", CGROUP_PLUGIN_VERSION);
	fclose(f);
#endif
	return 0;
}

static void return_thread_to_main(void) {
	FILE *cg;
	// Move the thread back to the main cgroup
	cg = fopen("/sys/fs/cgroup/cpu/mysql/tasks", "a");
	if (!cg) {
#ifdef CGROUP_DEBUG
		fprintf(f, "unable to open cpu cgroup tasks file\n");
#endif
		return;
	}
	fprintf(cg, "%d\n", gettid());
	fclose(cg);
	cg = fopen("/sys/fs/cgroup/memory/mysql/tasks", "a");
	if (!cg) {
#ifdef CGROUP_DEBUG
		fprintf(f, "unable to open mem cgroup tasks file\n");
#endif
		return;
	}
	fprintf(cg, "%d\n", gettid());
	fclose(cg);
}

static void cgroup_plugin(
		MYSQL_THD thd __attribute__((unused)),
		unsigned int event_class,
		const void *event) {
	const struct mysql_event_connection *ev;

	if (cgroup_enabled == 0)
		return;

	// Only check events related to connections
	if (event_class != MYSQL_AUDIT_CONNECTION_CLASS)
		return;

	ev = (const struct mysql_event_connection *) event;

#ifdef CGROUP_DEBUG
	if (ev->event_subclass == 0)
		fprintf(f, "audit_cgroup: event: CONNECT thread_id: %d user: %s@%s %s\n", gettid(), ev->user, ev->host, ev->ip);
	else
		fprintf(f, "audit_cgroup: event: DISCONNECT thread_id: %d user: %s@%s %s\n", gettid(), ev->user, ev->host, ev->ip);
	fflush(f);
#endif

	if (ev->event_subclass == MYSQL_AUDIT_CONNECTION_CONNECT) {
		// Put the thread in the proper cgroup
#ifdef CGROUP_DEBUG
		fprintf(f, "audit_cgroup: set_cgroup: %d\n", set_cgroup(ev));
		fflush(f);
#else
		set_cgroup(ev);
#endif
	} else if (ev->event_subclass == MYSQL_AUDIT_CONNECTION_CHANGE_USER) {
		// Change the thread cgroup when the user is changed
		set_cgroup(ev);
	} else if (ev->event_subclass == MYSQL_AUDIT_CONNECTION_DISCONNECT) {
		return_thread_to_main();
	}
	return;
}

static struct st_mysql_sys_var* cgroup_vars[] = {
	MYSQL_SYSVAR(enabled),
	NULL
};

static struct st_mysql_audit cgroup_handler = {
	MYSQL_AUDIT_INTERFACE_VERSION,	// interface version
	NULL,							// release_thd function
	cgroup_plugin,					// notify function
	{ MYSQL_AUDIT_CONNECTION_CHANGE_USER }
};


maria_declare_plugin(audit_cgroup) {
	MYSQL_AUDIT_PLUGIN,				// type
	&cgroup_handler,				// descriptor
	"audit_cgroup",					// name
	"Marian Marinov",				// author
	"cGroup resouce limits plugin",	// description
	PLUGIN_LICENSE_GPL,
	cgroup_init,					// init function (when loading the module)
	cgroup_deinit,					// deinit function (when unloading the module)
	CGROUP_HEX_VERSION,				// version
	NULL,							// status variables
	cgroup_vars,					// system variables
	CGROUP_PLUGIN_VERSION,			// Plugin version string
	MariaDB_PLUGIN_MATURITY_BETA	// MariaDB maturity UNKNOWN, EXPERIMENTAL, ALPHA, BETA, GAMMA, STABLE
}
maria_declare_plugin_end;
