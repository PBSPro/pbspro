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



#ifndef _PBS_PYTHON_DEF
#define _PBS_PYTHON_DEF

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pbs_ifl.h>
#include <pbs_config.h>
#include "pbs_internal.h"
#include <log.h>
#include "list_link.h"
#include "attribute.h"

#ifdef WIN32
#define DIRSEP 		    '\\'
#define DIRSEP_STR 		"\\"
#else
#define DIRSEP 		    '/'
#define DIRSEP_STR 		"/"
#endif

#ifdef LOG_BUF_SIZE                  /* from log.h */
#define STRBUF		LOG_BUF_SIZE
#else
#define STRBUF		4096
#endif

/*
 * Header file providing external callable routines without regardless
 * of whether it is linked with the python interpreter.
 *
 * NOTE: This is the external interface to basically start/stop/run commands
 *       associated with the embedded python interpreter.
 * ====================== IMPORTANT =======================
 * This file should *NOT* depend on the Python header files
 * use generic pointers!
 * ====================== IMPORTANT =======================
 */

#define PBS_PYTHON_PROGRAM "pbs_python"
struct python_interpreter_data {
	int data_initialized;     /* data initialized */
	int interp_started;       /* status flag*/
	char *daemon_name;        /* useful for logging */
	char local_host_name[PBS_MAXHOSTNAME+1]; 	/* short host name */
	int pbs_python_types_loaded; /* The PBS python types */
	void (*init_interpreter_data)(struct python_interpreter_data *);
	void (*destroy_interpreter_data)(struct python_interpreter_data *);
};

struct python_script {
	int    check_for_recompile;
	char   *path;                        /* FULL pathname of script */
	void   *py_code_obj;                 /* the actual compiled code string
					      * type is PyCodeObject *
					      */
	void   *global_dict;                 /* this is the globals() dictionary
					      * type is PyObject *
					      */
	struct stat cur_sbuf;                /* last modification time */
};

/**
 *
 * @brief
 * 	The hook_input_param_t structure contains the input request
 * 	parameters to the pbs_python_event_set() function.
 *
 * @param[in]	rq_job - maps to a struct rq_quejob batch request.
 * @param[in]	rq_manage - maps to a struct rq_manage batch request.
 * @param[in]	rq_modifyvnode - maps to a struct rq_modifyvnode.
 * @param[in]	rq_move - maps to a struct rq_move batch request.
 * @param[in]	rq_prov - maps to a struct prov_vnode_info.
 * @param[in]	rq_run - maps to a struct rq_runjob batch request.
 * @param[in]	progname - value to pbs.event().progname in an execjob_launch
 * 				hook.
 * @param[in]	argv_list - pbs.event().argv but in list form, used by
 * 				execjob_launch hook.
 * @param[in]	env - value to pbs.event().env in an execjob_launch hook.
 * @param[in]	jobs_list - list of jobs and their attributes/resources
 * 				passed to an exechost_periodic hook.
 * @param[in]	vns_list - list of vnodes and their attributes/resources
 * 				passed to various hooks.
 * @param[in]	vns_list_fail - list of failed vnodes and their
 *				attributes/resources passed to various hooks.
 * @param[in]	failed_mom_list - list of parent moms that have been
 *				seen as down.
 * @param[in]	succeeded_mom_list - list of parent moms that have been
 *				seend as healthy.
 * @param[in]	pid - value to pbs.event().pid in an execjob_attach hook.
 *
 */
typedef struct	hook_input_param {
	void		*rq_job;
	void		*rq_manage;
	void		*rq_modifyvnode;
	void		*rq_move;
	void		*rq_prov;
	void		*rq_run;
	char		*progname;
	pbs_list_head	*argv_list;
	char		*env;
	pbs_list_head	*jobs_list;
	pbs_list_head	*vns_list;
	pbs_list_head	*resv_list;
	pbs_list_head	*vns_list_fail;
	pbs_list_head	*failed_mom_list;
	pbs_list_head	*succeeded_mom_list;
	pid_t		pid;
} hook_input_param_t;

/**
 *
 * @brief
 * 	The hook_output_param_t structure contains the output request
 * 	parameters to be filled in by pbs_python_event_to_request() function.
 *
 * @param[out]	rq_job - resultant struct rq_quejob batch request values.
 * @param[out]	rq_manage - resultant struct rq_manage batch request values.
 * @param[out]	rq_move - resultant struct rq_move batch request values.
 * @param[out]	rq_prov - resultant struct prov_vnode_info values.
 * @param[out]	rq_run - resultant struct rq_runjob batch request values.
 * @param[out]	progname - resultant value to pbs.event().progname
 * 			   after executing execjob_launch hook.
 * @param[out]	argv_list - resultant pbs.event().argv value after
 * 			   executing execjob_launch hook.
 * @param[out]	env - resultant value to pbs.event().env after executing
 * 			execjob_launch hook.
 * @param[out]	jobs_list - list of modifications done to jobs after
 * 			executing exechost_periodic hook.
 * @param[out]	vns_list - list of modifications done to vnodes
 * 			after executing various hooks.
 * @param[out]	vns_list_fail - list of modifications done to failed
 * 			vnodes after executing various hooks.
 *
 */
typedef struct	hook_output_param {
	void		*rq_job;
	void		*rq_manage;
	void		*rq_move;
	void		*rq_prov;
	void		*rq_run;
	char		**progname;
	pbs_list_head	*argv_list;
	char		**env;
	pbs_list_head	*jobs_list;
	pbs_list_head	*vns_list;
	pbs_list_head	*resv_list;
	pbs_list_head	*vns_list_fail;
} hook_output_param_t;

/* global constants */

/* this is a pointer to interp data -> pbs_python_daemon_name.
 * Since some of the routines could be shared by all three daemons, this saves
 * passing the struct python_interpreter_data all over the place just get the
 * daemon name
 */

extern char *pbs_python_daemon_name;  /* pbs_python_external.c */


/* -- BEGIN pbs_python_external.c implementations -- */
extern int pbs_python_ext_start_interpreter(
	struct python_interpreter_data *interp_data);
extern void pbs_python_ext_shutdown_interpreter(
	struct python_interpreter_data *interp_data);

extern int  pbs_python_load_python_types(
	struct python_interpreter_data *interp_data);
extern void pbs_python_unload_python_types(
	struct python_interpreter_data *interp_data);

extern void * pbs_python_ext_namespace_init(
	struct python_interpreter_data *interp_data);

extern int pbs_python_check_and_compile_script(
	struct python_interpreter_data *interp_data,
	struct python_script *py_script);

extern int  pbs_python_run_code_in_namespace(
	struct python_interpreter_data *interp_data,
	struct python_script *script_file,
	int *exit_code);

extern void pbs_python_ext_free_python_script(
	struct python_script *py_script);
extern int  pbs_python_ext_alloc_python_script(
	const char *script_path,
	struct python_script **py_script);

extern void pbs_python_ext_quick_start_interpreter(void);
extern void pbs_python_ext_quick_shutdown_interpreter(void);
extern int set_py_progname(void);
extern int get_py_progname(char **);


/* -- END pbs_python_external.c implementations -- */


/* -- BEGIN PBS Server/Python implementations -- */


/* For the symbolic constants below, cross-reference src/modules files */

#define PY_ATTRIBUTES		"attributes" /* list of valid PBS attributes */
#define PY_ATTRIBUTES_READONLY	"attributes_readonly"
/* valid PBS attributes not */
/* settable in a hook script */
#define PY_ATTRIBUTES_HOOK_SET	"_attributes_hook_set"
/* attributes that got set in */
/* a hook script */
#define PY_READONLY_FLAG	"_readonly"	/* an object is read-only */
#define PY_RERUNJOB_FLAG	"_rerun"	/* flag some job to rerun */
#define PY_DELETEJOB_FLAG	"_delete"	/* flag some job to be deleted*/

/* List of attributes appearing in a Python job, resv, server, queue,	*/
/* resource, and other PBS-related objects,  that are only defined in	*/
/* Python but not in PBS. 	 					*/
/* This means the attributes are not settable inside a hook script.	*/
#define PY_PYTHON_DEFINED_ATTRIBUTES "id resvid _name _has_value"

/* special event object attributes - in modules/pbs/v1.1 _svr_types.py */
#define PY_EVENT_TYPE		"type"
#define PY_EVENT_HOOK_NAME	"hook_name"
#define PY_EVENT_HOOK_TYPE	"hook_type"
#define PY_EVENT_REQUESTOR	"requestor"
#define PY_EVENT_REQUESTOR_HOST	"requestor_host"
#define PY_EVENT_PARAM		"_param"
#define PY_EVENT_FREQ		"freq"

/* The event parameter keys */
#define PY_EVENT_PARAM_JOB	"job"
#define PY_EVENT_PARAM_JOB_O	"job_o"
#define PY_EVENT_PARAM_RESV	"resv"
#define PY_EVENT_PARAM_RESV_O "resv_o"
#define PY_EVENT_PARAM_SRC_QUEUE "src_queue"
#define PY_EVENT_PARAM_VNODE     "vnode"
#define PY_EVENT_PARAM_VNODE_O    "vnode_o"
#define PY_EVENT_PARAM_VNODELIST "vnode_list"
#define PY_EVENT_PARAM_VNODELIST_FAIL "vnode_list_fail"
#define PY_EVENT_PARAM_JOBLIST "job_list"
#define PY_EVENT_PARAM_RESVLIST "resv_list"
#define PY_EVENT_PARAM_AOE	 "aoe"
#define PY_EVENT_PARAM_PROGNAME "progname"
#define PY_EVENT_PARAM_ARGLIST "argv"
#define PY_EVENT_PARAM_ENV	"env"
#define PY_EVENT_PARAM_PID	"pid"
#define PY_EVENT_PARAM_MANAGEMENT	"management"

/* special job object attributes */
#define PY_JOB_FAILED_MOM_LIST	"failed_mom_list"
#define PY_JOB_SUCCEEDED_MOM_LIST	"succeeded_mom_list"


/* special resource object attributes - in modules/pbs/v1.1/_base_types.py */

#define PY_RESOURCE		"resc"
#define PY_RESOURCE_NAME	"_name"
#define PY_RESOURCE_HAS_VALUE	"_has_value"
#define PY_RESOURCE_GENERIC_VALUE "<generic resource>"


/* descriptor-related symbols - in modules/pbs/v1.1/_base_types.py */
#define PY_DESCRIPTOR_NAME		"_name"
#define PY_DESCRIPTOR_VALUE		"_value"
#define PY_DESCRIPTOR_VALUE_TYPE	"_value_type"
#define PY_DESCRIPTOR_CLASS_NAME	"_class_name"
#define PY_DESCRIPTOR_IS_RESOURCE	"_is_resource"
#define PY_DESCRIPTOR_RESC_ATTRIBUTE	"_resc_attribute"

/* optional value attrib of a pbs.hold_types instance */
#define PY_OPVAL			"opval"
#define PY_DELVAL			"delval"

/* refers to the __sub__ method of pbs.hold_types */
#define PY_OPVAL_SUB			"__sub__"

/* class-related - in modules/pbs/v1.1/_base_types.py */
#define PY_CLASS_DERIVED_TYPES		"_derived_types"

/* PBS Python types - in modules/pbs/v1.1 files */

#define	PY_TYPE_ATTR_DESCRIPTOR		"attr_descriptor"
#define	PY_TYPE_GENERIC			"generic_type"
#define	PY_TYPE_SIZE			"size"
#define	PY_TYPE_TIME			"generic_time"
#define	PY_TYPE_ACL			"generic_acl"
#define	PY_TYPE_BOOL			"pbs_bool"
#define PY_TYPE_JOB		   	"job"
#define PY_TYPE_QUEUE		     	"queue"
#define PY_TYPE_SERVER		     	"server"
#define PY_TYPE_RESV		    	"resv"
#define PY_TYPE_VNODE			"vnode"
#define PY_TYPE_EVENT		    	"event"
#define PY_TYPE_RESOURCE		"pbs_resource"
#define PY_TYPE_LIST			"pbs_list"
#define PY_TYPE_INT			"pbs_int"
#define PY_TYPE_STR			"pbs_str"
#define PY_TYPE_FLOAT			"pbs_float"
#define PY_TYPE_FLOAT2			"float"
#define PY_TYPE_ENTITY			"pbs_entity"
#define PY_TYPE_ENV			"pbs_env"
#define PY_TYPE_MANAGEMENT	"management"
#define PY_TYPE_SERVER_ATTRIBUTE	"server_attribute"

/* PBS Python Exception errors - in modules/pbs/v1.1 files */
#define	PY_ERROR_EVENT_INCOMPATIBLE 	"EventIncompatibleError"
#define	PY_ERROR_EVENT_UNSET_ATTRIBUTE 	"UnsetAttributeNameError"
#define	PY_ERROR_BAD_ATTRIBUTE_VALUE_TYPE "BadAttributeValueTypeError"
#define	PY_ERROR_BAD_ATTRIBUTE_VALUE 	"BadAttributeValueError"
#define	PY_ERROR_UNSET_RESOURCE 	"UnsetResourceNameError"
#define	PY_ERROR_BAD_RESOURCE_VALUE_TYPE "BadResourceValueTypeError"
#define	PY_ERROR_BAD_RESOURCE_VALUE 	"BadResourceValueError"

/* Special values */

/* Value of an unset Job_Name attribute */
#define JOB_NAME_UNSET_VALUE    "none"
#define WALLTIME_RESC		"walltime"  /* correlates with resv_duration */

/* Determines if attribute being set in C or in a Python script */
#define PY_MODE 1
#define C_MODE  2

/* Special Method names */
#define PY_GETRESV_METHOD	"get_resv"
#define PY_GETVNODE_METHOD	"get_vnode"
#define PY_ITER_NEXTFUNC_METHOD "iter_nextfunc"
#define PY_SIZE_TO_KBYTES_METHOD "size_to_kbytes"
#define PY_MARK_VNODE_SET_METHOD "mark_vnode_set"
#define PY_LOAD_RESOURCE_VALUE_METHOD "load_resource_value"
#define PY_RESOURCE_STR_VALUE_METHOD "resource_str_value"
#define PY_SET_C_MODE_METHOD 	"set_c_mode"
#define PY_SET_PYTHON_MODE_METHOD "set_python_mode"
#define PY_STR_TO_VNODE_STATE_METHOD "str_to_vnode_state"
#define PY_STR_TO_VNODE_NTYPE_METHOD "str_to_vnode_ntype"
#define PY_STR_TO_VNODE_SHARING_METHOD "str_to_vnode_sharing"
#define PY_VNODE_STATE_TO_STR_METHOD "vnode_state_to_str"
#define PY_VNODE_SHARING_TO_STR_METHOD "vnode_sharing_to_str"
#define PY_VNODE_NTYPE_TO_STR_METHOD "vnode_ntype_to_str"
#define PY_GET_PYTHON_DAEMON_NAME_METHOD "get_python_daemon_name"
#define PY_GET_PBS_SERVER_NAME_METHOD "get_pbs_server_name"
#define PY_GET_LOCAL_HOST_NAME_METHOD "get_local_host_name"
#define PY_GET_PBS_CONF_METHOD "get_pbs_conf"
#define PY_TYPE_PBS_ITER	"pbs_iter"
#define ITER_QUEUES		"queues"
#define ITER_JOBS		"jobs"
#define ITER_RESERVATIONS	"resvs"
#define ITER_VNODES           "vnodes"
#define PY_LOGJOBMSG_METHOD	"logjobmsg"
#define PY_REBOOT_HOST_METHOD	"reboot"
#define PY_SCHEDULER_RESTART_CYCLE_METHOD	"scheduler_restart_cycle"
#define PY_SET_PBS_STATOBJ_METHOD	"set_pbs_statobj"
#define PY_GET_SERVER_STATIC_METHOD	"get_server_static"
#define PY_GET_JOB_STATIC_METHOD	"get_job_static"
#define PY_GET_RESV_STATIC_METHOD	"get_resv_static"
#define PY_GET_VNODE_STATIC_METHOD	"get_vnode_static"
#define PY_GET_QUEUE_STATIC_METHOD	"get_queue_static"
#define PY_GET_SERVER_DATA_FP_METHOD	"get_server_data_fp"
#define PY_GET_SERVER_DATA_FILE_METHOD	"get_server_data_file"
#define PY_USE_STATIC_DATA_METHOD	"use_static_data"

/* Event parameter names */
#define	PBS_OBJ			"pbs"
#define	PBS_REBOOT_OBJECT	"reboot"
#define	PBS_REBOOT_CMD_OBJECT	"reboot_cmd"
#define	GET_NODE_NAME_FUNC	"get_local_nodename()"
#define	EVENT_OBJECT		"pbs.event()"
#define	EVENT_JOB_OBJECT	EVENT_OBJECT ".job"
#define	EVENT_JOB_O_OBJECT	EVENT_OBJECT ".job_o"
#define	EVENT_RESV_OBJECT	EVENT_OBJECT ".resv"
#define	EVENT_SRC_QUEUE_OBJECT	EVENT_OBJECT ".src_queue"
#define	EVENT_VNODE_OBJECT	EVENT_OBJECT ".vnode"
#define	EVENT_VNODE_O_OBJECT	EVENT_OBJECT ".vnode_o"
#define	EVENT_VNODELIST_OBJECT	EVENT_OBJECT ".vnode_list"
#define	EVENT_VNODELIST_FAIL_OBJECT	EVENT_OBJECT ".vnode_list_fail"
#define	EVENT_JOBLIST_OBJECT	EVENT_OBJECT ".job_list"
#define	EVENT_AOE_OBJECT	EVENT_OBJECT ".aoe"
#define	EVENT_ACCEPT_OBJECT	EVENT_OBJECT ".accept"
#define	EVENT_REJECT_OBJECT	EVENT_OBJECT ".reject"
#define	EVENT_REJECT_MSG_OBJECT	EVENT_OBJECT ".reject_msg"
#define	EVENT_HOOK_EUSER	EVENT_OBJECT ".hook_euser"
#define	EVENT_JOB_RERUNFLAG_OBJECT   EVENT_OBJECT ".job._rerun"
#define	EVENT_JOB_DELETEFLAG_OBJECT  EVENT_OBJECT ".job._delete"
#define	EVENT_PROGNAME_OBJECT  EVENT_OBJECT ".progname"
#define	EVENT_ARGV_OBJECT  EVENT_OBJECT ".argv"
#define	EVENT_ENV_OBJECT  EVENT_OBJECT ".env"
#define	EVENT_PID_OBJECT  EVENT_OBJECT ".pid"
#define	EVENT_MANAGEMENT_OBJECT  EVENT_OBJECT ".management"

/* Special Job parameters */
#define	JOB_FAILED_MOM_LIST_OBJECT	EVENT_JOB_OBJECT "." PY_JOB_FAILED_MOM_LIST
#define	JOB_SUCCEEDED_MOM_LIST_OBJECT	EVENT_JOB_OBJECT "." PY_JOB_SUCCEEDED_MOM_LIST

/* Server parameter names */
#define	SERVER_OBJECT		"pbs.server()"
#define	SERVER_JOB_OBJECT	SERVER_OBJECT ".job"
#define	SERVER_QUEUE_OBJECT	SERVER_OBJECT ".queue"
#define	SERVER_RESV_OBJECT	SERVER_OBJECT ".resv"
#define	SERVER_VNODE_OBJECT	SERVER_OBJECT ".vnode"

extern void pbs_python_set_mode(int mode);

extern int  pbs_python_event_mark_readonly(void);

extern int  pbs_python_event_set(
	unsigned int hook_event,
	char *req_user,
	char *req_host,
	hook_input_param_t *req_params,
	char *perf_label);

extern void pbs_python_event_unset(void);

extern int  pbs_python_event_to_request(unsigned int hook_event,
	hook_output_param_t *req_params, char *perf_label, char *perf_action);

extern int  pbs_python_event_set_attrval(char *name, char *value);

extern char *
pbs_python_event_get_attrval(char *name);

extern void *pbs_python_event_get(void);

extern void
pbs_python_event_accept(void);

extern void
pbs_python_event_reject(char *msg);

extern char *
pbs_python_event_get_reject_msg(void);

extern int
pbs_python_event_get_accept_flag(void);

extern void
pbs_python_reboot_host(char *cmd);

extern void
pbs_python_scheduler_restart_cycle(void);

extern void
pbs_python_no_scheduler_restart_cycle(void);

int
pbs_python_get_scheduler_restart_cycle_flag(void);

extern char *
pbs_python_get_reboot_host_cmd(void);

extern int
pbs_python_get_reboot_host_flag(void);

extern void
pbs_python_event_param_mod_allow(void);

extern void
pbs_python_event_param_mod_disallow(void);

extern int
pbs_python_event_param_get_mod_flag(void);

extern void
pbs_python_set_interrupt(void);

extern char *
pbs_python_event_job_getval_hookset(char *attrib_name, char *opval,
	int opval_len, char *delval, int delval_len);

extern char *
pbs_python_event_job_getval(char *attrib_name);

extern char *
pbs_python_event_jobresc_getval_hookset(char *attrib_name,  char *resc_name);

extern int
pbs_python_event_jobresc_clear_hookset(char *attrib_name);

extern char *
pbs_python_event_jobresc_getval(char *attrib_name, char *resc_name);

extern int
pbs_python_has_vnode_set(void);

extern void
pbs_python_do_vnode_set(void);

extern void
pbs_python_set_hook_debug_input_fp(FILE *);

extern FILE *
pbs_python_get_hook_debug_input_fp(void);

extern void
pbs_python_set_hook_debug_input_file(char *);

extern  char *
pbs_python_get_hook_debug_input_file(void);

extern void
pbs_python_set_hook_debug_output_file(char *);

extern  char *
pbs_python_get_hook_debug_output_file(void);

extern void
pbs_python_set_hook_debug_output_fp(FILE *fp);

FILE *
pbs_python_get_hook_debug_output_fp(void);

extern void
pbs_python_set_hook_debug_data_fp(FILE *);

extern FILE *
pbs_python_get_hook_debug_data_fp(void);

extern void
pbs_python_set_hook_debug_data_file(char *);

extern  char *
pbs_python_get_hook_debug_data_file(void);

extern void
pbs_python_set_use_static_data_value(int);

extern void
pbs_python_set_server_info(pbs_list_head *);

extern void
pbs_python_unset_server_info(void);

extern void
pbs_python_set_server_jobs_info(pbs_list_head *, pbs_list_head *);

extern void
pbs_python_unset_server_jobs_info(void);

extern void
pbs_python_set_server_queues_info(pbs_list_head *, pbs_list_head *);

extern void
pbs_python_unset_server_queues_info(void);

extern void
pbs_python_set_server_resvs_info(pbs_list_head *, pbs_list_head *);

extern void
pbs_python_unset_server_resvs_info(void);

extern void
pbs_python_set_server_vnodes_info(pbs_list_head *, pbs_list_head *);

extern void
pbs_python_unset_server_vnodes_info(void);

extern int
varlist_same(char *varl1, char *varl2);

extern int
pbs_python_set_os_environ(char *env_var, char *env_val);

extern int
pbs_python_set_pbs_hook_config_filename(char *conf_file);

extern void
hook_input_param_init(hook_input_param_t *hook_input);

extern void
hook_output_param_init(hook_output_param_t *hook_output);

/* -- END PBS Server/Python implementations -- */

#ifdef __cplusplus
}
#endif

#endif /* _PBS_PYTHON_DEF */
