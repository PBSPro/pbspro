/*
 * Copyright (C) 1994-2021 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */

#ifndef	_HOOK_H
#define	_HOOK_H
#ifdef	__cplusplus
extern "C" {
#endif


/*
 * hook.h - structure definitions for hook objects
 *
 * Include Files Required:
 *	<sys/types.h>
 *	"list_link.h"
 *	"batch_request.h"
 *	"pbs_ifl.h"
 */
#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define	FALSE	0
#endif

#include "pbs_python.h"

enum hook_type {
	HOOK_SITE,
	HOOK_PBS
};
typedef enum hook_type hook_type;

enum hook_user {
	HOOK_PBSADMIN,
	HOOK_PBSUSER
};
typedef enum hook_user hook_user;

#define HOOK_FAIL_ACTION_NONE				0x01
#define HOOK_FAIL_ACTION_OFFLINE_VNODES			0x02
#define HOOK_FAIL_ACTION_CLEAR_VNODES			0x04
#define HOOK_FAIL_ACTION_SCHEDULER_RESTART_CYCLE	0x08

/* server hooks */

#define HOOK_EVENT_QUEUEJOB 	0x01
#define HOOK_EVENT_MODIFYJOB 	0x02
#define HOOK_EVENT_RESVSUB	0x04
#define HOOK_EVENT_MOVEJOB	0x08
#define HOOK_EVENT_RUNJOB	0x10
#define HOOK_EVENT_ENDJOB	0x800000
#define HOOK_EVENT_PROVISION	0x20
#define HOOK_EVENT_PERIODIC	0x8000
#define HOOK_EVENT_RESV_END	0x10000
#define HOOK_EVENT_MANAGEMENT	0x200000
#define HOOK_EVENT_MODIFYVNODE	0x400000


/* mom hooks */
#define HOOK_EVENT_EXECJOB_BEGIN	0x40
#define HOOK_EVENT_EXECJOB_PROLOGUE	0x80
#define HOOK_EVENT_EXECJOB_EPILOGUE	0x100
#define HOOK_EVENT_EXECJOB_END		0x200
#define HOOK_EVENT_EXECJOB_PRETERM	0x400
#define HOOK_EVENT_EXECJOB_LAUNCH	0x800
#define HOOK_EVENT_EXECHOST_PERIODIC	0x1000
#define HOOK_EVENT_EXECHOST_STARTUP	0x2000
#define HOOK_EVENT_EXECJOB_ATTACH	0x4000
#define HOOK_EVENT_EXECJOB_RESIZE	0x20000
#define HOOK_EVENT_EXECJOB_ABORT	0x40000
#define HOOK_EVENT_EXECJOB_POSTSUSPEND	0x80000
#define HOOK_EVENT_EXECJOB_PRERESUME	0x100000

#define MOM_EVENTS	(HOOK_EVENT_EXECJOB_BEGIN|HOOK_EVENT_EXECJOB_PROLOGUE|HOOK_EVENT_EXECJOB_EPILOGUE|HOOK_EVENT_EXECJOB_END|HOOK_EVENT_EXECJOB_PRETERM|HOOK_EVENT_EXECHOST_PERIODIC|HOOK_EVENT_EXECJOB_LAUNCH|HOOK_EVENT_EXECHOST_STARTUP|HOOK_EVENT_EXECJOB_ATTACH|HOOK_EVENT_EXECJOB_RESIZE|HOOK_EVENT_EXECJOB_ABORT|HOOK_EVENT_EXECJOB_POSTSUSPEND|HOOK_EVENT_EXECJOB_PRERESUME)
#define USER_MOM_EVENTS	(HOOK_EVENT_EXECJOB_PROLOGUE|HOOK_EVENT_EXECJOB_EPILOGUE|HOOK_EVENT_EXECJOB_PRETERM)
#define FAIL_ACTION_EVENTS (HOOK_EVENT_EXECJOB_BEGIN|HOOK_EVENT_EXECHOST_STARTUP|HOOK_EVENT_EXECJOB_PROLOGUE)
struct hook {
	char 		*hook_name;	/* unique name of the hook */
	hook_type	type;		/* site-defined or pbs builtin */
	int		enabled;	/* TRUE or FALSE */
	int		debug;		/* TRUE or FALSE */
	hook_user	user;    	/* who executes the hook */
	unsigned int	fail_action;  	/* what to do when hook fails unexpectedly */
	unsigned int	event;		/* event  flag */
	short		order;		/* -1000..1000 */
	/* -1000..0 for pbs hooks */
	/* 1..1000 for site hooks */
	int		alarm;		/* number of seconds */
	void		*script;	/* actual script content in some fmt */

	int		freq;		/* # of seconds in between calls */
	/* install hook */
	int		pending_delete; /* set to 1 if a mom hook and pending */
	unsigned long	hook_control_checksum;	/* checksum for .HK file */
	unsigned long	hook_script_checksum;	/* checksum for .PY file */
	unsigned long	hook_config_checksum;	/* checksum for .CF file */
	/* deletion */
	pbs_list_link	hi_allhooks;
	pbs_list_link	hi_queuejob_hooks;
	pbs_list_link	hi_modifyjob_hooks;
	pbs_list_link	hi_resvsub_hooks;
	pbs_list_link	hi_movejob_hooks;
	pbs_list_link	hi_runjob_hooks;
	pbs_list_link	hi_endjob_hooks;
	pbs_list_link	hi_management_hooks;
	pbs_list_link	hi_modifyvnode_hooks;
	pbs_list_link 	hi_provision_hooks;
	pbs_list_link 	hi_periodic_hooks;
	pbs_list_link	hi_resv_end_hooks;
	pbs_list_link	hi_execjob_begin_hooks;
	pbs_list_link	hi_execjob_prologue_hooks;
	pbs_list_link	hi_execjob_epilogue_hooks;
	pbs_list_link	hi_execjob_end_hooks;
	pbs_list_link	hi_execjob_preterm_hooks;
	pbs_list_link	hi_execjob_launch_hooks;
	pbs_list_link 	hi_exechost_periodic_hooks;
	pbs_list_link 	hi_exechost_startup_hooks;
	pbs_list_link	hi_execjob_attach_hooks;
	pbs_list_link	hi_execjob_resize_hooks;
	pbs_list_link	hi_execjob_abort_hooks;
	pbs_list_link	hi_execjob_postsuspend_hooks;
	pbs_list_link	hi_execjob_preresume_hooks;
	struct work_task *ptask;		    /* work task pointer, used in periodic hooks */
};

typedef struct hook hook;

/* Hook-related files and directories */
#define	HOOK_FILE_SUFFIX	".HK"	/* hook control file */
#define	HOOK_SCRIPT_SUFFIX	".PY"	/* hook script file */
#define	HOOK_REJECT_SUFFIX	".RJ"	/* hook error reject message */
#define HOOK_TRACKING_SUFFIX	".TR"	/* hook pending action tracking file */
#define HOOK_BAD_SUFFIX		".BD"	/* a bad (moved out of the way) hook file */
#define	HOOK_CONFIG_SUFFIX	".CF"
#define PBS_HOOKDIR		"hooks"
#define PBS_HOOK_WORKDIR	PBS_HOOKDIR "/tmp"
#define PBS_HOOK_TRACKING	PBS_HOOKDIR "/tracking"
#define PBS_HOOK_NAME_SIZE	512

/* Some hook-related buffer sizes */
#define	HOOK_BUF_SIZE	512
#define	HOOK_MSG_SIZE	3172

/* parameters to import and export qmgr command */
#define CONTENT_TYPE_PARAM      "content-type"
#define CONTENT_ENCODING_PARAM  "content-encoding"
#define INPUT_FILE_PARAM        "input-file"
#define OUTPUT_FILE_PARAM       "output-file"

/* attribute default values */
/* Save only the non-defaults */
#define	HOOK_TYPE_DEFAULT	HOOK_SITE
#define HOOK_USER_DEFAULT	HOOK_PBSADMIN
#define HOOK_FAIL_ACTION_DEFAULT	HOOK_FAIL_ACTION_NONE
#define	HOOK_ENABLED_DEFAULT	TRUE
#define	HOOK_DEBUG_DEFAULT	FALSE
#define	HOOK_EVENT_DEFAULT	0
#define	HOOK_ORDER_DEFAULT	1
#define	HOOK_ALARM_DEFAULT	30
#define	HOOK_FREQ_DEFAULT	120
#define	HOOK_PENDING_DELETE_DEFAULT 0

/* Various attribute names in string format */
#define	HOOKATT_NAME		"hook_name"
#define	HOOKATT_TYPE		"type"
#define	HOOKATT_USER		"user"
#define	HOOKATT_ENABLED		"enabled"
#define	HOOKATT_DEBUG		"debug"
#define	HOOKATT_EVENT		"event"
#define	HOOKATT_ORDER		"order"
#define	HOOKATT_ALARM		"alarm"
#define	HOOKATT_FREQ		"freq"
#define	HOOKATT_FAIL_ACTION	"fail_action"
#define	HOOKATT_PENDING_DELETE  "pending_delete"

#define	HOOK_PBS_PREFIX		"PBS"  /* valid Hook name prefix for PBS hook */

/* Valid Hook type values */
#define	HOOKSTR_SITE		"site"
#define	HOOKSTR_PBS		"pbs"
#define	HOOKSTR_UNKNOWN		"" /* empty string is the value for */
/* unknown Hook type and Hook user */

/*  Valid Hook user values */
#define	HOOKSTR_ADMIN		"pbsadmin"
#define	HOOKSTR_USER		"pbsuser"

/*  Valid Hook fail_action values */
#define	HOOKSTR_FAIL_ACTION_NONE		"none"
#define	HOOKSTR_FAIL_ACTION_OFFLINE_VNODES	"offline_vnodes"
#define	HOOKSTR_FAIL_ACTION_CLEAR_VNODES	"clear_vnodes_upon_recovery"
#define	HOOKSTR_FAIL_ACTION_SCHEDULER_RESTART_CYCLE "scheduler_restart_cycle"

/* Valid hook enabled or debug values */
#define	HOOKSTR_TRUE		"true"
#define	HOOKSTR_FALSE		"false"

/* Valid Hook event values */
#define	HOOKSTR_QUEUEJOB	"queuejob"
#define	HOOKSTR_MODIFYJOB	"modifyjob"
#define	HOOKSTR_RESVSUB		"resvsub"
#define	HOOKSTR_MOVEJOB		"movejob"
#define	HOOKSTR_RUNJOB		"runjob"
#define HOOKSTR_PROVISION	"provision"
#define HOOKSTR_PERIODIC	"periodic"
#define HOOKSTR_RESV_END	"resv_end"
#define HOOKSTR_MANAGEMENT      "management"
#define HOOKSTR_ENDJOB		"endjob"
#define HOOKSTR_MODIFYVNODE      "modifyvnode"
#define HOOKSTR_EXECJOB_BEGIN   "execjob_begin"
#define HOOKSTR_EXECJOB_PROLOGUE "execjob_prologue"
#define HOOKSTR_EXECJOB_EPILOGUE "execjob_epilogue"
#define HOOKSTR_EXECJOB_END	"execjob_end"
#define HOOKSTR_EXECJOB_PRETERM	"execjob_preterm"
#define HOOKSTR_EXECJOB_LAUNCH	"execjob_launch"
#define HOOKSTR_EXECJOB_ATTACH	"execjob_attach"
#define HOOKSTR_EXECJOB_RESIZE "execjob_resize"
#define HOOKSTR_EXECJOB_ABORT	"execjob_abort"
#define HOOKSTR_EXECJOB_POSTSUSPEND	"execjob_postsuspend"
#define HOOKSTR_EXECJOB_PRERESUME	"execjob_preresume"
#define HOOKSTR_EXECHOST_PERIODIC "exechost_periodic"
#define HOOKSTR_EXECHOST_STARTUP "exechost_startup"
#define	HOOKSTR_NONE		"\"\""	/* double quote val for event == 0 */

#define HOOKSTR_FAIL_ACTION_EVENTS HOOKSTR_EXECJOB_BEGIN ", " HOOKSTR_EXECHOST_STARTUP ", " HOOKSTR_EXECJOB_PROLOGUE

/* Valid Hook order valid ranges */
/* for SITE hooks */
#define HOOK_SITE_ORDER_MIN 	1
#define HOOK_SITE_ORDER_MAX	1000

/* for PBS hooks */
#define HOOK_PBS_ORDER_MIN	-1000
#define HOOK_PBS_ORDER_MAX	2000

/* For cleanup_hooks_workdir() parameters */
#define	HOOKS_TMPFILE_MAX_AGE	1200  /* a temp hooks file's maximum age (in  */
/* in secs) before getting removed      */
#define HOOKS_TMPFILE_NEXT_CLEANUP_PERIOD 600 /* from this time, when (secs) */
/* cleanup_hooks_workdir()     */
/* gets called. 		     */

/* for import/export actions */
#define	HOOKSTR_CONTENT		"application/x-python"
#define	HOOKSTR_CONFIG		"application/x-config"
#define	HOOKSTR_BASE64		"base64"
#define HOOKSTR_DEFAULT		"default"

#define	PBS_HOOK_CONFIG_FILE	"PBS_HOOK_CONFIG_FILE"

/* default import statement printed out on a "print hook" request */
#define PRINT_HOOK_IMPORT_CALL  "import hook %s application/x-python base64 -\n"
#define PRINT_HOOK_IMPORT_CONFIG  "import hook %s application/x-config base64 -\n"

/* Format: The first %s is the directory location */
#define	FMT_HOOK_PREFIX "hook_"
#define	FMT_HOOK_JOB_OUTFILE "%s" FMT_HOOK_PREFIX "%s.out"
#define	FMT_HOOK_INFILE "%s" FMT_HOOK_PREFIX "%s_%s_%d.in"
#define	FMT_HOOK_OUTFILE "%s" FMT_HOOK_PREFIX "%s_%s_%d.out"
#define	FMT_HOOK_DATAFILE "%s" FMT_HOOK_PREFIX "%s_%s_%d.data"
#define	FMT_HOOK_SCRIPT "%s" FMT_HOOK_PREFIX "script%d"
#define	FMT_HOOK_SCRIPT_COPY "%s" FMT_HOOK_PREFIX "script_%s.%s"
#define	FMT_HOOK_CONFIG "%s" FMT_HOOK_PREFIX "config%d"
#define	FMT_HOOK_CONFIG_COPY "%s" FMT_HOOK_PREFIX "config_%s.%s"
#define	FMT_HOOK_RESCDEF "%s" FMT_HOOK_PREFIX "resourcedef%d"
#define	FMT_HOOK_RESCDEF_COPY "%s" FMT_HOOK_PREFIX "resourcedef.%s"
#define	FMT_HOOK_LOG "%s" FMT_HOOK_PREFIX "log%d"

/* Special log levels  - values must not intersect PBS_EVENT* values in log.h */

#define SEVERITY_LOG_DEBUG		0x0005		/* syslog DEBUG */
#define SEVERITY_LOG_WARNING		0x0006		/* syslog WARNING */
#define SEVERITY_LOG_ERR		0x0007		/* syslog ERR */

/* Power hook name */
#define PBS_POWER "PBS_power"

/* External functions */
extern int
set_hook_name(hook *, char *, char *, size_t);
extern int
set_hook_enabled(hook *, char *, char *, size_t);
extern int
set_hook_debug(hook *, char *, char *, size_t);
extern int
set_hook_type(hook *, char *, char *, size_t, int);
extern int
set_hook_user(hook *, char *, char *, size_t, int);
extern int
set_hook_event(hook *, char *, char *, size_t);
extern int
add_hook_event(hook *, char *, char *, size_t);
extern int
del_hook_event(hook *, char *, char *, size_t);
extern int
set_hook_fail_action(hook *, char *, char *, size_t, int);
extern int
add_hook_fail_action(hook *, char *, char *, size_t, int);
extern int
del_hook_fail_action(hook *, char *, char *, size_t);
extern int
set_hook_order(hook *, char *, char *, size_t);
extern int
set_hook_alarm(hook *, char *, char *, size_t);
extern int
set_hook_freq(hook *, char *, char *, size_t);

extern int
unset_hook_enabled(hook *, char *, size_t);
extern int
unset_hook_debug(hook *, char *, size_t);
extern int
unset_hook_type(hook *, char *, size_t);
extern int
unset_hook_user(hook *, char *, size_t);
extern int
unset_hook_fail_action(hook *, char *, size_t);
extern int
unset_hook_event(hook *, char *, size_t);
extern int
unset_hook_order(hook *, char *, size_t);
extern int
unset_hook_alarm(hook *, char *, size_t);
extern int
unset_hook_freq(hook *, char *, size_t);
extern hook *hook_alloc(void);
extern void hook_free(hook *, void (*)(struct python_script *));
extern void hook_purge(hook *, void (*)(struct python_script *));
extern int hook_save(hook *);
extern hook *hook_recov(char *, FILE *, char *, size_t,
	int (*)(const char *, struct python_script **),
	void (*)(struct python_script *));

extern hook *find_hook(char *);
extern hook *find_hookbyevent(int);
extern int encode_hook_content(char *, char *, char *, char *, size_t);
extern int decode_hook_content(char *, char *, char *, char *, size_t);
extern void print_hooks(unsigned int);
extern void mark_hook_file_bad(char *);

extern char *hook_event_as_string(unsigned int);
extern unsigned int hookstr_event_toint(char *);
extern char *hook_enabled_as_string(int);
extern char *hook_debug_as_string(int);
extern char *hook_type_as_string(hook_type);
extern char *hook_alarm_as_string(int);
extern char *hook_freq_as_string(int);
extern char *hook_order_as_string(short);
extern char *hook_user_as_string(hook_user);
extern char *hook_fail_action_as_string(unsigned int);
extern int num_eligible_hooks(unsigned int);

#ifdef	_WORK_TASK_H
extern void cleanup_hooks_workdir(struct work_task *);
#endif

#ifdef WIN32
#define	ALARM_HANDLER_ARG void
#else
#define	ALARM_HANDLER_ARG int sig
#endif

extern void catch_hook_alarm(ALARM_HANDLER_ARG);
extern int set_alarm(int sec, void (*)(void));

extern void hook_perf_stat_start(char *label, char *action, int);
extern void hook_perf_stat_stop(char *label, char *action, int);
#define HOOK_PERF_POPULATE "populate"
#define HOOK_PERF_FUNC "hook_func"
#define HOOK_PERF_RUN_CODE "run_code"
#define HOOK_PERF_START_PYTHON "start_interpreter"
#define HOOK_PERF_LOAD_INPUT "load_hook_input_file"
#define HOOK_PERF_HOOK_OUTPUT "hook_output"
#define HOOK_PERF_POPULATE_VNODE "populate:pbs.event().vnode"
#define HOOK_PERF_POPULATE_VNODE_O "populate:pbs.event().vnode_o"
#define HOOK_PERF_POPULATE_VNODELIST "populate:pbs.event().vnode_list"
#define HOOK_PERF_POPULATE_VNODELIST_FAIL "populate:pbs.event().vnode_list_fail"
#define HOOK_PERF_POPULATE_RESVLIST "populate:pbs.event().resv_list"
#define HOOK_PERF_POPULATE_JOBLIST "populate:pbs.event().job_list"
#define HOOK_PERF_LOAD_DATA "load_hook_data"
#ifdef	__cplusplus
}
#endif
#endif	/* _HOOK_H */
