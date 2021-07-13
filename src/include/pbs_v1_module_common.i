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

#if !defined(PBS_v1_COMMON_I_INCLUDED)
#define PBS_v1_COMMON_I_INCLUDED 1

time_t time_now = 0;

/*
 * the names of the Server:
 *    pbs_server_name - from PBS_SERVER_HOST_NAME
 *	  server_name - from PBS_SERVER
 *	  server_host - Set as follows:
 *	  		1. FQDN of pbs_server_name if set
 *	  		2. FQDN of server_name if set
 *	  		3. Call gethostname()
 *
 * The following is an excerpt from the EDD for SPID 4534 that explains
 * how PBS_SERVER_HOST_NAME is used:
 *
 * I.1.2.3	Synopsis:
 * Add new optional entry in PBS Configuration whose value is the fully
 * qualified domain name (FQDN) of the host on which the PBS Server is
 * running.
 *	I.1.2.3.1	This name is used by clients to contact the Server.
 *	I.1.2.3.2	If PBS Failover is configured (PBS_PRIMARY and
 *			PBS_SECONDARY in the PBS Configuration), this symbol
 *			and its value will be ignored and the values of
 *			PBS_PRIMARY and PBS_SECONDARY will be use as per
 *			sectionI.1.1.1.
 *	I.1.2.3.3	When  PBS failover is not configured and
 *			PBS_SERVER_HOST_NAME is specified, if the server_name
 *			is not specified by the client or is specified and
 *			matches the value of PBS_SERVER, then the value of
 *			PBS_SERVER_HOST_NAME is used as the name of the Server
 *			to contact.
 *	I.1.2.3.4	Note: When PBS_SERVER_HOST_NAME is not specified,
 *			the current behavior for determining the name of the
 *			Server to contact will still apply.
 *	I.1.2.3.5	The value of the configuration variable should be a
 *			fully qualified host name to avoid the possibility of
 *			host name collisions (e.g. master.foo.domain.name and
 *			master.bar.domain.name).
 */
char *pbs_server_name = NULL;
char server_name[PBS_MAXSERVERNAME+1] = "";  /* host_name[:service|port] */
char server_host[PBS_MAXHOSTNAME+1] = "";  /* host_name of this svr */
struct server server = {{0}};  /* the server structure */
struct pbsnode **pbsndlist = NULL;  /* array of ptr to nodes */
int svr_totnodes = 0;  /* number of nodes (hosts) */
struct python_interpreter_data  svr_interp_data;
int svr_delay_entry = 0;
pbs_list_head svr_queues;  /* list of queues */
pbs_list_head svr_alljobs;  /* list of all jobs in server */
pbs_list_head svr_allresvs;  /* all reservations in server */
pbs_list_head svr_queues;
pbs_list_head svr_alljobs;
pbs_list_head svr_allresvs;
pbs_list_head svr_allhooks;
pbs_list_head svr_queuejob_hooks;
pbs_list_head svr_modifyjob_hooks;
pbs_list_head svr_resvsub_hooks;
pbs_list_head svr_modifyresv_hooks;
pbs_list_head svr_movejob_hooks;
pbs_list_head svr_runjob_hooks;
pbs_list_head svr_management_hooks;
pbs_list_head svr_modifyvnode_hooks;
pbs_list_head svr_provision_hooks;
pbs_list_head svr_periodic_hooks;
pbs_list_head svr_resv_confirm_hooks;
pbs_list_head svr_resv_begin_hooks;
pbs_list_head svr_resv_end_hooks;
pbs_list_head svr_execjob_begin_hooks;
pbs_list_head svr_execjob_prologue_hooks;
pbs_list_head svr_execjob_epilogue_hooks;
pbs_list_head svr_execjob_preterm_hooks;
pbs_list_head svr_execjob_launch_hooks;
pbs_list_head svr_execjob_end_hooks;
pbs_list_head svr_exechost_periodic_hooks;
pbs_list_head svr_exechost_startup_hooks;
pbs_list_head svr_execjob_attach_hooks;
pbs_list_head svr_execjob_resize_hooks;
pbs_list_head svr_execjob_abort_hooks;
pbs_list_head svr_execjob_postsuspend_hooks;
pbs_list_head svr_execjob_preresume_hooks;

pbs_list_head task_list_immed;
pbs_list_head task_list_interleave;
pbs_list_head task_list_timed;
pbs_list_head task_list_event;

char *path_hooks = NULL;
char *path_hooks_workdir = NULL;
char *path_rescdef = NULL;
char *resc_in_err = NULL;

void *job_attr_idx = NULL;
void *resv_attr_idx = NULL;
void *node_attr_idx = NULL;
void *que_attr_idx = NULL;
void *svr_attr_idx = NULL;
void *sched_attr_idx = NULL;

#if defined(PBS_V1_COMMON_MODULE_DEFINE_STUB_FUNCS)
/*
 *	The following are a set of unused stub functions needed so that pbs_python
 *	and the loadable pbs_v1 Python module can be linked to svr_attr_def.o,
 *	job_attr_def.o, node_attr_def.o, queue_attr_def.o, resv_attr_def.o
 *
 */
#ifndef PBS_PYTHON
PyObject *
PyInit__pbs_ifl(void) {
	return NULL;
}
#endif

int
node_state(attribute *new, void *pnode, int actmode) {
	return 0;
}

int
set_resources_min_max(attribute *old, attribute *new, enum batch_op op) {
	return (0);
}

void
set_scheduler_flag(int flag, pbs_sched *psched) {
	return;
}

job	*
find_job(char *jobid) {
	return NULL;
}

resc_resv *
find_resv(char *resvid) {
	return NULL;
}

pbs_queue *
find_queuebyname(char *qname) {
	return NULL;
}

struct pbsnode *find_nodebyname(char *nname) {
	return NULL;
}

void
write_node_state(void) {
	return;
}

void
mgr_log_attr(char *msg, struct svrattrl *plist, int logclass,
		char *objname, char *hookname) {
	return;
}

int
mgr_set_attr(attribute *pattr, void *pidx, attribute_def *pdef, int limit,
		svrattrl *plist, int privil, int *bad, void *parent, int mode) {
	return (0);
}

int
svr_chk_history_conf(void) {
	return (0);
}

int
save_nodes_db(int flag, void *pmom) {
	return (0);
}

void
update_state_ct(attribute *pattr, int *ct_array, attribute_def *attr_def) {
	return;
}

void
update_license_ct() {
	return;
}

int
is_job_array(char *jobid) {
	return (0);
}

job *
find_arrayparent(char *subjobid) {
	return NULL;
}

int
ck_chkpnt(attribute *pattr, void *pobject, int mode) {
	return (0);
}

int
cred_name_okay(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
poke_scheduler(attribute *attr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
action_sched_priv(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
action_sched_log(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
action_sched_iteration(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
action_sched_user(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
action_queue_partition(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
action_sched_preempt_order(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
action_sched_preempt_common(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
action_reserve_retry_time(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
action_reserve_retry_init(attribute *pattr, void *pobj, int actmode) {
        return PBSE_NONE;
}

int
set_rpp_retry(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
set_rpp_highwater(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
is_valid_resource(attribute *pattr, void *pobject, int actmode) {

	return PBSE_NONE;
}

int
deflt_chunk_action(attribute *pattr, void *pobj, int mode) {

	return 0;
}

int
action_svr_iteration(attribute *pattr, void *pobj, int mode) {
	return 0;
}

int
set_license_location(attribute *pattr, void *pobject, int actmode) {
	return (PBSE_NONE);
}

void
unset_license_location(void) {
	return;
}

int
set_node_fail_requeue(attribute *pattr, void *pobject, int actmode) {
	return (PBSE_NONE);
}

void
unset_node_fail_requeue(void) {
	return;
}

int
action_node_partition(attribute *pattr, void *pobject, int actmode) {
	return (PBSE_NONE);
}

int
set_license_min(attribute *pattr, void *pobject, int actmode) {
	return (PBSE_NONE);
}

void
unset_license_min(void) {
	return;
}

int
set_license_max(attribute *pattr, void *pobject, int actmode) {
	return (PBSE_NONE);
}

void
unset_license_max(void) {
	return;
}

int
set_license_linger(attribute *pattr, void *pobject, int actmode) {

	return (PBSE_NONE);
}

void
unset_license_linger(void) {
	return;
}

void
unset_job_history_enable(void) {
	return;
}

int
set_job_history_enable(attribute *pattr, void *pobject, int actmode) {
	return (PBSE_NONE);
}

int
set_job_history_duration(attribute *pattr, void *pobject, int actmode) {

	return (PBSE_NONE);
}

void
unset_job_history_duration(void) {
	return;
}

int
set_max_job_sequence_id(attribute *pattr, void *pobject, int actmode) {
	return (PBSE_NONE);
}

void
unset_max_job_sequence_id(void) {
	return;
}

int
eligibletime_action(attribute *pattr, void *pobject, int actmode) {
	return 0;
}

int
decode_formula(attribute *patr, char *name, char *rescn, char *val) {
	return PBSE_NONE;
}

int
action_entlim_chk(attribute *pattr, void *pobject, int actmode) {
	return PBSE_NONE;
}

int
action_entlim_ct(attribute *pattr, void *pobject, int actmode) {
	return PBSE_NONE;
}

int
action_entlim_res(attribute *pattr, void *pobject, int actmode) {
	return PBSE_NONE;
}

int
check_no_entlim(attribute *pattr, void *pobject, int actmode) {
	return 0;
}

int
default_queue_chk(attribute *pattr, void *pobj, int actmode) {
	return (PBSE_NONE);
}

void
set_vnode_state(struct pbsnode *pnode, unsigned long state_bits,
		 enum vnode_state_op type) {
	return;
}

int
ctcpus(char *buf, int *hascpp) {
	return 0;
}

int
validate_nodespec(char *str) {
	return 0;
}

int
check_que_enable(attribute *pattr, void *pque, int mode) {
	return (0);
}

int
set_queue_type(attribute *pattr, void *pque, int mode) {
	return (0);
}

int
manager_oper_chk(attribute *pattr, void *pobject, int actmode) {
	return (0);
}

int
node_comment(attribute *pattr, void *pobj, int act) {
	return 0;
}

int
node_prov_enable_action(attribute *new, void *pobj, int act) {
	return PBSE_NONE;
}

int
set_log_events(attribute *new, void *pobj, int act) {
	return PBSE_NONE;
}

int
node_current_aoe_action(attribute *new, void *pobj, int act) {
	return PBSE_NONE;
}

int
action_sched_host(attribute *new, void *pobj, int act)
{
	return PBSE_NONE;
}

int
action_throughput_mode(attribute *new, void *pobj, int act)
{
	return PBSE_NONE;
}

int
action_job_run_wait(attribute *new, void *pobj, int act)
{
	return PBSE_NONE;
}

int
action_opt_bf_fuzzy(attribute *new, void *pobj, int act)
{
	return PBSE_NONE;
}

int
action_sched_partition(attribute *new, void *pobj, int act)
{
	return PBSE_NONE;
}

int
action_max_run_subjobs(attribute *pattr, void *pobject, int actmode)
{
	return 0;
}

int
decode_rcost(attribute *patr, char *name, char *rescn, char *val) {
	return 0;
}

int
encode_rcost(const attribute *attr, pbs_list_head *phead, char *atname,
		char *rsname, int mode, svrattrl **rtnl) {
	return (1);
}

int
set_rcost(attribute *old, attribute *new, enum batch_op op) {
	return (0);
}

void
free_rcost(attribute *pattr) {
	return;
}

int
svr_max_conc_prov_action(attribute *new, void *pobj, int act) {
	return 0;
}

int
action_backfill_depth(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
action_jobscript_max_size(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
action_check_res_to_release(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
queuestart_action(attribute *pattr, void *pobject, int actmode) {
	return 0;
}

int
set_cred_renew_enable(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
set_cred_renew_period(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
set_cred_renew_cache_period(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
encode_svrstate(const attribute *pattr, pbs_list_head *phead, char *atname,
		char *rsname, int mode, svrattrl **rtnl) {
	return (1);
}

int
comp_chkpnt(attribute *attr, attribute *with) {
	return 0;
}

int
decode_depend(attribute *patr, char *name, char *rescn, char *val) {
	return (0);
}

int
encode_depend(const attribute *attr, pbs_list_head *phead, char *atname,
		char *rsname, int mode, svrattrl **rtnl) {
	return 0;
}

int
set_depend(attribute *attr, attribute *new, enum batch_op op) {
	return (0);
}

int
comp_depend(attribute *attr, attribute *with) {
	return (-1);
}

void
free_depend(attribute *attr) {
	return;
}

int
depend_on_que(attribute *pattr, void *pobj, int mode) {
	return 0;
}

int
job_set_wait(attribute *pattr, void *pjob, int mode) {
	return (0);
}

int
alter_eligibletime(attribute *pattr, void *pobject, int actmode) {
	return PBSE_NONE;
}

int
keepfiles_action(attribute *pattr, void *pobject, int actmode) {
    return PBSE_NONE;
}

int
removefiles_action(attribute *pattr, void *pobject, int actmode) {
    return PBSE_NONE;
}

int
action_est_start_time_freq(attribute *pattr, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
setup_arrayjob_attrs(attribute *pattr, void *pobj, int mode) {
	return (PBSE_NONE);
}

int
fixup_arrayindicies(attribute *pattr, void *pobj, int mode) {
	return (PBSE_NONE);
}

int
decode_Mom_list(attribute *patr, char *name, char *rescn, char *val) {
	return (0);
}

int
node_queue_action(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
set_node_host_name(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
set_node_mom_port(attribute *pattr, void *pobj, int actmode) {
	return 0;
}

int
node_np_action(attribute *new, void *pobj, int actmode) {
	return PBSE_NONE;
}

int
node_pcpu_action(attribute *new, void *pobj, int actmode) {
	return (0);
}

char*
find_aoe_from_request(resc_resv *presv) {
	return NULL;
}

int
force_qsub_daemons_update_action(attribute *pattr, void *pobject,
	int actmode) {
	return (PBSE_NONE);
}

int
set_node_topology(attribute *pattr, void *pobject, int actmode) {

	return (PBSE_NONE);
}

int
chk_vnode_pool(attribute *pattr, void *pobject, int actmode) {
	return (PBSE_NONE);
}

int
validate_job_formula(attribute *pattr, void *pobject, int actmode) {
	return (PBSE_NONE);
}
#endif /* defined(PBS_V1_COMMON_MODULE_DEFINE_STUB_FUNCS) */

#endif /* defined(PBS_v1_COMMON_I_INCLUDED) */
