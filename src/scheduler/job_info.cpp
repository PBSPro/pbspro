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

/**
 * @file    job_info.c
 *
 * @brief
 * 		job_info.c - This file contains functions related to job_info structure.
 *
 * Functions included are:
 * 	query_jobs()
 * 	query_job()
 * 	new_job_info()
 * 	free_job_info()
 * 	set_job_state()
 * 	update_job_attr()
 * 	send_job_updates()
 * 	send_attr_updates()
 * 	unset_job_attr()
 * 	update_job_comment()
 * 	update_jobs_cant_run()
 * 	translate_fail_code()
 * 	dup_job_info()
 * 	preempt_job_set_filter()
 * 	get_preemption_order()
 * 	preempt_job()
 * 	find_and_preempt_jobs()
 * 	find_jobs_to_preempt()
 * 	select_index_to_preempt()
 * 	preempt_level()
 * 	set_preempt_prio()
 * 	create_subjob_name()
 * 	create_subjob_from_array()
 * 	update_array_on_run()
 * 	is_job_array()
 * 	modify_job_array_for_qrun()
 * 	queue_subjob()
 * 	formula_evaluate()
 * 	make_eligible()
 * 	make_ineligible()
 * 	update_accruetype()
 * 	getaoename()
 * 	update_estimated_attrs()
 * 	check_preempt_targets_for_none()
 * 	is_finished_job()
 * 	preemption_similarity()
 * 	geteoename()
 *
 */

#include <pbs_config.h>

#ifdef PYTHON
#include <Python.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <math.h>
#include <pbs_ifl.h>
#include <log.h>
#include <libutil.h>
#include <pbs_share.h>
#include <pbs_internal.h>
#include <pbs_error.h>
#include "queue_info.h"
#include "job_info.h"
#include "resv_info.h"
#include "constant.h"
#include "misc.h"
#include "config.h"
#include "globals.h"
#include "fairshare.h"
#include "node_info.h"
#include "check.h"
#include "sort.h"
#include "fifo.h"
#include "range.h"
#include "resource_resv.h"
#include "limits_if.h"
#include "simulate.h"
#include "resource.h"
#include "server_info.h"
#include "attribute.h"
#include "multi_threading.h"
#include "libpbs.h"

#ifdef NAS
#include "site_code.h"
#include "site_queue.h"
#endif


extern char *pbse_to_txt(int err);

/**
 *	This table contains job comment and information messages that correspond
 *	to the sched_error_code enums in "constant.h".  The order of the strings in
 *	the table must match the numeric order of the sched_error_code enum values.
 *	The length of the resultant strings (including any arguments inserted
 *	via % formatting directives by translate_fail_code(), q.v.) must not
 *	exceed the dimensions of the schd_error elements.  See data_types.h.
 */
struct fc_translation_table {
	const char	*fc_comment;	/**< job comment string */
	const char	*fc_info;	/**< job error string */
};
static struct fc_translation_table fctt[] = {
	{
		"",
		""
	},
	{	/* SUCCESS */
		"",
		""
	},
	{	/* SCHD_ERROR */
		"Internal Scheduling Error",
		"A scheduling error has occurred"
	},
	{	/* NOT_QUEUED */
		"Job not in queued state",
		"Job is not in queued state"
	},
	{	/* QUEUE_NOT_STARTED */
		"Queue not started.",
		"Queue not started"
	},
	{	/* QUEUE_NOT_EXEC */
		"Queue not an execution queue.",
		"Queue not an execution queue"
	},
	{	/* QUEUE_JOB_LIMIT_REACHED */
		"Queue %s job limit has been reached.",
		"Queue %s job limit reached"
	},
	{	/* SERVER_JOB_LIMIT_REACHED */
		"Server job limit has been reached.",
		"Server job limit reached"
	},
	{	/* SERVER_USER_LIMIT_REACHED */
		"User has reached server running job limit.",
		"Server per-user job limit reached"
	},
	{	/* QUEUE_USER_LIMIT_REACHED */
		"User has reached queue %s running job limit.",
		"Queue %s per-user job limit reached"
	},
	{	/* SERVER_GROUP_LIMIT_REACHED */
		"Group has reached server running limit.",
		"Server per-group limit reached"
	},
	{	/* QUEUE_GROUP_LIMIT_REACHED */
		"Group has reached queue %s running limit.",
		"Queue %s per-group job limit reached"
	},
	{	/* DED_TIME */
		"Dedicated time conflict",
		"Dedicated Time"
	},
	{	/* CROSS_DED_TIME_BOUNDRY */
		"Job would cross dedicated time boundary",
		"Job would not finish before dedicated time"
	},
	{	/* NO_AVAILABLE_NODE */
		"",
		""
	},
	{	/* NOT_ENOUGH_NODES_AVAIL */
		"Not enough of the right type of nodes are available",
		"Not enough of the right type of nodes available"
	},
	{	/* BACKFILL_CONFLICT */
		"Job would interfere with a top job",
		"Job would interfere with a top job"
	},
	{	/* RESERVATION_INTERFERENCE */
		"Job would interfere with a confirmed reservation",
		"Job would interfere with a reservation"
	},
	{	/* PRIME_ONLY */
		"Job will run in primetime only",
		"Job only runs in primetime"
	},
	{	/* NONPRIME_ONLY */
		"Job will run in nonprimetime only",
		"Job only runs in nonprimetime"
	},
	{	/* CROSS_PRIME_BOUNDARY */
		"Job will cross into %s",
		"Job would cross into %s"
	},
	{	/* NODE_NONEXISTENT */
		"Specified %s does not exist: %s",
		"Specified %s does not exist: %s"
	},
	{	/* NO_NODE_RESOURCES */
		"No available resources on nodes",
		"No available resources on nodes"
	},
	{	/* CANT_PREEMPT_ENOUGH_WORK */
		"Can't preempt enough work to run job",
		"Can't preempt enough work to run job"
	},
	{	/* QUEUE_USER_RES_LIMIT_REACHED */
		"Queue %s per-user limit reached on resource %s",
		"Queue %s per-user limit reached on resource %s"
	},
	{	/* SERVER_USER_RES_LIMIT_REACHED */
		"Server per-user limit reached on resource %s",
		"Server per-user limit reached on resource %s"
	},
	{	/* QUEUE_GROUP_RES_LIMIT_REACHED */
		"Queue %s per-group limit reached on resource %s",
		"Queue %s per-group limit reached on resource %s"
	},
	{	/* SERVER_GROUP_RES_LIMIT_REACHED */
		"Server per-group limit reached on resource %s",
		"Server per-group limit reached on resource %s"
	},
	{	/* NO_FAIRSHARES */
		"Job has zero shares for fairshare",
		"Job has zero shares for fairshare"
	},
	{	/* INVALID_NODE_STATE */
		"Node is in an ineligible state: %s",
#ifdef NAS /* localmod 031 */
		"Node is in an ineligible state: %s: %s"
#else
		"Node is in an ineligible state: %s"
#endif /* localmod 031 */
	},
	{	/* INVALID_NODE_TYPE */
		"Node is of an ineligible type: %s",
		"Node is of an ineligible type: %s"
	},
	{	/* NODE_NOT_EXCL */
#ifdef NAS /* localmod 031 */
		"Nodes not available",
#else
		"%s is requesting an exclusive node and node is in use",
#endif /* localmod 031 */
		"%s is requesting an exclusive node and node is in use"
	},
	{	/* NODE_JOB_LIMIT_REACHED */
		"Node has reached job run limit",
		"Node has reached job run limit"
	},
	{	/* NODE_USER_LIMIT_REACHED */
		"Node has reached user run limit",
		"Node has reached user run limit"
	},
	{	/* NODE_GROUP_LIMIT_REACHED */
		"Node has reached group run limit",
		"Node has reached group run limit"
	},
	{	/* NODE_NO_MULT_JOBS */
		"Node can't satisfy a multi-node job",
		"Node can't satisfy a multi-node job"
	},
	{	/* NODE_UNLICENSED */
		"Node has no PBS license",
		"Node has no PBS license"
	},
	{	/* UNUSED37 */
		"",
		""
	},
	{	/* NO_SMALL_CPUSETS */
		"Max number of small cpusets has been reached",
		"Max number of small cpusets has been reached"
	},
	{	/* INSUFFICIENT_RESOURCE */
		"Insufficient amount of resource: %s %s",
		"Insufficient amount of resource: %s %s"
	},
	{	/* RESERVATION_CONFLICT */
		"Job would conflict with reservation or top job",
		"Job would conflict with reservation or top job"
	},
	{	/* NODE_PLACE_PACK */
		"Node ineligible because job requested pack placement and won't fit on node",
		"Node ineligible because job requested pack placement and won't fit on node"
	},
	{	/* NODE_RESV_ENABLE */
		"Node not eligible for advance reservation",
		"Node not eligible for advance reservation"
	},
	{	/* STRICT_ORDERING */
		"Job would break strict sorted order",
		"Job would break strict sorted order"
	},
	{	/* MAKE_ELIGIBLE */
		"",
		""
	},
	{	/* MAKE_INELIGIBLE */
		"",
		""
	},
	{	/* INSUFFICIENT_QUEUE_RESOURCE */
		"Insufficient amount of queue resource: %s %s",
		"Insufficient amount of queue resource: %s %s"
	},
	{	/* INSUFFICIENT_SERVER_RESOURCE */
		"Insufficient amount of server resource: %s %s",
		"Insufficient amount of server resource: %s %s"
	},
	{	/* QUEUE_BYGROUP_JOB_LIMIT_REACHED */
		"Queue %s job limit reached for group %s",
		"Queue %s job limit reached for group %s"
	},
	{	/* QUEUE_BYUSER_JOB_LIMIT_REACHED */
		"Queue %s job limit reached for user %s",
		"Queue %s job limit reached for user %s"
	},
	{	/* SERVER_BYGROUP_JOB_LIMIT_REACHED */
		"Server job limit reached for group %s",
		"Server job limit reached for group %s"
	},
	{	/* SERVER_BYUSER_JOB_LIMIT_REACHED */
		"Server job limit reached for user %s",
		"Server job limit reached for user %s"
	},
	{	/* SERVER_BYGROUP_RES_LIMIT_REACHED */
		"would exceed group %s's limit on resource %s in complex",
		"would exceed group %s's limit on resource %s in complex"
	},
	{	/* SERVER_BYUSER_RES_LIMIT_REACHED */
		"would exceed user %s's limit on resource %s in complex",
		"would exceed user %s's limit on resource %s in complex"
	},
	{	/* QUEUE_BYGROUP_RES_LIMIT_REACHED */
		"would exceed group %s's limit on resource %s in queue %s",
		"would exceed group %s's limit on resource %s in queue %s"
	},
	{	/* QUEUE_BYUSER_RES_LIMIT_REACHED */
		"would exceed user %s's limit on resource %s in queue %s",
		"would exceed user %s's limit on resource %s in queue %s"
	},
	{	/* QUEUE_RESOURCE_LIMIT_REACHED */
		"would exceed overall limit on resource %s in queue %s",
		"would exceed overall limit on resource %s in queue %s"
	},
	{	/* SERVER_RESOURCE_LIMIT_REACHED */
		"would exceed overall limit on resource %s in complex",
		"would exceed overall limit on resource %s in complex"
	},
	{	/* PROV_DISABLE_ON_SERVER */
		"Cannot provision, provisioning disabled on server",
		"Cannot provision, provisioning disabled on server"
	},
	{	/* PROV_DISABLE_ON_NODE */
		"Cannot provision, provisioning disabled on vnode",
		"Cannot provision, provisioning disabled on vnode"
	},
	{	/* AOE_NOT_AVALBL */
		"Cannot provision, requested AOE %s not available on vnode",
		"Cannot provision, requested AOE %s not available on vnode"
	},
	{	/* EOE_NOT_AVALBL */
		"Cannot provision, requested EOE %s not available on vnode",
		"Cannot provision, requested EOE %s not available on vnode"
	},
	{	/* PROV_BACKFILL_CONFLICT */
		"Provisioning for job would interfere with backfill job",
		"Provisioning for job would interfere with backfill job"
	},
	{	/* IS_MULTI_VNODE */
		"Cannot provision, host has multiple vnodes",
		"Cannot provision, host has multiple vnodes"
	},
	{	/* PROV_RESRESV_CONFLICT */
		"Provision conflict with existing job/reservation",
		"Provision conflict with existing job/reservation"
	},
	{	/* RUN_FAILURE */
		"PBS Error: %s",
		"Failed to run: %s (%s)"
	},
	{	/* SET_TOO_SMALL */
		"%s set %s has too few free resources",
		"%s set %s has too few free resources or is too small"
	},
	{	/* CANT_SPAN_PSET */
		"can't fit in the largest placement set, and can't span psets",
		"Can't fit in the largest placement set, and can't span placement sets"
	},
	{   /* NO_FREE_NODES */
		"Not enough free nodes available",
		"Not enough free nodes available"
	},
	{	/* SERVER_PROJECT_LIMIT_REACHED */
		"Project has reached server running limit.",
		"Server per-project limit reached"
	},
	{	/* SERVER_PROJECT_RES_LIMIT_REACHED */
		"Server per-project limit reached on resource %s",
		"Server per-project limit reached on resource %s"
	},
	{	/* SERVER_BYPROJECT_RES_LIMIT_REACHED */
		"would exceed project %s's limit on resource %s in complex",
		"would exceed project %s's limit on resource %s in complex"
	},
	{	/* SERVER_BYPROJECT_JOB_LIMIT_REACHED */
		"Server job limit reached for project %s",
		"Server job limit reached for project %s"
	},
	{	/* QUEUE_PROJECT_LIMIT_REACHED */
		"Project has reached queue %s's running limit.",
		"Queue %s per-project job limit reached"
	},
	{	/* QUEUE_PROJECT_RES_LIMIT_REACHED */
		"Queue %s per-project limit reached on resource %s",
		"Queue %s per-project limit reached on resource %s"
	},
	{	/* QUEUE_BYPROJECT_RES_LIMIT_REACHED */
		"would exceed project %s's limit on resource %s in queue %s",
		"would exceed project %s's limit on resource %s in queue %s"
	},
	{	/* QUEUE_BYPROJECT_JOB_LIMIT_REACHED */
		"Queue %s job limit reached for project %s",
		"Queue %s job limit reached for project %s"
	},
	{	/* NO_TOTAL_NODES */
		"Not enough total nodes available",
		"Not enough total nodes available"
	},
	{     /* INVALID_RESRESV */
		"Invalid Job/Resv %s",
		"Invalid Job/Resv %s"
	},
	{	/* JOB_UNDER_THRESHOLD */
		"Job is under job_sort_formula threshold value",
		"Job is under job_sort_formula threshold value",
	},
	{	/* MAX_RUN_SUBJOBS */
		"Number of concurrent running subjobs limit reached",
		"Number of concurrent running subjobs limit reached",
#ifdef NAS
	},
	/* localmod 034 */
	{	/* GROUP_CPU_SHARE */
		"Job would exceed mission CPU share",
		"Job would exceed mission CPU share",
	},
	{	/* GROUP_CPU_INSUFFICIENT */
		"Job exceeds total mission share",
		"Job exceeds total mission share",
	},
	/* localmod 998 */
	{	/* RESOURCES_INSUFFICIENT */
		"Too few free resources",
		"Too few free resources",
#endif
	},
};

#define	ERR2COMMENT(code)	(fctt[(code) - RET_BASE].fc_comment)
#define	ERR2INFO(code)		(fctt[(code) - RET_BASE].fc_info)


/**
 * @brief	pthread routine for querying a chunk of jobs
 *
 * @param[in,out]	data - th_data_query_jinfo object for the querying
 *
 * @return void
 */
void
query_jobs_chunk(th_data_query_jinfo *data)
{
	struct batch_status *jobs;
	resource_resv **resresv_arr;
	server_info *sinfo;
	queue_info *qinfo;
	int sidx;
	int eidx;
	int num_jobs_chunk;
	int i;
	int jidx;
	struct batch_status *cur_job;
	schd_error *err;
	int pbs_sd;
	status *policy;

	jobs = data->jobs;
	sinfo = data->sinfo;
	qinfo = data->qinfo;
	pbs_sd = data->pbs_sd;
	policy = data->policy;
	sidx = data->sidx;
	eidx = data->eidx;
	num_jobs_chunk = eidx - sidx + 1;

	err = new_schd_error();
	if(err == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		data->error = 1;
		return;
	}

	resresv_arr = static_cast<resource_resv **>(malloc(sizeof(resource_resv *) * (num_jobs_chunk + 1)));
	if (resresv_arr == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		data->error = 1;
		return;
	}
	resresv_arr[0] = NULL;

	/* Move to the linked list item corresponding to the 'start' index */
	for (cur_job = jobs, i = 0; i < sidx && cur_job != NULL; cur_job = cur_job->next, i++)
		;

	for (i = sidx, jidx = 0; i <= eidx && cur_job != NULL; cur_job = cur_job->next, i++) {
		resource_resv *resresv;

		if ((resresv = query_job(pbs_sd, cur_job, sinfo, qinfo, err)) == NULL) {
			data->error = 1;
			free_schd_error(err);
			free_resource_resv_array(resresv_arr);
			return;
		}

		/* do a validity check to see if the job is sane.  If we're peering and
		 * we're not a manager at the remote host, we wont have necessary attribs
		 * like euser and egroup
		 */
		if (resresv->is_invalid || !is_resource_resv_valid(resresv, err) ) {
			schdlogerr(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG, resresv->name,
				"Job is invalid - ignoring for this cycle", err);
			/* decrement i because we're about to increment it as part of the for()
			 * header.  We to continue adding valid jobs to our array.  We're
			 * freeing what we allocated and ignoring this job completely.
			 */
			delete resresv;
			continue;
		}

		/* Make sure scheduler does not process a subjob in undesirable state*/
		if (resresv->job->is_subjob && !resresv->job->is_running && !resresv->job->is_exiting &&
			!resresv->job->is_suspended && !resresv->job->is_provisioning) {
			log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_RESV, LOG_DEBUG,
				resresv->name, "Subjob found in undesirable state, ignoring this job");
			delete resresv;
			continue;
		}

		/* if the job's fairshare entity has no percentage of the machine,
		 * the job can not run if enforce_no_shares is set
		 */
		if (policy->fair_share && conf.enforce_no_shares) {
			if (resresv->job->ginfo != NULL &&
			resresv->job->ginfo->tree_percentage == 0) {
				set_schd_error_codes(err, NEVER_RUN, NO_FAIRSHARES);
			}
		}

#ifdef NAS /* localmod 034 */
		site_set_job_share(resresv);
#endif /* localmod 034 */

		/* Don't consider a job not in a queued state as runnable */
		if (!in_runnable_state(resresv))
			resresv->can_not_run = 1;

#ifdef RESC_SPEC
		/* search_for_rescspec() sets jinfo->rspec */
		if (!search_for_rescspec(resresv, qinfo->server->nodes))
			set_schd_error_codes(err, NOT_RUN, NO_NODE_RESOURCES);
#endif

		if (err->error_code != SUCCESS) {
			update_job_can_not_run(pbs_sd, resresv, err);
			clear_schd_error(err);
		}
		resresv_arr[jidx++] = resresv;
	}

	resresv_arr[jidx] = NULL;
	data->oarr = resresv_arr;

	free_schd_error(err);
}

/**
 * @brief	Allocates th_data_query_jinfo for multi-threading of query_jobs
 *
 * @param[in]	policy	-	policy info
 * @param[in]	pbs_sd	-	connection to pbs_server
 * @param[in]	jobs	-	batch_status of jobs
 * @param[in]	qinfo	-	queue to get jobs from
 * @param[in]	sidx	-	start index for the jobs list for the thread
 * @param[in]	eidx	-	end index for the jobs list for the thread
 *
 * @return th_data_query_jinfo *
 * @retval a newly allocated th_data_query_jinfo object
 * @retval NULL for malloc error
 */
static inline th_data_query_jinfo *
alloc_tdata_jquery(status *policy, int pbs_sd, struct batch_status *jobs, queue_info *qinfo,
		int sidx, int eidx)
{
	th_data_query_jinfo *tdata;

	tdata = static_cast<th_data_query_jinfo *>(malloc(sizeof(th_data_query_jinfo)));
	if (tdata == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}
	tdata->error = 0;
	tdata->jobs = jobs;
	tdata->oarr = NULL; /* Will be filled by the thread routine */
	tdata->sinfo = qinfo->server;
	tdata->qinfo = qinfo;
	tdata->pbs_sd = pbs_sd;
	tdata->policy = policy;
	tdata->sidx = sidx;
	tdata->eidx = eidx;

	return tdata;
}

/**
 * @brief
 * 		create an array of jobs in a specified queue
 *
 * @par NOTE:
 * 		anything reservation related needs to happen in
 *		query_reservations().  Since it is called after us,
 *		reservations aren't available at this point.
 *
 * @param[in]	policy	-	policy info
 * @param[in]	pbs_sd	-	connection to pbs_server
 * @param[in]	qinfo	-	queue to get jobs from
 * @param[in]	pjobs   -	possible job array to add too
 * @param[in]	queue_name	-	the name of the queue to query (local/remote)
 *
 * @return	pointer to the head of a list of jobs
 * @par MT-safe: No
 */
resource_resv **
query_jobs(status *policy, int pbs_sd, queue_info *qinfo, resource_resv **pjobs, const std::string& queue_name)
{
	/* pbs_selstat() takes a linked list of attropl structs which tell it
	 * what information about what jobs to return.  We want all jobs which are
	 * in a specified queue
	 */
	struct attropl opl = { NULL, const_cast<char *>(ATTR_q), NULL, NULL, EQ };
	static struct attropl opl2[2] = { { &opl2[1], const_cast<char *>(ATTR_state), NULL, const_cast<char *>("Q"), EQ},
		{ NULL, const_cast<char *>(ATTR_array), NULL, const_cast<char *>("True"), NE} };
	static struct attrl *attrib = NULL;

	/* linked list of jobs returned from pbs_selstat() */
	struct batch_status *jobs;

	/* current job in jobs linked list */
	struct batch_status *cur_job;

	/* array of internal scheduler structures for jobs */
	resource_resv **resresv_arr;

	/* number of jobs in resresv_arr */
	int num_jobs = 0;
	/* number of jobs in pjobs */
	int num_prev_jobs;
	int num_new_jobs;

	/* for multi-threading */
	int jidx;
	th_data_query_jinfo *tdata = NULL;
	th_task_info *task = NULL;
	resource_resv ***jinfo_arrs_tasks;
	int tid;

	if (policy == NULL || qinfo == NULL || queue_name.empty())
		return pjobs;

	opl.value = const_cast<char *>(queue_name.c_str());

	if (qinfo->is_peer_queue)
		opl.next = &opl2[0];

	if (attrib == NULL) {
		const char *jobattrs[] = {
			ATTR_p,
			ATTR_qtime,
			ATTR_qrank,
			ATTR_etime,
			ATTR_stime,
			ATTR_N,
			ATTR_state,
			ATTR_substate,
			ATTR_sched_preempted,
			ATTR_comment,
			ATTR_released,
			ATTR_euser,
			ATTR_egroup,
			ATTR_project,
			ATTR_resv_ID,
			ATTR_altid,
			ATTR_SchedSelect,
			ATTR_array_id,
			ATTR_node_set,
			ATTR_array,
			ATTR_array_index,
			ATTR_topjob_ineligible,
			ATTR_array_indices_remaining,
			ATTR_execvnode,
			ATTR_l,
			ATTR_rel_list,
			ATTR_used,
			ATTR_accrue_type,
			ATTR_eligible_time,
			ATTR_estimated,
			ATTR_c,
			ATTR_r,
			ATTR_depend,
			ATTR_A,
			ATTR_max_run_subjobs,
			ATTR_server_inst_id,
			NULL};

		for (int i = 0; jobattrs[i] != NULL; i++) {
			struct attrl *temp_attrl;

			temp_attrl = new_attrl();
			temp_attrl->name = strdup(jobattrs[i]);
			temp_attrl->next = attrib;
			temp_attrl->value = const_cast<char *>("");
			attrib = temp_attrl;
		}
	}

	/* get jobs from PBS server */
	if ((jobs = send_selstat(pbs_sd, &opl, attrib, const_cast<char *>("S"))) == NULL) {
		if (pbs_errno > 0) {
			const char *errmsg = pbs_geterrmsg(pbs_sd);
			if (errmsg == NULL)
				errmsg = "";
			log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_NOTICE, "job_info",
					"pbs_selstat failed: %s (%d)", errmsg, pbs_errno);
		}
		return pjobs;
	}

	/* count the number of new jobs */
	cur_job = jobs;
	while (cur_job != NULL) {
		num_jobs++;
		cur_job = cur_job->next;
	}
	num_new_jobs = num_jobs;

	/* if there are previous jobs, count those too */
	num_prev_jobs = count_array(pjobs);
	num_jobs += num_prev_jobs;


	/* allocate enough space for all the jobs and the NULL sentinal */
	resresv_arr = static_cast<resource_resv **>(realloc(pjobs, sizeof(resource_resv*) * (num_jobs + 1)));

	if (resresv_arr == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		pbs_statfree(jobs);
		return NULL;
	}
	resresv_arr[num_prev_jobs] = NULL;

	tid = *((int *) pthread_getspecific(th_id_key));
	if (tid != 0 || num_threads <= 1) {
		/* don't use multi-threading if I am a worker thread or num_threads is 1 */
		tdata = alloc_tdata_jquery(policy, pbs_sd, jobs, qinfo, 0, num_new_jobs - 1);
		if (tdata == NULL) {
			free_resource_resv_array(resresv_arr);
			pbs_statfree(jobs);
			return NULL;
		}
		query_jobs_chunk(tdata);

		if (tdata->error || tdata->oarr == NULL) {
			free_resource_resv_array(resresv_arr);
			pbs_statfree(jobs);
			free(tdata->oarr);
			free(tdata);
			return NULL;
		}

		jidx = num_prev_jobs;
		for (int j = 0; tdata->oarr[j] != NULL; j++) {
			resresv_arr[jidx++] = tdata->oarr[j];
		}
		free(tdata->oarr);
		free(tdata);
		resresv_arr[jidx] = NULL;
	} else {
		int chunk_size = num_new_jobs / num_threads;
		int th_err = 0;
		int num_tasks = 0;

		chunk_size = (chunk_size > MT_CHUNK_SIZE_MIN) ? chunk_size : MT_CHUNK_SIZE_MIN;
		chunk_size = (chunk_size < MT_CHUNK_SIZE_MAX) ? chunk_size : MT_CHUNK_SIZE_MAX;
		for (int j = 0; num_new_jobs > 0;
				num_tasks++, j += chunk_size, num_new_jobs -= chunk_size) {
			tdata = alloc_tdata_jquery(policy, pbs_sd, jobs, qinfo, j, j + chunk_size - 1);
			if (tdata == NULL) {
				th_err = 1;
				break;
			}
			task = static_cast<th_task_info *>(malloc(sizeof(th_task_info)));
			if (task == NULL) {
				free(tdata);
				log_err(errno, __func__, MEM_ERR_MSG);
				th_err = 1;
				break;
			}
			task->task_id = num_tasks;
			task->task_type = TS_QUERY_JOB_INFO;
			task->thread_data = (void*) tdata;

			pthread_mutex_lock(&work_lock);
			ds_enqueue(work_queue, (void*) task);
			pthread_cond_signal(&work_cond);
			pthread_mutex_unlock(&work_lock);
		}
		jinfo_arrs_tasks = static_cast<resource_resv ***>(malloc(num_tasks * sizeof(resource_resv**)));
		if (jinfo_arrs_tasks == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			th_err = 1;
		}
		/* Get results from worker threads */
		for (int i = 0; i < num_tasks;) {
			pthread_mutex_lock(&result_lock);
			while (ds_queue_is_empty(result_queue))
				pthread_cond_wait(&result_cond, &result_lock);
			while (!ds_queue_is_empty(result_queue)) {
				task = static_cast<th_task_info *>(ds_dequeue(result_queue));
				tdata = static_cast<th_data_query_jinfo *>(task->thread_data);
				if (tdata->error)
					th_err = 1;
				jinfo_arrs_tasks[task->task_id] = tdata->oarr;
				free(tdata);
				free(task);
				i++;
			}
			pthread_mutex_unlock(&result_lock);
		}
		if (th_err) {
			pbs_statfree(jobs);
			free_resource_resv_array(resresv_arr);
			free(jinfo_arrs_tasks);
			return NULL;
		}
		/* Assemble job info objects from various threads into the resresv_arr */
		jidx = num_prev_jobs;
		for (int i = 0; i < num_tasks; i++) {
			if (jinfo_arrs_tasks[i] != NULL) {
				for (int j = 0; jinfo_arrs_tasks[i][j] != NULL; j++) {
					resresv_arr[jidx++] = jinfo_arrs_tasks[i][j];
				}
				free(jinfo_arrs_tasks[i]);
			}
		}
		resresv_arr[jidx] = NULL;
		free(jinfo_arrs_tasks);
	}

	pbs_statfree(jobs);

	return resresv_arr;
}

/**
 * @brief
 *		query_job - takes info from a batch_status about a job and
 *			 converts it into a resource_resv struct
 *
 * 	  @param[in] pbs_sd - connection descriptor to the server
 *	  @param[in] job - batch_status struct of job
 *	  @param[in] qinfo - queue where job resides
 *	  @param[out] err - returns error info
 *
 *	@return resource_resv
 *	@retval job (may be invalid, if so, err will report why)
 *	@retval  or NULL on error
 */

resource_resv *
query_job(int pbs_sd, struct batch_status *job, server_info *sinfo, queue_info *qinfo, schd_error *err)
{
	resource_resv *resresv;		/* converted job */
	struct attrl *attrp;		/* list of attributes returned from server */
	long count;			/* long used in string->long conversion */
	char *endp;			/* used for strtol() */
	resource_req *resreq;		/* resource_req list for resources requested  */

	if ((resresv = new resource_resv(job->name)) == NULL)
		return NULL;

	if ((resresv->job = new_job_info()) ==NULL) {
		delete resresv;
		return NULL;
	}

	resresv->rank = get_sched_rank();

	attrp = job->attribs;

	resresv->server = sinfo;
	resresv->job->queue = qinfo;

	resresv->is_job = true;

	resresv->job->can_checkpoint = true;	/* default can be checkpointed */
	resresv->job->can_requeue = true;		/* default can be requeued */
	resresv->job->can_suspend = true;		/* default can be suspended */

	while (attrp != NULL && !resresv->is_invalid) {
		clear_schd_error(err);
		if (conf.fairshare_ent == attrp->name) {
			if (sinfo->fstree != NULL) {
#ifdef NAS /* localmod 059 */
				/* This is a hack to allow -A specification for testing, but
				 * ignore most incorrect user -A values
				 */
				if (strchr(attrp->value, ':') != NULL) {
					/* moved to query_jobs() in order to include the queue name
					 resresv->job->ginfo = find_alloc_ginfo( attrp->value,
					 sinfo->fstree->root );
					 */
					/* localmod 034 */
					resresv->job->sh_info = site_find_alloc_share(sinfo, attrp->value);
				}
#else
				resresv->job->ginfo = find_alloc_ginfo(attrp->value, sinfo->fstree->root);
#endif /* localmod 059 */
			}
			else
				resresv->job->ginfo = NULL;
		}
		if (!strcmp(attrp->name, ATTR_p)) { /* priority */
			count = strtol(attrp->value, &endp, 10);
			if (*endp == '\0')
				resresv->job->priority = count;
			else
				resresv->job->priority = -1;
#ifdef NAS /* localmod 045 */
			resresv->job->NAS_pri = resresv->job->priority;
#endif /* localmod 045 */
		}
		else if (!strcmp(attrp->name, ATTR_qtime)) { /* queue time */
			count = strtol(attrp->value, &endp, 10);
			if (*endp == '\0')
				resresv->qtime = count;
			else
				resresv->qtime = -1;
		}
		else if (!strcmp(attrp->name, ATTR_qrank)) { /* queue rank */
			long long qrank;
			qrank = strtoll(attrp->value, &endp, 10);
			if (*endp == '\0')
				resresv->qrank = qrank;
			else
				resresv->qrank = -1;
		}
		else if (!strcmp(attrp->name, ATTR_server_inst_id)) {
			resresv->svr_inst_id = string_dup(attrp->value);
			if (resresv->svr_inst_id == NULL) {
				delete resresv;
				return NULL;
			}
		}
		else if (!strcmp(attrp->name, ATTR_etime)) { /* eligible time */
			count = strtol(attrp->value, &endp, 10);
			if (*endp == '\0')
				resresv->job->etime = count;
			else
				resresv->job->etime = -1;
		}
		else if (!strcmp(attrp->name, ATTR_stime)) { /* job start time */
			count = strtol(attrp->value, &endp, 10);
			if (*endp == '\0')
				resresv->job->stime = count;
			else
				resresv->job->stime = -1;
		}
		else if (!strcmp(attrp->name, ATTR_N))		/* job name (qsub -N) */
			resresv->job->job_name = string_dup(attrp->value);
		else if (!strcmp(attrp->name, ATTR_state)) { /* state of job */
			if (set_job_state(attrp->value, resresv->job) == 0) {
				set_schd_error_codes(err, NEVER_RUN, ERR_SPECIAL);
				set_schd_error_arg(err, SPECMSG, "Job is in an invalid state");
				resresv->is_invalid = 1;
			}
		}
		else if (!strcmp(attrp->name, ATTR_substate)) {
			if (!strcmp(attrp->value, SUSP_BY_SCHED_SUBSTATE))
				resresv->job->is_susp_sched = true;
			if (!strcmp(attrp->value, PROVISIONING_SUBSTATE))
				resresv->job->is_provisioning = true;
			if (!strcmp(attrp->value, PRERUNNING_SUBSTATE))
				resresv->job->is_prerunning = true;
		}
		else if (!strcmp(attrp->name, ATTR_sched_preempted)) {
			count = strtol(attrp->value, &endp, 10);
			if (*endp == '\0') {
				resresv->job->time_preempted = count;
				resresv->job->is_preempted = true;
			}
		}
		else if (!strcmp(attrp->name, ATTR_comment))	/* job comment */
			resresv->job->comment = string_dup(attrp->value);
		else if (!strcmp(attrp->name, ATTR_released)) /* resources_released */
			resresv->job->resreleased = parse_execvnode(attrp->value, sinfo, NULL);
		else if (!strcmp(attrp->name, ATTR_euser))	/* account name */
			resresv->user = string_dup(attrp->value);
		else if (!strcmp(attrp->name, ATTR_egroup))	/* group name */
			resresv->group = string_dup(attrp->value);
		else if (!strcmp(attrp->name, ATTR_project))	/* project name */
			resresv->project = string_dup(attrp->value);
		else if (!strcmp(attrp->name, ATTR_resv_ID))	/* reserve_ID */
			resresv->job->resv_id = string_dup(attrp->value);
		else if (!strcmp(attrp->name, ATTR_altid))    /* vendor ID */
			resresv->job->alt_id = string_dup(attrp->value);
		else if (!strcmp(attrp->name, ATTR_SchedSelect))
#ifdef NAS /* localmod 031 */
		{
			resresv->job->schedsel = string_dup(attrp->value);
#endif /* localmod 031 */

			resresv->select = parse_selspec(attrp->value);
#ifdef NAS /* localmod 031 */
		}
#endif /* localmod 031 */
		else if (!strcmp(attrp->name, ATTR_array_id))
			resresv->job->array_id = attrp->value;
		else if (!strcmp(attrp->name, ATTR_node_set))
			resresv->node_set_str = break_comma_list(attrp->value);
		else if (!strcmp(attrp->name, ATTR_array)) { /* array */
			if (!strcmp(attrp->value, ATR_TRUE))
				resresv->job->is_array = true;
		}
		else if (!strcmp(attrp->name, ATTR_array_index)) { /* array_index */
			count = strtol(attrp->value, &endp, 10);
			if (*endp == '\0')
				resresv->job->array_index = count;
			else
				resresv->job->array_index = -1;

			resresv->job->is_subjob = true;
		}
		else if (!strcmp(attrp->name, ATTR_topjob_ineligible)) {
			if (!strcmp(attrp->value, ATR_TRUE))
				resresv->job->topjob_ineligible = true;
		}
		/* array_indices_remaining */
		else if (!strcmp(attrp->name, ATTR_array_indices_remaining))
			resresv->job->queued_subjobs = range_parse(attrp->value);
		else if (!strcmp(attrp->name, ATTR_max_run_subjobs)) {
			count = strtol(attrp->value, &endp, 10);
			if (*endp == '\0')
				resresv->job->max_run_subjobs = count;
		}
		else if (!strcmp(attrp->name, ATTR_execvnode)) {
			nspec **tmp_nspec_arr;
			tmp_nspec_arr = parse_execvnode(attrp->value, sinfo, NULL);
			resresv->nspec_arr = combine_nspec_array(tmp_nspec_arr);
			free_nspecs(tmp_nspec_arr);

			if (resresv->nspec_arr != NULL)
				resresv->ninfo_arr = create_node_array_from_nspec(resresv->nspec_arr);
		} else if (!strcmp(attrp->name, ATTR_l)) { /* resources requested*/
			resreq = find_alloc_resource_req_by_str(resresv->resreq, attrp->resource);
			if (resreq == NULL) {
				delete resresv;
				return NULL;
			}

			if (set_resource_req(resreq, attrp->value) != 1) {
				set_schd_error_codes(err, NEVER_RUN, ERR_SPECIAL);
				set_schd_error_arg(err, SPECMSG, "Bad requested resource data");
				resresv->is_invalid = true;
				return resresv;
			} else {
				if (resresv->resreq == NULL)
					resresv->resreq = resreq;
#ifdef NAS
				if (!strcmp(attrp->resource, "nodect")) { /* nodect for sort */
					/* localmod 040 */
					count = strtol(attrp->value, &endp, 10);
					if (*endp == '\0')
						resresv->job->nodect = count;
					else
						resresv->job->nodect = 0;
					/* localmod 034 */
					resresv->job->accrue_rate = resresv->job->nodect; /* XXX should be SBU rate */
				}
#endif
				if (!strcmp(attrp->resource, "place")) {
					resresv->place_spec = parse_placespec(attrp->value);
					if (resresv->place_spec == NULL) {
						set_schd_error_codes(err, NEVER_RUN, ERR_SPECIAL);
						set_schd_error_arg(err, SPECMSG, "invalid placement spec");
						resresv->is_invalid = true;

					}
				}
			}
		} else if (!strcmp(attrp->name, ATTR_rel_list)) {
			resreq = find_alloc_resource_req_by_str(resresv->job->resreq_rel, attrp->resource);
			if (resreq != NULL)
				set_resource_req(resreq, attrp->value);
			if (resresv->job->resreq_rel == NULL)
				resresv->job->resreq_rel = resreq;
		} else if (!strcmp(attrp->name, ATTR_used)) { /* resources used */
			resreq =
				find_alloc_resource_req_by_str(resresv->job->resused, attrp->resource);
			if (resreq != NULL)
				set_resource_req(resreq, attrp->value);
			if (resresv->job->resused ==NULL)
				resresv->job->resused = resreq;
		} else if (!strcmp(attrp->name, ATTR_accrue_type)) {
			count = strtol(attrp->value, &endp, 10);
			if (*endp == '\0')
				resresv->job->accrue_type = count;
			else
				resresv->job->accrue_type = 0;
		} else if (!strcmp(attrp->name, ATTR_eligible_time))
			resresv->job->eligible_time = (time_t) res_to_num(attrp->value, NULL);
		else if (!strcmp(attrp->name, ATTR_estimated)) {
			if (!strcmp(attrp->resource, "start_time")) {
				resresv->job->est_start_time =
					(time_t) res_to_num(attrp->value, NULL);
			}
			else if (!strcmp(attrp->resource, "execvnode"))
				resresv->job->est_execvnode = string_dup(attrp->value);
		}
		else if (!strcmp(attrp->name, ATTR_c)) { /* checkpoint allowed? */
			if (strcmp(attrp->value, "n") == 0)
				resresv->job->can_checkpoint = false;
		}
		else if (!strcmp(attrp->name, ATTR_r)) { /* reque allowed ? */
			if (strcmp(attrp->value, ATR_FALSE) == 0)
				resresv->job->can_requeue = false;
		}
		else if (!strcmp(attrp->name, ATTR_depend)) {
			resresv->job->depend_job_str = string_dup(attrp->value);
		}

		attrp = attrp->next;
	}

#ifdef NAS /* localmod 040 */
	/* we modify nodect to be the same value for all jobs in queues that are
	 * configured to ignore nodect key sorting, for two reasons:
	 * 1. obviously to accomplish ignoring of nodect key sorting
	 * 2. maintain stability of qsort when comparing with a job in a queue that
	 *    does not require nodect key sorting
	 * note that this assumes nodect is used only for sorting
	 */
	if (qinfo->ignore_nodect_sort)
		resresv->job->nodect = 999999;
#endif /* localmod 040 */

	if (qinfo->is_peer_queue) {
		resresv->is_peer_ob = 1;
		resresv->job->peer_sd = pbs_sd;
	}

	if ((resresv->aoename = getaoename(resresv->select)) != NULL)
		resresv->is_prov_needed = true;
	if ((resresv->eoename = geteoename(resresv->select)) != NULL) {
		/* job with a power profile can't be checkpointed or suspended */
		resresv->job->can_checkpoint = false;
		resresv->job->can_suspend = false;
	}

	if (resresv->select != NULL && resresv->select->chunks != NULL) {
		/*
		 * Job is invalid if there are no resources in a chunk.  Usually
		 * happens because we strip out resources not in conf.res_to_check
		 */
		int k;
		for (k = 0; resresv->select->chunks[k] != NULL; k++)
			if (resresv->select->chunks[k]->req == NULL) {
				set_schd_error_codes(err, NEVER_RUN, INVALID_RESRESV);
				set_schd_error_arg(err, ARG1, "invalid chunk in select");
				resresv->is_invalid = true;
				return resresv;
			}
	}

	if (resresv->place_spec->scatter &&
	    resresv->select->total_chunks > 1)
		resresv->will_use_multinode = true;

	if (resresv->job->is_queued && resresv->nspec_arr != NULL)
		resresv->job->is_checkpointed = true;

	/* If we did not wait for mom to start the job (throughput mode),
	 * it is possible that we’re seeing a running job without a start time set.
	 * The stime is set when the mom reports back to the server to say the job is running.
	 */
	if ((resresv->job->is_running) && (resresv->job->stime == UNSPECIFIED))
		resresv->job->stime = sinfo->server_time + 1;

	/* For jobs that have an exec_vnode, we create a "select" based
	 * on its exec_vnode.  We do this so if we ever need to run the job
	 * again, we will replace the job on the exact vnodes/resources it originally used.
	 */
	std::string selectspec;
	if (resresv->job->is_suspended && resresv->job->resreleased != NULL)
		/* For jobs that are suspended and have resource_released, the "select"
		 * we create is based off of resources_released instead of the exec_vnode.
		 */
		selectspec = create_select_from_nspec(resresv->job->resreleased);
	else if (resresv->nspec_arr != NULL)
		selectspec = create_select_from_nspec(resresv->nspec_arr);

	if (!selectspec.empty())
		resresv->execselect = parse_selspec(selectspec);

	set_job_times(pbs_sd, resresv, sinfo->server_time);

	/* Add Resource_List resources after resource_used on the job.  This is 
	 * mainly for fairshare.  This allows us to use custom resources only found in 
	 * Resource_List in our fairshare formula.  If a resource appears in both
	 * lists, we'll find the one in resources_used first and use that.
	 */
	auto req = resresv->job->resused;

	if (req != NULL) {
		while (req->next != NULL)
			req = req->next;

		req->next = dup_resource_req_list(resresv->resreq);
	}


	/* if the fairshare entity was not set by query_job(), then check
	 * if it's 'queue' and if so, set the group info to the queue name
	 */
	if (conf.fairshare_ent == "queue") {
		if (sinfo->fstree != NULL) {
			resresv->job->ginfo =
				find_alloc_ginfo(qinfo->name.c_str(), sinfo->fstree->root);
		} else
			resresv->job->ginfo = NULL;
	}

	/* if fairshare_ent is invalid or the job doesn't have one, give a default
	 * of something most likely unique - egroup:euser
	 */

	if (resresv->job->ginfo == NULL) {
		char fairshare_name[100];
#ifdef NAS /* localmod 058 */
		sprintf(fairshare_name, "%s:%s:%s", resresv->group, resresv->user,
			(qinfo->name != NULL ? qinfo->name : ""));
#else
		sprintf(fairshare_name, "%s:%s", resresv->group, resresv->user);
#endif /* localmod 058 */
		if (resresv->server->fstree != NULL) {
			resresv->job->ginfo = find_alloc_ginfo(fairshare_name, sinfo->fstree->root);
		} else
			resresv->job->ginfo = NULL;
	}
#ifdef NAS /* localmod 034 */
	if (resresv->job->sh_info == NULL) {
		sprintf(fairshare_name, "%s:%s", resresv->group, resresv->user);
		resresv->job->sh_info = site_find_alloc_share(sinfo,
							      fairshare_name);
	}
	site_set_share_type(sinfo, resresv);
#endif /* localmod 034 */

	return resresv;
}

/**
 *	@brief
 *		job_info constructor
 *
 * @return job_info *
 */
job_info *
new_job_info()
{
	job_info *jinfo;

	if ((jinfo = new job_info()) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	jinfo->is_queued = 0;
	jinfo->is_running = 0;
	jinfo->is_held = 0;
	jinfo->is_waiting = 0;
	jinfo->is_transit = 0;
	jinfo->is_exiting = 0;
	jinfo->is_suspended = 0;
	jinfo->is_susp_sched = 0;
	jinfo->is_userbusy = 0;
	jinfo->is_begin = 0;
	jinfo->is_expired = 0;
	jinfo->is_checkpointed = 0;
	jinfo->accrue_type=0;
	jinfo->eligible_time=0;
	jinfo->can_not_preempt = 0;
	jinfo->topjob_ineligible = 0;

	jinfo->is_array = 0;
	jinfo->is_subjob = 0;

	jinfo->can_checkpoint = 1; /* default can be checkpointed */
	jinfo->can_requeue = 1;	   /* default can be reuqued */
	jinfo->can_suspend = 1;    /* default can be suspended */

	jinfo->is_provisioning = 0;
	jinfo->is_preempted = 0;
	jinfo->is_prerunning = 0;

	jinfo->job_name = NULL;
	jinfo->comment = NULL;
	jinfo->job_name = NULL;
	jinfo->resv_id = NULL;
	jinfo->alt_id = NULL;
	jinfo->queue = NULL;
	jinfo->resv = NULL;
	jinfo->priority = 0;
	jinfo->etime = UNSPECIFIED;
	jinfo->stime = UNSPECIFIED;
	jinfo->preempt = 0;
	jinfo->preempt_status = 0;
	jinfo->peer_sd = -1;
	jinfo->est_start_time = UNSPECIFIED;
	jinfo->time_preempted = UNSPECIFIED;
	jinfo->est_execvnode = NULL;
	jinfo->resused = NULL;
	jinfo->ginfo = NULL;

	jinfo->array_index = UNSPECIFIED;
	jinfo->parent_job = NULL;
	jinfo->queued_subjobs = NULL;
	jinfo->max_run_subjobs = UNSPECIFIED;
	jinfo->running_subjobs = 0;
	jinfo->attr_updates = NULL;
	jinfo->resreleased = NULL;
	jinfo->resreq_rel = NULL;
	jinfo->depend_job_str = NULL;
	jinfo->dependent_jobs = NULL;


	jinfo->formula_value = 0.0;

#ifdef RESC_SPEC
	jinfo->rspec = NULL;
#endif

#ifdef NAS
	/* localmod 045 */
	jinfo->NAS_pri = 0;
	/* localmod 034 */
	jinfo->sh_amts = NULL;
	jinfo->sh_info = NULL;
	jinfo->accrue_rate = 0;
	/* localmod 040 */
	jinfo->nodect = 0;
	/* localmod 031 */
	jinfo->schedsel = NULL;
	/* localmod 053 */
	jinfo->u_info = NULL;
#endif

	return jinfo;
}

/**
 *	@brief
 *		job_info destructor
 *
 * @param[in,out]	jinfo	-	Job Info structure to be freed.
 */

void
free_job_info(job_info *jinfo)
{
	if (jinfo == NULL)
		return;
	
	if (jinfo->comment != NULL)
		free(jinfo->comment);

	if (jinfo->job_name != NULL)
		free(jinfo->job_name);

	if (jinfo->resv_id != NULL)
		free(jinfo->resv_id);

	if (jinfo->alt_id != NULL)
		free(jinfo->alt_id);

	if (jinfo->est_execvnode != NULL)
		free(jinfo->est_execvnode);

	if (jinfo->queued_subjobs != NULL)
		free_range_list(jinfo->queued_subjobs);

	if (jinfo->depend_job_str != NULL)
		free (jinfo->depend_job_str);

	if (jinfo->dependent_jobs != NULL)
		free(jinfo->dependent_jobs);

	free_resource_req_list(jinfo->resused);

	free_attrl_list(jinfo->attr_updates);

	free_resource_req_list(jinfo->resreq_rel);

	free_nspecs(jinfo->resreleased);

#ifdef RESC_SPEC
	free_rescspec(jinfo->rspec);
#endif
#ifdef NAS
	/* localmod 034 */
	if (jinfo->sh_amts)
		free(jinfo->sh_amts);

	/* localmod 031 */
	if (jinfo->schedsel)
		free(jinfo->schedsel);
#endif
	delete jinfo;
}


/**
 * @brief
 *		set_job_state - set the state flag in a job_info structure
 *			i.e. the is_* bit
 *
 * @param[in]	state	-	the state
 * @param[in,out]	jinfo	-	the job info structure
 *
 * @return	1	-	if state is successfully set
 * @return	0	-	if state is not set
 */
int
set_job_state(const char *state, job_info *jinfo)
{
	if (jinfo == NULL)
		return 0;

	/* turn off all state bits first to make sure only 1 is set at the end */
	jinfo->is_queued = jinfo->is_running = jinfo->is_transit =
		jinfo->is_held = jinfo->is_waiting = jinfo->is_exiting =
		jinfo->is_suspended = jinfo->is_userbusy = jinfo->is_begin =
		jinfo->is_expired = 0;

	switch (state[0]) {
		case 'Q':
			jinfo->is_queued = 1;
			break;

		case 'R':
			jinfo->is_running = 1;
			break;

		case 'T':
			jinfo->is_transit = 1;
			break;

		case 'H':
			jinfo->is_held = 1;
			break;

		case 'W':
			jinfo->is_waiting = 1;
			break;

		case 'E':
			jinfo->is_exiting = 1;
			break;

		case 'S':
			jinfo->is_suspended = 1;
			break;

		case 'U':
			jinfo->is_userbusy = 1;
			break;

		case 'B':
			jinfo->is_begin = 1;
			break;

		case 'X':
			jinfo->is_expired = 1;
			break;

		default:
			return 0;
	}
	return 1;
}

/**
 * @brief	Check whether it's ok send attribute updates to server
 *
 * @param[in]	attrname - name of the attribute
 *
 * @return	int
 * @retval	0 for No
 * @retval	1 for Yes
 */
static int
can_send_update(const char *attrname)
{
	const char *attrs_to_throttle[] = {ATTR_comment, ATTR_estimated, NULL};
	int i;

	if (send_job_attr_updates)
		return 1;

	/* Check to see if the attr being updated is eligible for throttling */
	for (i = 0; attrs_to_throttle[i] != NULL; i++) {
		if (strcmp(attrs_to_throttle[i], attrname) == 0)
			return 0;
	}

	return 1;
}

/**
 * @brief
 * 		update job attributes on the server
 *
 * @param[in]	pbs_sd     -	connection to the pbs_server
 * @param[in]	resresv    -	job to update
 * @param[in]	attr_name  -	the name of the attribute to alter
 * @param[in]	attr_resc  -	resource part of the attribute (if any)
 * @param[in]	attr_value -	the value of the attribute to alter (as a str)
 * @param[in]	extra	-	extra attrl attributes to tag along on the alterjob
 * @param[in]	flags	-	UPDATE_NOW - call send_attr_updates() to update the attribute now
 *			     			UPDATE_LATER - attach attribute change to job to be sent all at once
 *							for the job.  NOTE: Only the jobs that are part
 *							of the server in main_sched_loop() will be updated in this way.
 *
 * @retval	1	attributes were updated or successfully attached to job
 * @retval	0	no attributes were updated for a valid reason
 * @retval -1	no attributes were updated for an error
 *
 */
int
update_job_attr(int pbs_sd, resource_resv *resresv, const char *attr_name,
	const char *attr_resc, const char *attr_value, struct attrl *extra, unsigned int flags)
{
	struct attrl *pattr = NULL;
	struct attrl *pattr2 = NULL;
	struct attrl *end;

	if (resresv == NULL  ||
		(attr_name == NULL && attr_value == NULL && extra == NULL))
		return -1;

	if (extra == NULL && (attr_name == NULL || attr_value == NULL))
		return -1;

	if (!resresv->is_job)
		return 0;

	/* if running in simulation then don't update but simulate that we have*/
	if (pbs_sd == SIMULATE_SD)
		return 1;

	/* don't try and update attributes for jobs on peer servers */
	if (resresv->is_peer_ob)
		return 0;

	/* if we've received a SIGPIPE, it means our connection to the server
	 * has gone away.  No need to attempt to contact again
	 */
	if (got_sigpipe)
		return -1;

	if (attr_name == NULL && attr_value == NULL) {
		end = pattr = dup_attrl_list(extra);
		if(pattr == NULL)
			return -1;
	} else {
		if ((flags & UPDATE_NOW) && !can_send_update(attr_name)) {
			struct attrl *iter_attrl = NULL;
			int attr_elig = 0;

			/* Check if any of the extra attrs are eligible to be sent */
			for (iter_attrl = extra; iter_attrl != NULL; iter_attrl = iter_attrl->next) {
				if (can_send_update(iter_attrl->name)) {
					attr_elig = 1;
					break;
				}
			}

			if (!attr_elig)
				return 0;
		}

		pattr = new_attrl();

		if (pattr == NULL)
			return -1;
		pattr->name = string_dup(attr_name);
		pattr->value = string_dup(attr_value);
		pattr->resource = string_dup(attr_resc);
		end = pattr;
		if (extra != NULL) {
			pattr2 = dup_attrl_list(extra);
			if (pattr2 == NULL) {
				free_attrl(pattr);
				return -1;
			}
			pattr->next = pattr2;
			/* extra may have been a list, let's find the end */
			for (end = pattr2; end->next != NULL; end = end ->next)
				;
		}
	}

	if(flags & UPDATE_LATER) {
		end->next = resresv->job->attr_updates;
		resresv->job->attr_updates = pattr;
	}

	if (pattr != NULL && (flags & UPDATE_NOW)) {
		int rc;
		rc = send_attr_updates(pbs_sd, resresv, pattr);
		free_attrl_list(pattr);
		return rc;
	}

	return 0;
}

/**
 * @brief
 * 		send delayed job attribute updates for job using send_attr_updates().
 *
 * @par
 * 		The main reason to use this function over a direct send_attr_update()
 *      call is so that the job's attr_updates list gets free'd and NULL'd.
 *      We don't want to send the attr updates multiple times
 *
 * @param[in]	pbs_sd	-	server connection descriptor
 * @param[in]	job	-	job to send attributes to
 *
 * @return	int(ret val from send_attr_updates)
 * @retval	1	- success
 * @retval	0	- failure to update
 */
int send_job_updates(int pbs_sd, resource_resv *job)
{
	int rc;
	struct attrl *iter_attr = NULL;

	if(job == NULL)
		return 0;

	if (!send_job_attr_updates) {
		int send = 0;
		for (iter_attr = job->job->attr_updates; iter_attr != NULL; iter_attr = iter_attr->next) {
			if (can_send_update(iter_attr->name)) {
				send = 1;
				break;
			}
		}
		if (!send)
			return 0;
	}

	rc = send_attr_updates(pbs_sd, job, job->job->attr_updates);

	free_attrl_list(job->job->attr_updates);
	job->job->attr_updates = NULL;
	return rc;
}

/**
 *	@brief
 *		unset job attributes on the server
 *
 * @param[in]	pbs_sd	-	connection to the pbs_server
 * @param[in]	resresv	-	job to update
 * @param[in]	attr_name	-	the name of the attribute to unset
 * @param[in]	flags	-	UPDATE_NOW : call send_attr_updates() to update the attribute now
 *			     			UPDATE_LATER - attach attribute change to job to be sent all at once
 *							for the job.  NOTE: Only the jobs that are part
 *							of the server in main_sched_loop() will be updated in this way.
 *
 *	@retval	1	: attributes were unset
 *	@retval	0	: no attributes were unset for a valid reason
 *	@retval -1	: no attributes were unset for an error
 *
 */
int
unset_job_attr(int pbs_sd, resource_resv *resresv, const char *attr_name, unsigned int flags)
{
	return (update_job_attr(pbs_sd, resresv, attr_name, NULL, "", NULL, flags));

}

/**
 * @brief
 *		update_job_comment - update a job's comment attribute.  If the job's
 *			     comment attr is identical, don't update
 *
 * @param[in]	pbs_sd	-	pbs connection descriptor
 * @param[in]	resresv -	the job to update
 * @param[in]	comment -	the comment string
 *
 * @return	int
 * @retval	1	: if the comment was updated
 * @retval	0	: if not
 *
 */
int
update_job_comment(int pbs_sd, resource_resv *resresv, char *comment) {
	int rc = 0;

	if (resresv == NULL || comment == NULL)
		return 0;

	if (!resresv->is_job || resresv->job ==NULL)
		return 0;

	/* no need to update the job comment if it is the same */
	if (resresv->job->comment == NULL ||
		strcmp(resresv->job->comment, comment)) {
		if (conf.update_comments) {
			rc = update_job_attr(pbs_sd, resresv, ATTR_comment, NULL, comment, NULL, UPDATE_LATER);
			if (rc > 0) {
				if (resresv->job->comment !=NULL)
					free(resresv->job->comment);
				resresv->job->comment = string_dup(comment);
			}
		}
	}
	return rc;
}

/**
 * @brief
 *		update_jobs_cant_run - update an array of jobs which can not run
 *
 * @param[in]	pbs_sd	-	connection to the PBS server
 * @param[in,out]	resresv_arr	-	the array to update
 * @param[in]	start	-	the job which couldn't run
 * @param[in]	comment	-	the comment to update
 * @param[in]	log_msg	-	the message to log for the job
 *
 * @return nothing
 *
 */
void
update_jobs_cant_run(int pbs_sd, resource_resv **resresv_arr,
	resource_resv *start, struct schd_error *err, int start_where)
{
	int i = 0;

	if (resresv_arr == NULL)
		return;

	/* We are not starting at the front of the array, so we need to find the
	 * element to start with.
	 */
	if (start != NULL) {
		for (; resresv_arr[i] != NULL && resresv_arr[i] != start; i++)
			;
	} else
		i = 0;

	if (resresv_arr[i] != NULL) {
		if (start_where == START_BEFORE_JOB)
			i--;
		else if (start_where == START_AFTER_JOB)
			i++;

		for (; resresv_arr[i] != NULL; i++) {
			if (!resresv_arr[i]->can_not_run) {
				update_job_can_not_run(pbs_sd, resresv_arr[i], err);
			}
		}
	}
}

/**
 * @brief
 *		translate failure codes into a comment and log message
 *
 * @param[in]	err	-	error reply structure to translate
 * @param[out]	comment_msg	-	translated comment (may be NULL)
 * @param[out]	log_msg	-	translated log message (may be NULL)
 *
 * @return	int
 * @retval	1	: comment and log messages were set
 * @retval	0	: comment and log messages were not set
 *
 */
int
translate_fail_code(schd_error *err, char *comment_msg, char *log_msg)
{
	int rc = 1;
	char commentbuf[MAX_LOG_SIZE];
	const char *arg1;
	const char *arg2;
	const char *arg3;
	const char *spec;

	if (err == NULL)
		return 0;

	if(err->status_code == SCHD_UNKWN) {
		if(comment_msg != NULL)
			comment_msg[0] = '\0';
		if(log_msg != NULL)
			log_msg[0] = '\0';
		return 0;
	}

	if (err->error_code < RET_BASE) {
		const char *pbse;

		if (err->specmsg != NULL)
			pbse = err->specmsg;
		else
			pbse = pbse_to_txt(err->error_code);

		if (pbse == NULL)
			pbse = "";

		if (comment_msg != NULL)
			snprintf(commentbuf, sizeof(commentbuf), "%s", pbse);
		if (log_msg != NULL)
			snprintf(log_msg, MAX_LOG_SIZE, "%s", pbse);
	}

	arg1 = err->arg1;
	arg2 = err->arg2;
	arg3 = err->arg3;
	spec = err->specmsg;
	if(arg1 == NULL)
		arg1 = "";
	if(arg2 == NULL)
		arg2 = "";
	if(arg3 == NULL)
		arg3 = "";
	if(spec == NULL)
		spec = "";

	switch (err->error_code) {
		case ERR_SPECIAL:

			if (comment_msg != NULL)
				snprintf(commentbuf, sizeof(commentbuf), "%s", spec);
			if (log_msg != NULL)
				snprintf(log_msg, MAX_LOG_SIZE, "%s", spec);
			break;

		/* codes using no args */
		case MAX_RUN_SUBJOBS:
		case BACKFILL_CONFLICT:
		case CANT_PREEMPT_ENOUGH_WORK:
		case CROSS_DED_TIME_BOUNDRY:
		case DED_TIME:
		case NODE_GROUP_LIMIT_REACHED:
		case NODE_JOB_LIMIT_REACHED:
		case NODE_NO_MULT_JOBS:
		case NODE_PLACE_PACK:
		case NODE_RESV_ENABLE:
		case NODE_UNLICENSED:
		case NODE_USER_LIMIT_REACHED:
		case NONPRIME_ONLY:
		case NOT_ENOUGH_NODES_AVAIL:
		case NO_FAIRSHARES:
		case NO_NODE_RESOURCES:
		case NO_SMALL_CPUSETS:
		case PRIME_ONLY:
		case QUEUE_NOT_STARTED:
		case RESERVATION_CONFLICT:
		case SCHD_ERROR:
		case SERVER_GROUP_LIMIT_REACHED:
		case SERVER_PROJECT_LIMIT_REACHED:
		case SERVER_JOB_LIMIT_REACHED:
		case SERVER_USER_LIMIT_REACHED:
		case STRICT_ORDERING:
		case PROV_DISABLE_ON_SERVER:
		case PROV_DISABLE_ON_NODE:
		case PROV_BACKFILL_CONFLICT:
		case CANT_SPAN_PSET:
		case IS_MULTI_VNODE:
		case PROV_RESRESV_CONFLICT:
		case NO_FREE_NODES:
		case NO_TOTAL_NODES:
		case JOB_UNDER_THRESHOLD:
#ifdef NAS
			/* localmod 034 */
		case GROUP_CPU_SHARE:
		case GROUP_CPU_INSUFFICIENT:
			/* localmod 998 */
		case RESOURCES_INSUFFICIENT:
#endif
			if (comment_msg != NULL)
				snprintf(commentbuf, sizeof(commentbuf), "%s", ERR2COMMENT(err->error_code));
			if (log_msg != NULL)
				snprintf(log_msg, MAX_LOG_SIZE, "%s", ERR2INFO(err->error_code));
			break;
		/* codes using arg1  */
#ifndef NAS /* localmod 031 */
		case INVALID_NODE_STATE:
#endif /* localmod 031 */
		case INVALID_NODE_TYPE:
		case NODE_NOT_EXCL:
		case QUEUE_GROUP_LIMIT_REACHED:
		case QUEUE_PROJECT_LIMIT_REACHED:
		case QUEUE_JOB_LIMIT_REACHED:
		case QUEUE_USER_LIMIT_REACHED:
		case SERVER_BYGROUP_JOB_LIMIT_REACHED:
		case SERVER_BYPROJECT_JOB_LIMIT_REACHED:
		case SERVER_BYUSER_JOB_LIMIT_REACHED:
		case AOE_NOT_AVALBL:
		case EOE_NOT_AVALBL:
		case CROSS_PRIME_BOUNDARY:
		case INVALID_RESRESV:
			if (comment_msg != NULL)
				snprintf(commentbuf, sizeof(commentbuf), ERR2COMMENT(err->error_code), arg1);
			if (log_msg != NULL)
				snprintf(log_msg, MAX_LOG_SIZE, ERR2INFO(err->error_code), arg1);
			break;

		/* codes using two arguments */
#ifdef NAS /* localmod 031 */
		case INVALID_NODE_STATE:
#endif /* localmod 031 */
		case QUEUE_BYGROUP_JOB_LIMIT_REACHED:
		case QUEUE_BYPROJECT_JOB_LIMIT_REACHED:
		case QUEUE_BYUSER_JOB_LIMIT_REACHED:
		case RUN_FAILURE:
		case NODE_NONEXISTENT:
		case SET_TOO_SMALL:
			if (comment_msg != NULL) {
				snprintf(commentbuf, sizeof(commentbuf), ERR2COMMENT(err->error_code), arg1, arg2);
			}
			if (log_msg != NULL) {
				snprintf(log_msg, MAX_LOG_SIZE, ERR2INFO(err->error_code), arg1, arg2);
			}
			break;
		/* codes using a resource definition and arg1 */
		case QUEUE_GROUP_RES_LIMIT_REACHED:
		case QUEUE_PROJECT_RES_LIMIT_REACHED:
		case QUEUE_USER_RES_LIMIT_REACHED:
			if (comment_msg != NULL && err->rdef != NULL)
				snprintf(commentbuf, sizeof(commentbuf), ERR2COMMENT(err->error_code), arg1, err->rdef->name.c_str());
			if (log_msg != NULL && err->rdef != NULL)
				snprintf(log_msg, MAX_LOG_SIZE, ERR2INFO(err->error_code), arg1, err->rdef->name.c_str());
			break;

		/* codes using resource definition in error structure */
		case SERVER_GROUP_RES_LIMIT_REACHED:
		case SERVER_PROJECT_RES_LIMIT_REACHED:
		case SERVER_USER_RES_LIMIT_REACHED:
		case SERVER_RESOURCE_LIMIT_REACHED:
			if (comment_msg != NULL && err->rdef != NULL)
				snprintf(commentbuf, sizeof(commentbuf), ERR2COMMENT(err->error_code), err->rdef->name.c_str());
			if (log_msg != NULL && err->rdef != NULL)
				snprintf(log_msg, MAX_LOG_SIZE, ERR2INFO(err->error_code), err->rdef->name.c_str());
			break;

		/* codes using a resource definition and arg1 in a different order */
		case QUEUE_RESOURCE_LIMIT_REACHED:
		case INSUFFICIENT_QUEUE_RESOURCE:
		case INSUFFICIENT_SERVER_RESOURCE:
		case INSUFFICIENT_RESOURCE:
			if (comment_msg != NULL && err->rdef != NULL)
				snprintf(commentbuf, sizeof(commentbuf), ERR2COMMENT(err->error_code), err->rdef->name.c_str(),
					arg1 == NULL ? "" : arg1);
			if (log_msg != NULL && err->rdef != NULL)
				snprintf(log_msg, MAX_LOG_SIZE, ERR2INFO(err->error_code), err->rdef->name.c_str(),
					arg1 == NULL ? "" : arg1);
			break;

		/* codes using arg1, arg3 and resource definition (in a weird order) */
		case QUEUE_BYGROUP_RES_LIMIT_REACHED:
		case QUEUE_BYPROJECT_RES_LIMIT_REACHED:
		case QUEUE_BYUSER_RES_LIMIT_REACHED:
			if (comment_msg != NULL && err->rdef != NULL)
				snprintf(commentbuf, sizeof(commentbuf), ERR2COMMENT(err->error_code), arg3, err->rdef->name.c_str(), arg1);
			if (log_msg != NULL && err->rdef != NULL)
				snprintf(log_msg, MAX_LOG_SIZE, ERR2INFO(err->error_code), arg3, err->rdef->name.c_str(), arg1);
			break;

		/* codes using resource definition and arg2 */
		case SERVER_BYGROUP_RES_LIMIT_REACHED:
		case SERVER_BYPROJECT_RES_LIMIT_REACHED:
		case SERVER_BYUSER_RES_LIMIT_REACHED:
			if (comment_msg != NULL && err->rdef != NULL)
				snprintf(commentbuf, sizeof(commentbuf), ERR2COMMENT(err->error_code), arg2,
					err->rdef->name.c_str());
			if (log_msg != NULL && err->rdef != NULL)
				snprintf(log_msg, MAX_LOG_SIZE, ERR2INFO(err->error_code), arg2,
					err->rdef->name.c_str());
			break;

		case RESERVATION_INTERFERENCE:
			if (*arg1 !='\0') {
				if (comment_msg != NULL) {
					sprintf(commentbuf, "%s: %s",
						ERR2COMMENT(err->error_code), arg1);
				}
				if (log_msg != NULL) {
					snprintf(log_msg, MAX_LOG_SIZE, "%s: %s",
						ERR2INFO(err->error_code), arg1);
				}
			}
			else  {
				if (comment_msg != NULL)
					snprintf(commentbuf, sizeof(commentbuf), "%s", ERR2COMMENT(err->error_code));
				if (log_msg != NULL)
					snprintf(log_msg, MAX_LOG_SIZE, "%s", ERR2INFO(err->error_code));
			}
			break;

		case NOT_QUEUED:
		default:
			rc = 0;
			if (comment_msg != NULL)
				commentbuf[0] = '\0';
			if (log_msg != NULL)
				log_msg[0] = '\0';
	}

	if (comment_msg != NULL) {
		/* snprintf() use MAX_LOG_SIZE because all calls to this function
		 * pass in comment_msg buffers of size MAX_LOG_SIZE.  This needs to be
		 * fixed by passing in the size of comment_msg and log_msg (SPID268659)
		 */
		switch (err->status_code) {
			case SCHD_UNKWN:
			case NOT_RUN:
				snprintf(comment_msg, MAX_LOG_SIZE, "%s: %.*s",
					NOT_RUN_PREFIX,
					(int)(MAX_LOG_SIZE - strlen(NOT_RUN_PREFIX) - 3),
					commentbuf);
				break;
			case NEVER_RUN:
				snprintf(comment_msg, MAX_LOG_SIZE, "%s: %.*s",
					NEVER_RUN_PREFIX,
					(int)(MAX_LOG_SIZE - strlen(NEVER_RUN_PREFIX) - 3),
					commentbuf);
				break;
			default:
				snprintf(comment_msg, MAX_LOG_SIZE, "%s", commentbuf);
		}
	}

	return rc;
}

/**
 * @brief resresv_set constructor
 */
resresv_set *
new_resresv_set(void)
{
	resresv_set *rset;

	rset = static_cast<resresv_set *>(malloc(sizeof(resresv_set)));
	if (rset == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	rset->can_not_run = 0;
	rset->err = NULL;
	rset->user = NULL;
	rset->group = NULL;
	rset->project = NULL;
	rset->place_spec = NULL;
	rset->req = NULL;
	rset->select_spec = NULL;
	rset->qinfo = NULL;

	return rset;
}
/**
 * @brief resresv_set destructor
 */
void
free_resresv_set(resresv_set *rset) {
	if(rset == NULL)
		return;

	free_schd_error(rset->err);
	free(rset->user);
	free(rset->group);
	free(rset->project);
	delete rset->select_spec;
	free_place(rset->place_spec);
	free_resource_req_list(rset->req);
	free(rset);
}
/**
 *  @brief resresv_set array destructor
 */
void
free_resresv_set_array(resresv_set **rsets) {
	int i;

	if (rsets == NULL)
		return;

	for (i = 0; rsets[i] != NULL; i++)
		free_resresv_set(rsets[i]);

	free(rsets);
}

/**
 * @brief resresv_set copy constructor
 */
resresv_set *
dup_resresv_set(resresv_set *oset, server_info *nsinfo)
{
	resresv_set *rset;

	if (oset == NULL || nsinfo == NULL)
		return NULL;

	rset = new_resresv_set();
	if (rset == NULL)
		return NULL;

	rset->can_not_run = oset->can_not_run;

	rset->err = dup_schd_error(oset->err);
	if (oset->err != NULL && oset->err == NULL) {
		free_resresv_set(rset);
		return NULL;
	}

	rset->user = string_dup(oset->user);
	if (oset->user != NULL && rset->user == NULL) {
		free_resresv_set(rset);
		return NULL;
	}
	rset->group = string_dup(oset->group);
	if (oset->group != NULL && rset->group == NULL) {
		free_resresv_set(rset);
		return NULL;
	}
	rset->project = string_dup(oset->project);
	if (oset->project != NULL && rset->project == NULL) {
		free_resresv_set(rset);
		return NULL;
	}
	rset->select_spec = new selspec(*oset->select_spec);
	if (rset->select_spec == NULL) {
		free_resresv_set(rset);
		return NULL;
	}
	rset->place_spec = dup_place(oset->place_spec);
	if (rset->place_spec == NULL) {
		free_resresv_set(rset);
		return NULL;
	}
	rset->req = dup_resource_req_list(oset->req);
	if (oset->req != NULL && rset->req == NULL) {
		free_resresv_set(rset);
		return NULL;
	}
	if(oset->qinfo != NULL)
		rset->qinfo = find_queue_info(nsinfo->queues, oset->qinfo->name);

	return rset;
}
/**
 * @brief resresv_set array copy constructor
 */
resresv_set **
dup_resresv_set_array(resresv_set **osets, server_info *nsinfo)
{
	int i;
	int len;
	resresv_set **rsets;
	if (osets == NULL || nsinfo == NULL)
		return NULL;

	len = count_array(osets);

	rsets = static_cast<resresv_set **>(malloc((len + 1) * sizeof(resresv_set *)));
	if (rsets == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	for (i = 0; osets[i] != NULL; i++) {
		rsets[i] = dup_resresv_set(osets[i], nsinfo);
		if (rsets[i] == NULL) {
			free_resresv_set_array(rsets);
			return NULL;
		}
	}
	rsets[i] = NULL;
	return rsets;

}

/**
 * @brief should a resresv_set use the user
 * @param sinfo - server info
 * @param qinfo - queue info
 * @retval 1 - yes
 * @retval 0 - no
 */
int
resresv_set_use_user(server_info *sinfo, queue_info *qinfo)
{
	if ((sinfo != NULL) && (sinfo->has_user_limit))
		return 1;
	if ((qinfo != NULL) && (qinfo->has_user_limit))
		return 1;


	return 0;
}

/**
 * @brief should a resresv_set use the group
 * @param sinfo - server info
 * @param qinfo - queue info
 * @retval 1 - yes
 * @retval 0 - no
 */
int
resresv_set_use_grp(server_info *sinfo, queue_info *qinfo)
{
	if ((sinfo != NULL) && (sinfo->has_grp_limit))
		return 1;
	if ((qinfo != NULL) && (qinfo->has_grp_limit))
		return 1;


	return 0;
}

/**
 * @brief should a resresv_set use the project
 * @param sinfo - server info
 * @param qinfo - queue info
 * @retval 1 - yes
 * @retval 0 - no
 */
int
resresv_set_use_proj(server_info *sinfo, queue_info *qinfo)
{
	if ((sinfo != NULL) && (sinfo->has_proj_limit))
		return 1;
	if ((qinfo != NULL) && (qinfo->has_proj_limit))
		return 1;


	return 0;
}

/**
 * @brief should a resresv_set use the queue
 * 	A resresv_set should use queue for the following reasons:
 * 	Hard limits	max_run_res, etc
 * 	Soft Limits	max_run_res_soft, etc
 * 	Nodes		Queue has nodes(e.g., node's queue attribute)
 * 	Dedtime queue 	Queue is a dedicated time queue
 * 	Primetime	Queue is a primetime queue
 * 	Non-primetime	Queue is a non-primetime queue
 * 	Resource limits	Queue has resources_available limits
 * 	Reservation	Queue is a reservation queue
 *
 * @param qinfo - the queue
 * @retval 1 - yes
 * @retval 0 - no
 */
int
resresv_set_use_queue(queue_info *qinfo)
{
	if (qinfo == NULL)
		return 0;

	if (qinfo->has_hard_limit || qinfo->has_soft_limit || qinfo->has_nodes ||
	    qinfo->is_ded_queue || qinfo->is_prime_queue || qinfo->is_nonprime_queue ||
	    qinfo->has_resav_limit || qinfo->resv != NULL)
		return 1;

	return 0;
}

/**
 * @brief determine which selspec to use from a resource_resv for a resresv_set
 *
 * @par Jobs that have an execselect are either running or need to be placed
 *	back on the nodes they were originally running on (e.g., suspended jobs).
 *	We need to put them in their own set because they are no longer
 *	requesting the same resources as jobs with the same select spec.
 *	They are requesting the resources on each vnode they are running on.
 *	We don't care about running jobs because the only time they will be
 *	looked at is if they are requeued.  At that point they are back in
 *	the queued state and have the same select spec as they originally did.
 *
 * @return selspec *
 * @retval selspec to use
 * @retval NULL on error
 */
selspec *
resresv_set_which_selspec(resource_resv *resresv)
{
	if(resresv == NULL)
		return NULL;

	if (resresv->job != NULL && !resresv->job->is_running && resresv->execselect != NULL)
		return resresv->execselect;

	return resresv->select;
}

/**
 * @brief create the list of resources to consider when creating the resresv sets
 * @param policy[in] - policy info
 * @param sinfo[in] - server universe
 * @return resdef **
 * @retval array of resdefs for creating resresv_set's resources.
 * @retval NULL on error
 */
std::unordered_set<resdef *>
create_resresv_sets_resdef(status *policy) {
	std::unordered_set<resdef *> defs;
	schd_resource *limres;

	if (policy == NULL)
		return {};

	limres = query_limres();

	defs = policy->resdef_to_check;
	defs.insert(allres["cput"]);
	defs.insert(allres["walltime"]);
	defs.insert(allres["max_walltime"]);
	defs.insert(allres["min_walltime"]);
	if(sc_attrs.preempt_targets_enable)
		defs.insert(allres["preempt_targets"]);

	for (auto cur_res = limres; cur_res != NULL; cur_res = cur_res->next)
			defs.insert(cur_res->def);

	return defs;
}

/**
 * @brief create a resresv_set based on a resource_resv
 *
 * @param[in] policy - policy info
 * @param[in] sinfo - server info
 * @param[in] resresv - resresv to create resresv set from
 *
 * @return resresv_set **
 * @retval newly created resresv_set
 * @retval NULL on error
 */
resresv_set *
create_resresv_set_by_resresv(status *policy, server_info *sinfo, resource_resv *resresv)
{
	resresv_set *rset;
	if (policy == NULL || resresv == NULL)
		return NULL;

	rset = new_resresv_set();
	if (rset == NULL)
		return NULL;

	if (resresv->is_job && resresv->job != NULL) {
		if (resresv_set_use_queue(resresv->job->queue))
			rset->qinfo = resresv->job->queue;
	}

	if (resresv_set_use_user(sinfo, rset->qinfo))
		rset->user = string_dup(resresv->user);
	if (resresv_set_use_grp(sinfo, rset->qinfo))
		rset->group = string_dup(resresv->group);
	if (resresv_set_use_proj(sinfo, rset->qinfo))
		rset->project = string_dup(resresv->project);

	rset->select_spec = new selspec(*resresv_set_which_selspec(resresv));
	if (rset->select_spec == NULL) {
		free_resresv_set(rset);
		return NULL;
	}
	rset->place_spec = dup_place(resresv->place_spec);
	if (rset->place_spec == NULL) {
		free_resresv_set(rset);
		return NULL;
	}
	/* rset->req may be NULL if the intersection of resresv->resreq and policy->equiv_class_resdef is the NULL set */
	rset->req = dup_selective_resource_req_list(resresv->resreq, policy->equiv_class_resdef);


	return rset;
}

/**
 * @brief find the index of a resresv_set by its component parts
 * @par qinfo, user, group, project, or req can be NULL if the resresv_set does not have one
 * @param[in] policy - policy info
 * @param[in] rsets - resresv_sets to search
 * @param[in] user - user name
 * @param[in] group - group name
 * @param[in] project - project name
 * @param[in] sel - select spec
 * @param[in] pl - place spec
 * @param[in] req - list of resources (i.e., qsub -l)
 * @param[in] qinfo - queue
 * @return int
 * @retval index of resresv if found
 * @retval -1 if not found or on error
 */
int
find_resresv_set(status *policy, resresv_set **rsets, char *user, char *group, char *project, selspec *sel, place *pl, resource_req *req, queue_info *qinfo)
{
	int i;

	if (rsets == NULL)
		return -1;

	for (i = 0; rsets[i] != NULL; i++) {
		if ((qinfo != NULL && rsets[i]->qinfo == NULL) || (qinfo == NULL && rsets[i]->qinfo != NULL))
			continue;
		if ((qinfo != NULL && rsets[i]->qinfo != NULL) && qinfo->name != rsets[i]->qinfo->name)

			continue;
		if ((user != NULL && rsets[i]->user == NULL) || (user == NULL && rsets[i]->user != NULL))
			continue;
		if (user != NULL && cstrcmp(user, rsets[i]->user) != 0)
			continue;

		if ((group != NULL && rsets[i]->group == NULL) || (group == NULL && rsets[i]->group != NULL))
			continue;
		if (group != NULL && cstrcmp(group, rsets[i]->group) != 0)
			continue;

		if ((project != NULL && rsets[i]->project == NULL) || (project == NULL && rsets[i]->project != NULL))
			continue;
		if (project != NULL && cstrcmp(project, rsets[i]->project) != 0)
			continue;

		if (compare_selspec(rsets[i]->select_spec, sel) == 0)
			continue;
		if (compare_place(rsets[i]->place_spec, pl) == 0)
			continue;
		if (compare_resource_req_list(rsets[i]->req, req, policy->equiv_class_resdef) == 0)
			continue;
		/* If we got here, we have found our set */
		return i;
	}
	return -1;

}

/**
 * @brief find the index of a resresv_set by a resresv inside it
 * @param[in] policy - policy info
 * @param[in] rsets - resresv_set array to search
 * @param[in] resresv - resresv to search for
 * @return index of resresv
 */
int
find_resresv_set_by_resresv(status *policy, resresv_set **rsets, resource_resv *resresv)
{
	char *user = NULL;
	char *grp = NULL;
	char *proj = NULL;
	queue_info *qinfo = NULL;
	selspec *sspec;

	if (policy == NULL || rsets == NULL || resresv == NULL)
		return -1;

	if (resresv->is_job && resresv->job != NULL)
		if (resresv_set_use_queue(resresv->job->queue))
			qinfo = resresv->job->queue;

	if (resresv_set_use_user(resresv->server, qinfo))
		user = resresv->user;

	if (resresv_set_use_grp(resresv->server, qinfo))
		grp = resresv->group;

	if (resresv_set_use_proj(resresv->server, qinfo))
		proj = resresv->project;

	sspec = resresv_set_which_selspec(resresv);

	return find_resresv_set(policy, rsets, user, grp, proj, sspec, resresv->place_spec, resresv->resreq, qinfo);
}

/**
 * @brief create equivalence classes based on an array of resresvs
 * @param[in] policy - policy info
 * @param[in] sinfo - server universe
 * @return array of equivalence classes (resresv_sets)
 */
resresv_set **
create_resresv_sets(status *policy, server_info *sinfo)
{
	int i;
	int j = 0;
	int len;
	int rset_len;
	resource_resv **resresvs;
	resresv_set **rsets;
	resresv_set **tmp_rset_arr;
	resresv_set *cur_rset;

	if (policy == NULL || sinfo == NULL)
		return NULL;

	resresvs = sinfo->jobs;

	len = count_array(resresvs);
	rsets = static_cast<resresv_set **>(malloc((len + 1) * sizeof(resresv_set *)));
	if (rsets == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	rsets[0] = NULL;

	for (i = 0; resresvs[i] != NULL; i++) {
		auto cur_ind = find_resresv_set_by_resresv(policy, rsets, resresvs[i]);

		/* Didn't find the set, create it.*/
		if (cur_ind == -1) {
			cur_rset = create_resresv_set_by_resresv(policy, sinfo, resresvs[i]);
			if (cur_rset == NULL) {
				free_resresv_set_array(rsets);
				return NULL;
			}
			cur_ind = j;
			rsets[j++] = cur_rset;
			rsets[j] = NULL;
		}
		resresvs[i]->ec_index = cur_ind;
	}

	tmp_rset_arr = static_cast<resresv_set **>(realloc(rsets,(j + 1) * sizeof(resresv_set *)));
	if (tmp_rset_arr != NULL)
		rsets = tmp_rset_arr;
	rset_len = count_array(rsets);
	if (rset_len > 0) {
		log_eventf(PBSEVENT_DEBUG3, PBS_EVENTCLASS_SCHED, LOG_DEBUG, __func__,
			"Number of job equivalence classes: %d", rset_len);
	}

	return rsets;
}

/**
 * @brief
 * 		job_info copy constructor
 *
 * @param[in]	ojinfo	-	Pointer to JobInfo structure
 * @param[in]	nqinfo	-	Queue Info
 * @param[in]	sinfo	-	Server Info
 *
 * @return	job_info *
 * @retval	NULL	:	when function fails to duplicate job_info
 * @retval	!NULL	:	duplicated job_info structure pointer
 */
job_info *
dup_job_info(job_info *ojinfo, queue_info *nqinfo, server_info *nsinfo)
{
	job_info *njinfo;

	if ((njinfo = new_job_info()) == NULL)
		return NULL;

	njinfo->queue = nqinfo;
	njinfo->is_queued = ojinfo->is_queued;
	njinfo->is_running = ojinfo->is_running;
	njinfo->is_held = ojinfo->is_held;
	njinfo->is_waiting = ojinfo->is_waiting;
	njinfo->is_transit = ojinfo->is_transit;
	njinfo->is_exiting = ojinfo->is_exiting;
	njinfo->is_userbusy = ojinfo->is_userbusy;
	njinfo->is_begin = ojinfo->is_begin;
	njinfo->is_expired = ojinfo->is_expired;
	njinfo->is_suspended = ojinfo->is_suspended;
	njinfo->is_susp_sched = ojinfo->is_susp_sched;
	njinfo->is_array = ojinfo->is_array;
	njinfo->is_subjob = ojinfo->is_subjob;
	njinfo->is_prerunning = ojinfo->is_prerunning;
	njinfo->can_not_preempt = ojinfo->can_not_preempt;
	njinfo->topjob_ineligible = ojinfo->topjob_ineligible;
	njinfo->is_checkpointed = ojinfo->is_checkpointed;
	njinfo->is_provisioning = ojinfo->is_provisioning;

	njinfo->can_checkpoint = ojinfo->can_checkpoint;
	njinfo->can_requeue    = ojinfo->can_requeue;
	njinfo->can_suspend    = ojinfo->can_suspend;

	njinfo->priority = ojinfo->priority;
	njinfo->etime = ojinfo->etime;
	njinfo->stime = ojinfo->stime;
	njinfo->preempt = ojinfo->preempt;
	njinfo->preempt_status = ojinfo->preempt_status;
	njinfo->peer_sd = ojinfo->peer_sd;
	njinfo->est_start_time = ojinfo->est_start_time;
	njinfo->formula_value = ojinfo->formula_value;
	njinfo->est_execvnode = string_dup(ojinfo->est_execvnode);
	njinfo->job_name = string_dup(ojinfo->job_name);
	njinfo->comment = string_dup(ojinfo->comment);
	njinfo->resv_id = string_dup(ojinfo->resv_id);
	njinfo->alt_id = string_dup(ojinfo->alt_id);

	if (ojinfo->resv != NULL) {
		njinfo->resv = find_resource_resv_by_indrank(nqinfo->server->resvs,
			ojinfo->resv->resresv_ind, ojinfo->resv->rank);
	}

	njinfo->resused = dup_resource_req_list(ojinfo->resused);

	njinfo->array_index = ojinfo->array_index;
	njinfo->array_id = ojinfo->array_id;
	njinfo->queued_subjobs = dup_range_list(ojinfo->queued_subjobs);
	njinfo->max_run_subjobs = ojinfo->max_run_subjobs;

	njinfo->resreleased = dup_nspecs(ojinfo->resreleased, nsinfo->nodes, NULL);
	njinfo->resreq_rel = dup_resource_req_list(ojinfo->resreq_rel);

	if (nqinfo->server->fstree !=NULL) {
		njinfo->ginfo = find_group_info(ojinfo->ginfo->name,
			nqinfo->server->fstree->root);
	}
	else
		njinfo->ginfo = NULL;

	njinfo->depend_job_str = string_dup(njinfo->depend_job_str);

#ifdef RESC_SPEC
	njinfo->rspec = dup_rescspec(ojinfo->rspec);
#endif

#ifdef NAS
	/* localmod 045 */
	njinfo->NAS_pri = ojinfo->NAS_pri;
	/* localmod 034 */
	njinfo->sh_amts = site_dup_share_amts(ojinfo->sh_amts);
	njinfo->sh_info = ojinfo->sh_info;
	njinfo->accrue_rate = ojinfo->accrue_rate;
	/* localmod 040 */
	njinfo->nodect = ojinfo->nodect;
	/* localmod 031 */
	njinfo->schedsel = string_dup(ojinfo->schedsel);
	/* localmod 053 */
	njinfo->u_info = ojinfo->u_info;
#endif

	return njinfo;
}

/**
 * @brief
 * 		filter function used with resource_resv_filter
 *        create limited running job set for use with preemption.
 *        If there are multiple resources found in preempt_targets
 *        the scheduler will select a preemptable job which satisfies
 *        any one of them.
 *
 * @see	resource_resv_filter()
 *
 * @param[in]	job	-	job to consider to include
 * @param[in]	arg	-	attribute=value pairs criteria for inclusion
 *
 * @retval	int
 * @return	1	: If job falls into one of the preempt_targets
 * @return	0	: If job dos not fall into any of the preempt_targets
 */
int
preempt_job_set_filter(resource_resv *job, const void *arg)
{
	resource_req *req;
	char **arglist;
	char *p;
	char *dot;
	int i;

	if (job == NULL || arg == NULL || job->job == NULL ||
		job->job->queue == NULL || job->job->is_running != 1)
		return 0;

	arglist = (char **) arg;

	for (i = 0; arglist[i] != NULL; i++) {
		p = strpbrk(arglist[i], ".=");
		if (p != NULL) {
			/* two valid attributes: queue and Resource_List.<res> */
			if (!strncasecmp(arglist[i], ATTR_queue, p - arglist[i])) {
				if (job->job->queue->name == p + 1)
					return 1;
			}
			else if (!strncasecmp(arglist[i], ATTR_l, p - arglist[i])) {
				dot = p;
				p = strpbrk(arglist[i], "=");
				if (p == NULL)
					return 0;
				else {
					*p = '\0';
					req = find_resource_req_by_str(job->resreq, dot+1);
					*p = '=';
					if (req != NULL) {
						if (!strcmp(req->res_str, p+1))
							return 1;
					}
				}
			}
		}
	}
	return 0;
}

/**
 * @brief
 *  get_job_req_used_time - get a running job's req and used time for preemption
 *
 * @param[in]	pjob - the job in question
 * @param[out]	rtime - return pointer to the requested time
 * @param[out]	utime - return pointer to the used time
 *
 * @return	int
 * @retval	0 for success
 * @retval	1 for error
 */
static int
get_job_req_used_time(resource_resv *pjob, int *rtime, int *utime)
{
	resource_req *req; /* the jobs requested soft_walltime/walltime/cput */
	resource_req *used; /* the amount of the walltime/cput used */

	if (pjob == NULL || pjob->job == NULL || !pjob->job->is_running
			|| rtime == NULL || utime == NULL)
		return 1;

	req = find_resource_req(pjob->resreq, allres["soft_walltime"]);

	if (req == NULL)
		req = find_resource_req(pjob->resreq, allres["walltime"]);

	if (req == NULL) {
		req = find_resource_req(pjob->resreq, allres["cput"]);
		used = find_resource_req(pjob->job->resused, allres["cput"]);;
	} else
		used = find_resource_req(pjob->job->resused, allres["walltime"]);

	if (req != NULL && used != NULL) {
		*rtime = req->amount;
		*utime = used->amount;
	} else {
		*rtime = -1;
		*utime = -1;
	}

	return 0;
}

/**
 * @brief
 *  schd_get_preempt_order - deduce the preemption ordering to be used for a job
 *
 * @param[in]	pjob	-	the job to preempt
 * @param[in]	sinfo	-	Pointer to server info structure.
 *
 * @return	: struct preempt_ordering.  array containing preemption order
 *
 */
struct preempt_ordering *schd_get_preempt_order(resource_resv *resresv)
{
	struct preempt_ordering *po = NULL;
	int req = -1;
	int used = -1;

	if (get_job_req_used_time(resresv, &req, &used) != 0)
		return NULL;

	po = get_preemption_order(sc_attrs.preempt_order, req, used);

	return po;
}

/**
 * @brief
 * 		find the jobs to preempt and then preempt them
 *
 * @param[in]	policy	-	policy info
 * @param[in]	pbs_sd	-	communication descriptor to the PBS server
 * @param[in]	hjob	-	the high priority job
 * @param[in]	sinfo	-	the server to find jobs to preempt
 * @param[out]	err	-	schd_error to return error from runjob
 *
 * @return	int
 * @retval	1	: success
 * @retval  0	: failure
 * @retval -1	: error
 *
 */
int
find_and_preempt_jobs(status *policy, int pbs_sd, resource_resv *hjob, server_info *sinfo, schd_error *err)
{

	int i = 0;
	int *jobs = NULL;
	resource_resv *job = NULL;
	int done = 0;
	int rc = 1;
	int *preempted_list = NULL;
	int preempted_count = 0;
	int *fail_list = NULL;
	int fail_count = 0;
	int num_tries = 0;
	int no_of_jobs = 0;
	char **preempt_jobs_list = NULL;
	preempt_job_info *preempt_jobs_reply = NULL;

	/* jobs with AOE cannot preempt (atleast for now) */
	if (hjob->aoename != NULL)
		return 0;

	/* using calloc - saves the trouble to put NULL at end of list */
	if ((preempted_list = static_cast<int *>(calloc((sinfo->sc.running + 1), sizeof(int)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return -1;
	}

	if ((fail_list = static_cast<int *>(calloc((sinfo->sc.running + 1), sizeof(int)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		free(preempted_list);
		return -1;
	}

	/* loop till done is true, ie, all selected jobs are truely preempted,
	 * or we cant find enough jobs to preempt
	 * or the maximum number of tries has been exhausted
	 */
	while (!done &&
		((jobs = find_jobs_to_preempt(policy, hjob, sinfo, fail_list, &no_of_jobs)) != NULL) &&
		num_tries < MAX_PREEMPT_RETRIES) {
		done = 1;

		if ((preempt_jobs_list = static_cast<char **>(calloc(no_of_jobs + 1, sizeof(char *)))) == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			free(preempted_list);
			free(fail_list);
			return -1;
		}

		for (i = 0; i < no_of_jobs; i++) {
			job = find_resource_resv_by_indrank(sinfo->running_jobs, -1, jobs[i]);
			if (job != NULL) {
				if ((preempt_jobs_list[i] = string_dup(job->name.c_str())) == NULL) {
					log_err(errno, __func__, MEM_ERR_MSG);
					free_string_array(preempt_jobs_list);
					free(preempt_jobs_list);
					free(preempted_list);
					free(fail_list);
					return -1;
				}
			}
		}

		if ((preempt_jobs_reply = send_preempt_jobs(pbs_sd, preempt_jobs_list)) == NULL) {
			free_string_array(preempt_jobs_list);
			free(preempted_list);
			free(fail_list);
			return -1;
		}

		for (i = 0; i < no_of_jobs; i++) {
			job = find_resource_resv(sinfo->running_jobs, preempt_jobs_reply[i].job_id);
			if (job == NULL) {
				log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG, preempt_jobs_reply[i].job_id,
					"Server replied to preemption request with job which does not exist.");
				continue;
			}

			if (preempt_jobs_reply[i].order[0] == '0') {
				done = 0;
				fail_list[fail_count++] = job->rank;
				log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO, job->name, "Job failed to be preempted");
			}
			else {
				int update_accrue_type = 1;
				preempted_list[preempted_count++] = job->rank;
				if (preempt_jobs_reply[i].order[0] == 'S') {
					if (!policy->rel_on_susp.empty()) {
						/* Set resources_released and execselect on the job */
						create_res_released(policy, job);
					}
					update_universe_on_end(policy, job, "S", NO_FLAGS);
					job->job->is_susp_sched = 1;
					log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO,
						job->name, "Job preempted by suspension");
					/* Since suspended job is not part of its current equivalence class,
					 * break the job's association with its equivalence class.
					 */
					job->ec_index= UNSPECIFIED;
				} else if (preempt_jobs_reply[i].order[0] == 'C') {
					job->job->is_checkpointed = 1;
					update_universe_on_end(policy, job, "Q", NO_FLAGS);
					log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO,
						job->name, "Job preempted by checkpointing");
					/* Since checkpointed job is not part of its current equivalence class,
					 * break the job's association with its equivalence class.
					 */
					job->ec_index = UNSPECIFIED;
				} else if (preempt_jobs_reply[i].order[0] == 'Q') {
					update_universe_on_end(policy, job, "Q", NO_FLAGS);
					log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO,
						job->name, "Job preempted by requeuing");
				} else {
					update_universe_on_end(policy, job, "X", NO_FLAGS);
					log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO,
						job->name, "Job preempted by deletion");
					job->can_not_run = 1;
					update_accrue_type = 0;
				}
				if (update_accrue_type)
					update_accruetype(pbs_sd, sinfo, ACCRUE_MAKE_ELIGIBLE, SUCCESS, job);
				job->job->is_preempted = 1;
				job->job->time_preempted = sinfo->server_time;
				sinfo->num_preempted++;
			}
		}

		free(jobs);
		free_string_array(preempt_jobs_list);
		free(preempt_jobs_reply);
		num_tries++;
	}

	if (done) {
		clear_schd_error(err);
		auto ret = run_update_resresv(policy, pbs_sd, sinfo, hjob->job->queue, hjob, NULL, RURR_ADD_END_EVENT, err);

		/* oops... we screwed up.. the high priority job didn't run.  Forget about
		 * running it now and resume preempted work
		 */
		if (!ret) {
			schd_error *serr;
			serr = new_schd_error();
			if(serr == NULL) {
				free(preempted_list);
				free(fail_list);
				return -1;
			}
			log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG, hjob->name,
				"Preempted work didn't run job - rerun it");
			for (i = 0; i < preempted_count; i++) {
				job = find_resource_resv_by_indrank(sinfo->jobs, -1, preempted_list[i]);
				if (job != NULL && !job->job->is_running) {
					clear_schd_error(serr);
					if (run_update_resresv(policy, pbs_sd, sinfo, job->job->queue, job, NULL, RURR_NO_FLAGS, serr) == 0) {
						schdlogerr(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG, job->name, "Failed to rerun job:", serr);
					}
				}
			}
			rc = 0;
			free_schd_error_list(serr);
		}
	}
	else if (num_tries == MAX_PREEMPT_RETRIES) {
		rc = 0;
		log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG, hjob->name,
			"Maximum number of preemption tries exceeded - cannot run job");
	}
	else
		rc = 0;

	free(preempted_list);
	free(fail_list);
	return rc;
}


/**
 * @brief
 * 		find jobs to preempt in order to run a high priority job.
 *        First we'll check if the reason the job can't run will be helped
 *        if we preempt work (i.e. job won't run because of dedtime) then
 *        we'll simulate preempting jobs to find a list which will work.
 *        We will then go back through the list to find if any work doesn't
 *        need to be preempted.  Finally we'll return the list if we found
 *        one, NULL if not.
 *
 * @param[in]	policy		-	policy info
 * @param[in]	hjob		-	the high priority job
 * @param[in]	sinfo		-	the server of the jobs to preempt
 * @param[in]	fail_list	-	list of jobs which preemption has failed
 *				 	do not attempt to preempt again
 * @param[out]	no_of_jobs	-	number of jobs in the list being returned
 *
 * @return	int *
 * @retval	array of job ranks to preempt
 * @retval	NULL	: error/no jobs
 * @par NOTE:	returned array is allocated with malloc() --  needs freeing
 *
 */
int *
find_jobs_to_preempt(status *policy, resource_resv *hjob, server_info *sinfo, int *fail_list, int *no_of_jobs)
{
	int i;
	int j = 0;
	int has_lower_jobs = 0;	/* there are jobs of a lower preempt priority */
	unsigned int prev_prio;		/* jinfo's preempt field before simulation */
	server_info *nsinfo;
	status *npolicy;
	resource_resv **rjobs = NULL;	/* the running jobs to choose from */
	resource_resv **pjobs = NULL;	/* jobs to preempt */
	resource_resv **rjobs_subset = NULL;
	int *pjobs_list = NULL;	/* list of job ids */
	resource_resv *nhjob = NULL; /* pointer to high priority job from duplicated universe */
	resource_resv *pjob = NULL;
	int rc = 0;
	int retval = 0;
	char log_buf[MAX_LOG_SIZE];
	nspec **ns_arr = NULL;
	schd_error *err = NULL;

	enum sched_error_code old_errorcode = SUCCESS;
	resdef *old_rdef = NULL;
	long indexfound;
	long skipto;
	int filter_again = 0;

	schd_error *full_err = NULL;
	schd_error *cur_err = NULL;

	resource_req *preempt_targets_req = NULL;
	char **preempt_targets_list = NULL;
	resource_resv **prjobs = NULL;
	int rjobs_count = 0;


	*no_of_jobs = 0;
	if (hjob == NULL || sinfo == NULL)
		return NULL;

	/* if the job is in an express queue and there are multiple express queues,
	 * we need see if there are any running jobs who we can preempt.  All
	 * express queues fall into the same preempt level but have different
	 * preempt priorities.
	 */
	if ((hjob->job->preempt_status & PREEMPT_TO_BIT(PREEMPT_EXPRESS)) &&
		sinfo->has_mult_express) {
		for (i = 0; sinfo->running_jobs[i] != NULL && !has_lower_jobs; i++)
			if (sinfo->running_jobs[i]->job->preempt < hjob->job->preempt)
				has_lower_jobs = TRUE;
	}
	else {
		for (i = 0; i < NUM_PPRIO && !has_lower_jobs; i++)
			if (sc_attrs.preempt_prio[i][1] < hjob->job->preempt &&
				sinfo->preempt_count[i] > 0)
				has_lower_jobs = TRUE;
	}

	if (has_lower_jobs == FALSE)
		return NULL;

	/* we increment cstat.preempt_attempts when we check, if we only did a
	 * cstat.preempt_attempts > conf.max_preempt_attempts we would actually
	 * attempt to preempt conf.max_preempt_attempts + 1 times
	 */
	if (conf.max_preempt_attempts != SCHD_INFINITY) {
		if (cstat.preempt_attempts >= conf.max_preempt_attempts) {
			log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, hjob->name,
				"Not attempting to preempt: over max cycle preempt limit");
			return NULL;
		}
		else
			cstat.preempt_attempts++;
	}


	log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, hjob->name,
		"Employing preemption to try and run high priority job.");

	/* Let's get all the reasons the job won't run now.
	 * This will help us find the set of jobs to preempt
	 */

	full_err = new_schd_error();
	if(full_err == NULL) {
		return NULL;
	}

	ns_arr = is_ok_to_run(policy, sinfo, hjob->job->queue, hjob, RETURN_ALL_ERR, full_err);

	/* This should be NULL, but just in case */
	free_nspecs(ns_arr);

	/* If a job can't run due to any of these reasons, no amount of preemption will help */
	for (cur_err = full_err; cur_err != NULL; cur_err = cur_err->next) {
		int cant_preempt = 0;
		switch((int) cur_err->error_code)
		{
			case SCHD_ERROR:
			case NOT_QUEUED:
			case QUEUE_NOT_STARTED:
			case QUEUE_NOT_EXEC:
			case DED_TIME:
			case CROSS_DED_TIME_BOUNDRY:
			case PRIME_ONLY:
			case NONPRIME_ONLY:
			case CROSS_PRIME_BOUNDARY:
			case NODE_NONEXISTENT:
			case CANT_SPAN_PSET:
			case RESERVATION_INTERFERENCE:
			case PROV_DISABLE_ON_SERVER:
			case MAX_RUN_SUBJOBS:
				cant_preempt = 1;
				break;
		}
		if(cur_err->status_code == NEVER_RUN)
			cant_preempt = 1;
		if (cant_preempt) {
			translate_fail_code(cur_err, NULL, log_buf);
			log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG, hjob->name,
				"Preempt: Can not preempt to run job: %s", log_buf);
			free_schd_error_list(full_err);
			return NULL;
		}
	}

	if ((pjobs = static_cast<resource_resv **>(malloc(sizeof(resource_resv *) * (sinfo->sc.running + 1)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		free_schd_error_list(full_err);
		return NULL;
	}
	pjobs[0] = NULL;

	if (sc_attrs.preempt_targets_enable) {
		preempt_targets_req = find_resource_req(hjob->resreq, allres["preempt_targets"]);
		if (preempt_targets_req != NULL) {

			preempt_targets_list = break_comma_list(preempt_targets_req->res_str);
			retval = check_preempt_targets_for_none(preempt_targets_list);
			if (retval == PREEMPT_NONE) {
				log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, hjob->name,
					"No preemption set specified for the job: Job will not preempt");
				free_schd_error_list(full_err);
				free(pjobs);
				free_string_array(preempt_targets_list);
				return NULL;
			}
		}
	}

	/* use locally dup'd copy of sinfo so we don't modify the original */
	if ((nsinfo = dup_server_info(sinfo)) == NULL) {
		free_schd_error_list(full_err);
		free(pjobs);
		free_string_array(preempt_targets_list);
		return NULL;
	}
	npolicy = nsinfo->policy;
	nhjob = find_resource_resv_by_indrank(nsinfo->jobs, hjob->resresv_ind, hjob->rank);
	prev_prio = nhjob->job->preempt;

	if (sc_attrs.preempt_targets_enable) {
		if (preempt_targets_req != NULL) {
			prjobs = resource_resv_filter(nsinfo->running_jobs,
				count_array(nsinfo->running_jobs),
				preempt_job_set_filter,
				(void *) preempt_targets_list, NO_FLAGS);
			free_string_array(preempt_targets_list);
		}
	}

	if (prjobs != NULL) {
		rjobs = prjobs;
		rjobs_count = count_array(prjobs);
		if (rjobs_count > 0) {
			log_eventf(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, nhjob->name,
				"Limited running jobs used for preemption from %d to %d", nsinfo->sc.running, rjobs_count);
		}
		else {
			log_eventf(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, nhjob->name,
				"Limited running jobs used for preemption from %d to 0: No jobs to preempt", nsinfo->sc.running);
			pjobs_list = NULL;
			goto cleanup;
		}

	}
	else {
		rjobs = nsinfo->running_jobs;
		rjobs_count = nsinfo->sc.running;
	}

	/* sort jobs in ascending preemption priority and starttime... we want to preempt them
	 * from lowest prio to highest
	 */
	if (sc_attrs.preempt_sort == PS_MIN_T_SINCE_START) {
		qsort(rjobs, rjobs_count, sizeof(job_info *),
			cmp_preempt_stime_asc);
	}
	else {
		/* sort jobs in ascending preemption priority... we want to preempt them
		 * from lowest prio to highest
		 */
		qsort(rjobs, rjobs_count, sizeof(job_info *),
		cmp_preempt_priority_asc);
	}

	err = dup_schd_error(full_err);	/* only first element */
	if(err == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		pjobs_list = NULL;
		goto cleanup;
	}

	rjobs_subset = filter_preemptable_jobs(rjobs, nhjob, err);
	if (rjobs_subset == NULL) {
		log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_INFO, nhjob->name, "Found no preemptable candidates");
		pjobs_list = NULL;
		goto cleanup;
	}

	skipto = 0;
	while ((indexfound = select_index_to_preempt(npolicy, nhjob, rjobs_subset, skipto, err, fail_list)) != NO_JOB_FOUND) {
		struct preempt_ordering *po;
		int dont_preempt_job = 0;
		int ind = 0;

		if (indexfound == ERR_IN_SELECT) {
			/* System error occurred, no need to proceed */
			log_err(errno, __func__, MEM_ERR_MSG);
			pjobs_list = NULL;
			goto cleanup;
		}
		pjob = rjobs_subset[indexfound];
		log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, pjob->name,
			"Simulation: preempting job");

		po = schd_get_preempt_order(pjob);
		if (po != NULL) {
			if (!policy->rel_on_susp.empty() && po->order[0] == PREEMPT_METHOD_SUSPEND && pjob->job->can_suspend) {
				pjob->job->resreleased = create_res_released_array(npolicy, pjob);
				pjob->job->resreq_rel = create_resreq_rel_list(npolicy, pjob);
			}
		}

		update_universe_on_end(npolicy, pjob,  "S", NO_ALLPART);
		rjobs_count--;
		/* Check if any of the previously preempted job increased its preemption priority to be more than the
		 * high priority job
		 */
		for (ind = 0; pjobs[ind] != NULL; ind++) {
			if (pjobs[ind]->job->preempt > nhjob->job->preempt) {
				dont_preempt_job = 1;
				break;
			}
		}
		/* Check if the job we just ended increases its preemption priority to be more than the high priority job.
		 * If so, don't preempt this job
		 */
		if (dont_preempt_job || pjob->job->preempt > nhjob->job->preempt) {
			remove_resresv_from_array(rjobs_subset, pjob);
			log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_INFO, pjob->name,
				  "Preempting job will escalate its priority or priority of other jobs, not preempting it");
			if (sim_run_update_resresv(npolicy, pjob, ns_arr, NO_ALLPART) != 1) {
				log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_INFO, nhjob->name,
					  "Trouble finding preemptable candidates");
				pjobs_list = NULL;
				goto cleanup;
			}
			if (indexfound > 0)
				skipto = indexfound - 1;
			else
				skipto = 0;
			continue;
		}

		if (pjob->end_event != NULL)
			delete_event(nsinfo, pjob->end_event);

		pjobs[j++] = pjob;
		pjobs[j] = NULL;

		if (err != NULL) {
			old_errorcode = err->error_code;
			if (err->rdef != NULL) {
				old_rdef = err->rdef;
			} else
				old_rdef = NULL;
		}

		clear_schd_error(err);
		if ((ns_arr = is_ok_to_run(npolicy, nsinfo,
			nhjob->job->queue, nhjob, NO_ALLPART, err)) != NULL) {

			/* Normally when running a subjob, we do not care about the subjob. We just care that it successfully runs.
			 * We allow run_update_resresv() to enqueue and run the subjob.  In this case, we need to act upon the
			 * subjob after it runs.  To handle this case, we enqueue it first then we run it.
			 */
			if (nhjob->job->is_array) {
				resource_resv *nj;
				nj = queue_subjob(nhjob, nsinfo, nhjob->job->queue);

				if (nj == NULL) {
					pjobs_list = NULL;
					goto cleanup;
				}
				nhjob = nj;
			}


			log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, nhjob->name,
				"Simulation: Preempted enough work to run job");
			rc = sim_run_update_resresv(npolicy, nhjob, ns_arr, NO_ALLPART);
			break;
		} else if (old_errorcode == err->error_code) {
			if (err->rdef != NULL) {
			/* If the error code matches, make sure the resource definition is also matching.
			 * If the definition does not match that means the error is because of some other
			 * resource and we need to filter again.
			 * Otherwise, we just set the skipto to the index of the job that was last seen
			 * by select_index_to_preempt function. This will make select_index_to_preempt
			 * start looking at the jobs from the place it left in the previous call.
			 */
				if (old_rdef != err->rdef)
					filter_again = 1;
				else {
					skipto = indexfound;
					filter_again = 0;
				}
			} else {
				skipto = indexfound;
				filter_again = 0;
			}
		} else {
			/* error changed, so we need to revisit jobs discarded as preemption candidates earlier */
			filter_again = 1;
		}

		if (filter_again == 1) {
			free(rjobs_subset);
			rjobs_subset = filter_preemptable_jobs(rjobs, nhjob, err);
			if (rjobs_subset == NULL) {
				log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_INFO, nhjob->name, "Found no preemptable candidates");
				pjobs_list = NULL;
				goto cleanup;
			}
			filter_again = 0;
			skipto = 0;
		}

		translate_fail_code(err, NULL, log_buf);
		log_eventf(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, nhjob->name,
			"Simulation: not enough work preempted: %s", log_buf);
	}

	pjobs[j] = NULL;
	if (rjobs_subset != NULL)
		free(rjobs_subset);

	/* check to see if we lowered our preempt priority in our simulation
	 * if we have, then punt and don't
	 */
	if (prev_prio > nhjob->job->preempt) {
		rc = 0;
		log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, nhjob->name,
			"Job not run because it would immediately be preemptable.");
	}


	/* Right now we have a list of jobs we know will create enough space.  It
	 * might preempt too much work.  We need to determine if each job is
	 * still needed.
	 *
	 * We look to see if jobs are similar to the high priority job (preemption_similarity())
	 * or we try and rerun them in the simulated universe.
	 * If we can run them or the jobs aren't similar, then we don't have to
	 * preempt them.  We will go backwards from the end of the list because we
	 * started preempting with the lowest priority jobs.
	 */

	if (rc > 0) {
		if ((pjobs_list = static_cast<int *>(calloc((j + 1), sizeof(int)))) == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			goto cleanup;
		}

		for (j--, i = 0; j >= 0 ; j--) {
			int remove_job = 0;
			clear_schd_error(err);
			if (preemption_similarity(nhjob, pjobs[j], full_err) == 0) {
				remove_job = 1;
			} else if ((ns_arr = is_ok_to_run(npolicy, nsinfo,
				pjobs[j]->job->queue, pjobs[j], NO_ALLPART, err)) != NULL) {
				remove_job = 1;
				sim_run_update_resresv(npolicy, pjobs[j], ns_arr, NO_ALLPART);
			}


			if (remove_job) {
				log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG,
					pjobs[j]->name, "Simulation: preemption of job not needed.");
				remove_resresv_from_array(pjobs, pjobs[j]);

			} else {
				pjobs_list[i] = pjobs[j]->rank;
				i++;
			}
		}

		pjobs_list[i] = 0;
		/* i == 0 means we removed all the jobs: Should not happen */
		if (i == 0) {
			log_event(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, nhjob->name,
				"Simulation Error: All jobs removed from preemption list");
		} else
			*no_of_jobs = i;
	}
cleanup:
	free_server(nsinfo);
	free(pjobs);
	free(prjobs);
	free_schd_error_list(full_err);
	free_schd_error(err);

	return pjobs_list;
}

/**
 * @brief
 *		select a good candidate for preemption
 *
 * @param[in] policy - policy info
 * @param[in] hjob - the high priority job to preempt for
 * @param[in] rjobs - the list of running jobs to select from
 * @param[in] skipto - Index from where we need to start looking into rjobs
 * @param[in] err    - reason the high prio job isn't running
 * @param[in] fail_list - list of jobs to skip. They previously failed to be preempted.
 *			  Do not select them again.
 *
 * @return long
 * @retval index of the job to preempt
 * @retval NO_JOB_FOUND nothing can be selected for preemption
 * @retval ERR_IN_SELECT error
 */
long
select_index_to_preempt(status *policy, resource_resv *hjob,
	resource_resv **rjobs, long skipto, schd_error *err,
	int *fail_list)
{
	int i, j, k;
	int good = 1;		/* good boolean: Is job eligible to be preempted */
	struct preempt_ordering *po;

	if ( err == NULL || hjob == NULL || hjob->job == NULL ||
		rjobs == NULL || rjobs[0] == NULL)
		return NO_JOB_FOUND;

	/* This shouldn't happen, but you can never be too paranoid */
	if (hjob->job->is_running && hjob->ninfo_arr == NULL)
		return NO_JOB_FOUND;

	/* if we find a good job, we'll break out at the bottom
	 * we can't break out up here since i will be incremented by this point
	 * and we'll be returning the job AFTER the one we want too
	 */
	for (i = skipto; rjobs[i] != NULL; i++) {
		/* Does the running job have any resource we need? */
		int node_good = 1;

		/* lets be optimistic.. we'll start off assuming this is a good candidate */
		good = 1;

		/* if hjob hit a hard limit, check if candidate job has requested that resource
		 * if reason is different then set flag as if resource was found
		 */

		if (rjobs[i]->job == NULL || rjobs[i]->ninfo_arr == NULL)
			continue; /* we have problems... */

		if (!rjobs[i]->job->is_running)
			/* Only running jobs have resources allocated to them.
			 * They are only eligible to preempt.
			 */
			good = 0;


		if (rjobs[i]->job->is_provisioning)
			good = 0; /* provisioning job cannot be preempted */

		if (good) {
			if (rjobs[i]->job->can_not_preempt ||
			rjobs[i]->job->preempt >= hjob->job->preempt)
				good = 0;
		}

		if (good) {
			for (j = 0; fail_list[j] != 0; j++) {
				if (fail_list[j] == rjobs[i]->rank) {
					good = 0;
					break;
				}
			}
		}

		if (good) {
			/* get the preemption order to be used for this job */
			po = schd_get_preempt_order(rjobs[i]);

			/* check whether chosen order is enabled for this job */
			for (j = 0; j < PREEMPT_METHOD_HIGH; j++) {
				if (po->order[j] == PREEMPT_METHOD_SUSPEND  &&
					rjobs[i]->job->can_suspend)
					break; /* suspension is always allowed */

				if (po->order[j] == PREEMPT_METHOD_CHECKPOINT &&
					rjobs[i]->job->can_checkpoint)
					break; /* choose if checkpoint is allowed */

				if (po->order[j] == PREEMPT_METHOD_REQUEUE &&
					rjobs[i]->job->can_requeue)
					break; /* choose if requeue is allowed */
				if (po->order[j] == PREEMPT_METHOD_DELETE)
					break;
			}
			if (j == PREEMPT_METHOD_HIGH) /* no preemption method good */
				good = 0;
		}

		if (good) {
			for (j = 0; good && rjobs[i]->ninfo_arr[j] != NULL; j++) {
				if (rjobs[i]->ninfo_arr[j]->is_down || rjobs[i]->ninfo_arr[j]->is_offline)
					good = 0;
			}
		}

		/* if the high priority job is suspended then make sure we only
		 * select jobs from the node the job is currently suspended on
		 */

		if (good) {
			if (hjob->ninfo_arr != NULL) {
				for (j = 0; hjob->ninfo_arr[j] != NULL; j++) {
					if (find_node_by_rank(rjobs[i]->ninfo_arr,
						hjob->ninfo_arr[j]->rank) != NULL)
						break;
				}

				/* if we made all the way through the list, then rjobs[i] has no useful
				 * nodes for us to use... don't select it, unless it's not node resources we're after
				 */

				if (hjob->ninfo_arr[j] == NULL)
					good = 0;
			}
		}
		if (good) {
			schd_error *err;
			node_good = 0;

			err = new_schd_error();
			if(err == NULL)
				return NO_JOB_FOUND;

			for (j = 0; rjobs[i]->ninfo_arr[j] != NULL && !node_good; j++) {
				node_info *node = rjobs[i]->ninfo_arr[j];
				bool only_check_noncons = false;

				if (node->is_multivnoded) {
					/* unsafe to consider vnodes from multivnoded hosts "no good" when "not enough" of some consumable
					 * resource can be found in the vnode, since rest may be provided by other vnodes on the same host
					 * restrict check on these vnodes to check only against non consumable resources
					 */
					if (policy->resdef_to_check_noncons.empty()) {
						for (const auto& rtc : policy->resdef_to_check) {
							if (rtc->type.is_non_consumable)
								policy->resdef_to_check_noncons.insert(rtc);
						}
					}
					only_check_noncons = true;
				}
				for (k = 0; hjob->select->chunks[k] != NULL; k++) {
					long num_chunks_returned = 0;
					unsigned int flags = COMPARE_TOTAL | CHECK_ALL_BOOLS | UNSET_RES_ZERO;
					/* if only non consumables are checked, infinite number of chunks can be satisfied,
					 * and SCHD_INFINITY is negative, so don't be tempted to check on positive value
					 */
					clear_schd_error(err);
					if (only_check_noncons) {
						if (!policy->resdef_to_check_noncons.empty())
							num_chunks_returned = check_avail_resources(node->res, hjob->select->chunks[k]->req,
											flags, policy->resdef_to_check_noncons, INSUFFICIENT_RESOURCE, err);
						else
							num_chunks_returned = SCHD_INFINITY;
					} else
						num_chunks_returned = check_avail_resources(node->res, hjob->select->chunks[k]->req,
								flags, INSUFFICIENT_RESOURCE, err);

					if ( (num_chunks_returned > 0) || (num_chunks_returned == SCHD_INFINITY) ) {
						node_good = 1;
						break;
					}

				}
			}
			free_schd_error(err);
		}

		if (node_good == 0)
			good = 0;


		if (good)
			break;
	}

	if (good && rjobs[i] != NULL)
		return i;

	return NO_JOB_FOUND;
}

/**
 * @brief
 *		preempt_level - take a preemption priority and return a preemption
 *			level
 *
 * @param[in]	prio	-	the preemption priority
 *
 * @return	the preemption level
 *
 */
int
preempt_level(unsigned int prio)
{
	int level = NUM_PPRIO;
	int i;

	for (i = 0; i < NUM_PPRIO && level == NUM_PPRIO ; i++)
		if (sc_attrs.preempt_prio[i][1] == prio)
			level = i;

	return level;
}

/**
 * @brief
 *		set_preempt_prio - set a job's preempt field to the correct value
 *
 * @param[in]	job	-	the job to set
 * @param[in]	qinfo	-	the queue the job is in
 * @param[in]	sinfo	-	the job's server
 *
 * @return	nothing
 *
 */
void
set_preempt_prio(resource_resv *job, queue_info *qinfo, server_info *sinfo)
{
	int i;
	job_info *jinfo;
	int rc;

	if (job == NULL || job->job == NULL || qinfo == NULL || sinfo == NULL)
		return;

	jinfo = job->job;

	/* in the case of reseting the value, we need to clear them first */
	jinfo->preempt = 0;
	jinfo->preempt_status = 0;

	if (sinfo->qrun_job != NULL) {
		if (job == sinfo->qrun_job ||
		    (jinfo->is_subjob && jinfo->array_id == sinfo->qrun_job->name))
			jinfo->preempt_status |= PREEMPT_TO_BIT(PREEMPT_QRUN);
	}

	if (sc_attrs.preempt_queue_prio != SCHD_INFINITY &&
		qinfo->priority >= sc_attrs.preempt_queue_prio)
		jinfo->preempt_status |= PREEMPT_TO_BIT(PREEMPT_EXPRESS);

	if (over_fs_usage(jinfo->ginfo))
		jinfo->preempt_status |= PREEMPT_TO_BIT(PREEMPT_OVER_FS_LIMIT);

	if ((rc = check_soft_limits(sinfo, qinfo, job)) != 0) {
		if ((rc & PREEMPT_TO_BIT(PREEMPT_ERR)) != 0) {
			job->can_not_run = 1;
			log_event(PBSEVENT_ERROR, PBS_EVENTCLASS_JOB, LOG_ERR, job->name,
				"job marked as not runnable due to check_soft_limits internal error");
			return;
		}
		else
			jinfo->preempt_status |= rc;
	}

	/* we haven't set it yet, therefore it's a normal job */
	if (jinfo->preempt_status == 0)
		jinfo->preempt_status = PREEMPT_TO_BIT(PREEMPT_NORMAL);

	/* now that we've set all the possible preempt status's on the job, lets
	 * set its priority compared to those statuses.  The statuses are sorted
	 * by number of bits first, and priority second.  We need to just search
	 * through the list once and set the priority to the first one we find
	 */

	for (i = 0; i < NUM_PPRIO && sc_attrs.preempt_prio[i][0] != 0 &&
		jinfo->preempt == 0; i++) {
		if ((jinfo->preempt_status & sc_attrs.preempt_prio[i][0]) == sc_attrs.preempt_prio[i][0]) {
			jinfo->preempt = sc_attrs.preempt_prio[i][1];
			/* if the express bit is on, then we'll add the priority of that
			 * queue into our priority to allow for multiple express queues
			 */
			if (sc_attrs.preempt_prio[i][0] & PREEMPT_TO_BIT(PREEMPT_EXPRESS))
				jinfo->preempt += jinfo->queue->priority;
		}
	}
	/* we didn't find our preemption level -- this means we're a normal job */
	if (jinfo->preempt == 0) {
		jinfo->preempt_status = PREEMPT_TO_BIT(PREEMPT_NORMAL);
		jinfo->preempt = preempt_normal;
	}
}

/**
 * @brief
 * 		create subjob name from subjob id and array name
 *
 * @param[in]	array_id	-	the parent array name
 * @param[in]	index	-	subjob index
 *
 * @return	created subjob name
 */
std::string
create_subjob_name(const std::string& array_id, int index)
{
	std::string subjob_id;
	std::size_t brackets;

	subjob_id = array_id;
	brackets = subjob_id.find("[]");
	if (brackets == std::string::npos)
		return std::string("");
	subjob_id.insert(brackets + 1, std::to_string(index));

	return subjob_id;
}

/**
 * @brief
 *		create_subjob_from_array - create a resource_resv structure for a
 *				   subjob from a job array structure.  The
 *				   subjob will be in state 'Q'
 *
 * @param[in]	array	-	the job array
 * @param[in]	index	-	the subjob index
 * @param[in]	subjob_name	-	name of subjob @see create_subjob_name()
 *
 * @return	the new subjob
 * @retval	NULL	: on error
 *
 */
resource_resv *
create_subjob_from_array(resource_resv *array, int index, const std::string& subjob_name)
{
	resource_resv *subjob;	/* job_info structure for new subjob */
	range *tmp;			/* a tmp ptr to hold the queued_indices ptr */
	schd_error *err;

	if (array == NULL || array->job == NULL)
		return NULL;

	if (!array->job->is_array)
		return NULL;

	err = new_schd_error();
	if (err == NULL)
		return NULL;

	/* so we don't dup the queued_indices for the subjob */
	tmp = array->job->queued_subjobs;
	array->job->queued_subjobs = NULL;

	subjob = dup_resource_resv(array, array->server, array->job->queue, subjob_name);

	if (subjob == NULL) {
		free_schd_error(err);
		return NULL;
	}

	/* make a copy of dependent jobs */
	subjob->job->depend_job_str = string_dup(array->job->depend_job_str);
	subjob->job->dependent_jobs = (resource_resv **) dup_array(array->job->dependent_jobs);

	array->job->queued_subjobs = tmp;

	subjob->job->is_begin = 0;
	subjob->job->is_array = 0;

	subjob->job->is_queued = 1;
	subjob->job->is_subjob = 1;
	subjob->job->array_index = index;
	subjob->job->array_id = array->name;

	subjob->rank =  get_sched_rank();

	free_schd_error(err);
	return subjob;
}

/**
 * @brief
 *		update_array_on_run - update a job array object when a subjob is run
 *
 * @param[in]	array	-	the job array to update
 * @param[in]	subjob	-	the subjob which was run
 *
 * @return	success or failure
 *
 */
int
update_array_on_run(job_info *array, job_info *subjob)
{
	if (array == NULL || subjob == NULL)
		return 0;

	range_remove_value(&array->queued_subjobs, subjob->array_index);

	if (array->is_queued) {
		array->is_begin = 1;
		array->is_queued = 0;
	}

	return 1;
}

/**
 * @brief
 *		is_job_array - is a job name a job array range
 *			  valid_form: 1234[]
 *			  valid_form: 1234[N]
 *			  valid_form: 1234[N-M]
 *
 * @param[in]	jobname	-	jobname to check
 *
 * @return int
 * @retval	1	: if jobname is a job array
 * @retval	2	: if jobname is a subjob
 * @retval	3	: if jobname is a range
 * @retval	0	: if it is not a job array
 */
int
is_job_array(char *jobname)
{
	char *bracket;
	int ret = 0;

	if (jobname == NULL)
		return 0;

	bracket = strchr(jobname, (int) '[');

	if (bracket != NULL) {
		if (*(bracket + 1) == ']')
			ret = 1;
		else if (strchr(bracket, (int) '-') != NULL)
			ret = 3;
		else
			ret = 2;
	}

	return ret;
}

/**
 * @brief
 *		modify_job_array_for_qrun - modify a job array for qrun -
 *				    set queued_subjobs to just the
 *				    range which is being run
 *				    set qrun_job on server
 *
 * @param[in,out]	sinfo	-	server to modify job array
 * @param[in]	jobid	-	string job name
 *
 * @return	int
 * @retval	1	: on success
 * @retval	0	: on failure
 * @retval	-1	: on error
 *
 */
int
modify_job_array_for_qrun(server_info *sinfo, char *jobid)
{
	char name[128];
	char rest[128];
	char *rangestr;
	char *ptr;
	range *r, *r2;
	int len;

	resource_resv *job;

	if (sinfo == NULL || jobid == NULL)
		return -1;

	pbs_strncpy(name, jobid, sizeof(name));

	if ((ptr = strchr(name, (int) '[')) == NULL)
		return 0;

	*ptr = '\0';
	rangestr = ptr + 1;

	if ((ptr = strchr(rangestr, ']')) == NULL)
		return 0;

	pbs_strncpy(rest, ptr, sizeof(rest));

	*ptr = '\0';

	/* now rangestr should be the subjob index or range of indices */
	if ((r = range_parse(rangestr)) == NULL)
		return 0;

	/* now that we've converted the subjob index or range into a range list
	 * we can munge our original name to find the job array
	 */
	len = strlen(name);
	name[len] = '[';
	name[len + 1] = '\0';
	strcat(name, rest);

	job = find_resource_resv(sinfo->jobs, name);

	if (job != NULL) {
		/* lets only run the jobs which were requested */
		r2 = range_intersection(r, job->job->queued_subjobs);
		if (r2 != NULL) {
			free_range_list(job->job->queued_subjobs);
			job->job->queued_subjobs = r2;
		}
		else {
			free_range_list(r);
			return 0;
		}
	}
	else {
		free_range_list(r);
		return 0;
	}

	sinfo->qrun_job = job;
	free_range_list(r);
	return 1;
}

/**
 * @brief
 *		create a subjob from a job array and queue it
 *
 * @param[in]	array	-	job array to create the next subjob from
 * @param[in]	sinfo	-	the server the job array is in
 * @param[in]	qinfo	-	the queue the job array is in
 *
 * @return	resource_resv *
 * @retval	new subjob
 * @retval	NULL	: on error
 *
 * @note
 * 		subjob will be attached to the server/queue job lists
 *
 */
resource_resv *
queue_subjob(resource_resv *array, server_info *sinfo,
	queue_info *qinfo)
{
	int subjob_index;
	std::string subjob_name;
	resource_resv *rresv = NULL;
	resource_resv **tmparr = NULL;

	if (array == NULL || array->job == NULL || sinfo == NULL || qinfo == NULL)
		return NULL;

	if (!array->job->is_array)
		return NULL;

	subjob_index = range_next_value(array->job->queued_subjobs, -1);
	if (subjob_index >= 0) {
		subjob_name = create_subjob_name(array->name, subjob_index);
		if (!subjob_name.empty()) {
			if ((rresv = find_resource_resv(sinfo->jobs, subjob_name)) != NULL) {
				/* Set tmparr to something so we're not considered an error */
				tmparr = sinfo->jobs;
			}
			else if ((rresv = create_subjob_from_array(array, subjob_index, subjob_name)) != NULL) {
				/* add_resresv_to_array calls realloc, so we need to treat this call
				 * as a call to realloc.  Put it into a temp variable to check for NULL
				 */
				tmparr = add_resresv_to_array(sinfo->jobs, rresv, NO_FLAGS);
				if (tmparr != NULL) {
					sinfo->jobs = tmparr;
					sinfo->sc.queued++;
					sinfo->sc.total++;

					tmparr = add_resresv_to_array(sinfo->all_resresv, rresv, SET_RESRESV_INDEX);
					if (tmparr != NULL) {
						sinfo->all_resresv = tmparr;
						tmparr = add_resresv_to_array(qinfo->jobs, rresv, NO_FLAGS);
						if (tmparr != NULL) {
							qinfo->jobs = tmparr;
							qinfo->sc.queued++;
							qinfo->sc.total++;
						}
					}
				}
				rresv->job->parent_job = array;
			}
		}
	}

	if (tmparr == NULL || rresv == NULL) {
		log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG, array->name,
			"Unable to create new subjob for job array");
		return NULL;
	}

	return rresv;
}

/**
 * @brief
 * 		evaluate a math formula for jobs based on their resources
 *		NOTE: currently done through embedded python interpreter
 *
 * @param[in]	formula	-	formula to evaluate
 * @param[in]	resresv	-	job for special case key words
 * @param[in]	resreq	-	resources to use when evaluating
 *
 * @return	evaluated formula answer or 0 on exception
 *
 */

#ifdef PYTHON
sch_resource_t
formula_evaluate(const char *formula, resource_resv *resresv, resource_req *resreq)
{
	char buf[1024];
	char *globals;
	int globals_size = 1024;  /* initial size... will grow if needed */
	sch_resource_t ans = 0;
	char *formula_buf;
	int formula_buf_len;

	PyObject *module;
	PyObject *dict;
	PyObject *obj;

	if (formula == NULL || resresv == NULL ||
		resresv->job == NULL)
		return 0;

	formula_buf_len = sizeof(buf) + strlen(formula) + 1;

	formula_buf = static_cast<char *>(malloc(formula_buf_len));
	if (formula_buf == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return 0;
	}

	if ((globals = static_cast<char *>(malloc(globals_size * sizeof(char)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		free(formula_buf);
		return 0;
	}

	globals[0] = '\0';

	if (pbs_strcat(&globals, &globals_size, "globals_dict = {") == NULL) {
		free(globals);
		free(formula_buf);
		return 0;
	}


	for (const auto& cr : consres) {
		auto req = find_resource_req(resreq, cr);

		if (req != NULL)
			sprintf(buf, "\'%s\':%.*f,", cr->name.c_str(),
				float_digits(req->amount, FLOAT_NUM_DIGITS), req->amount);
		else
			sprintf(buf, "\'%s\' : 0,", cr->name.c_str());

		if (pbs_strcat(&globals, &globals_size, buf) == NULL) {
			free(globals);
			free(formula_buf);
			return 0;
		}
	}

	/* special cases */
	sprintf(buf, "\'%s\':%ld,\'%s\':%d,\'%s\':%d,\'%s\':%f, \'%s\': %f, \'%s\': %f, \'%s\': %f, \'%s\':%d}",
		FORMULA_ELIGIBLE_TIME, resresv->job->eligible_time,
		FORMULA_QUEUE_PRIO, resresv->job->queue->priority,
		FORMULA_JOB_PRIO, resresv->job->priority,
		FORMULA_FSPERC, resresv->job->ginfo->tree_percentage,
		FORMULA_FSPERC_DEP, resresv->job->ginfo->tree_percentage,
		FORMULA_TREE_USAGE, resresv->job->ginfo->usage_factor,
		FORMULA_FSFACTOR, resresv->job->ginfo->tree_percentage == 0 ? 0 :
			pow(2, -(resresv->job->ginfo->usage_factor/resresv->job->ginfo->tree_percentage)),
		FORMULA_ACCRUE_TYPE, resresv -> job -> accrue_type);
	if (pbs_strcat(&globals, &globals_size, buf) == NULL) {
		free(globals);
		free(formula_buf);
		return 0;
	}

	PyRun_SimpleString(globals);
	free(globals);

	/* now that we've set all the values, let's calculate the answer */
	snprintf(formula_buf, formula_buf_len,
		"_PBS_PYTHON_EXCEPTIONSTR_=\"\"\n"
		"ex = None\n"
		"try:\n"
		"\t_FORMANS_ = eval(\"%s\", globals_dict, locals())\n"
		"except Exception as ex:"
		"\t_PBS_PYTHON_EXCEPTIONSTR_=str(ex)\n", formula);

	PyRun_SimpleString(formula_buf);
	free(formula_buf);

	module = PyImport_AddModule("__main__");
	dict = PyModule_GetDict(module);
	obj = PyMapping_GetItemString(dict, "_FORMANS_");

	if (obj != NULL) {
		ans = PyFloat_AsDouble(obj);
		Py_XDECREF(obj);
	}

	obj = PyMapping_GetItemString(dict, "_PBS_PYTHON_EXCEPTIONSTR_");
	if (obj != NULL) {
		auto str = PyUnicode_AsUTF8(obj);
		if (str != NULL) {
			if (strlen(str) > 0) { /* exception happened */
				log_eventf(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG, resresv->name,
					"Formula evaluation for job had an error.  Zero value will be used: %s", str);
				ans = 0;
			}
		}
		Py_XDECREF(obj);
	}



	return ans;
}
#else
sch_resource_t
formula_evaluate(char *formula, resource_resv *resresv, resource_req *resreq)
{
	return 0;
}
#endif

/**
 * @brief
 * 		Set the job accrue type to eligible time.
 *
 * @param[in]	pbs_sd	-	connection to pbs_server
 * @param[out]	resresv	-	pointer to job
 *
 * @return	void
 *
 */
static void
make_eligible(int pbs_sd, resource_resv *resresv)
{
	if (resresv == NULL || resresv->job == NULL)
		return;
	if (resresv->job->accrue_type !=JOB_ELIGIBLE) {
		update_job_attr(pbs_sd, resresv, ATTR_accrue_type, NULL, ACCRUE_ELIG, NULL, UPDATE_LATER);
		resresv->job->accrue_type = JOB_ELIGIBLE;
	}
	return;
}

/**
 * @brief
 * 		Set the job accrue type to ineligible time.
 *
 * @param[in]	pbs_sd	-	connection to pbs_server
 * @param[out]	resresv	-	pointer to job
 *
 * @return	void
 */
static void
make_ineligible(int pbs_sd, resource_resv *resresv)
{
	if (resresv == NULL || resresv->job == NULL)
		return;
	if (resresv->job->accrue_type !=JOB_INELIGIBLE) {
		update_job_attr(pbs_sd, resresv, ATTR_accrue_type, NULL, ACCRUE_INEL, NULL, UPDATE_LATER);
		resresv->job->accrue_type = JOB_INELIGIBLE;
	}
	return;
}

/**
 * @brief
 * 		Updates accrue_type of job on server. The accrue_type is determined
 * 		from the values of mode and err_code. If resresv is a job array, special
 * 		action is taken. If mode is set to something other than ACCRUE_CHECK_ERR
 * 		then the value of err_code is ignored unless it is set to SCHD_ERROR.
 *
 * @param[in]	pbs_sd	-	connection to pbs_server
 * @param[in]	sinfo	-	pointer to server
 * @param[in]	mode	-	mode of operation
 * @param[in]	err_code	-	sched_error_code value
 * @param[in,out]	resresv	-	pointer to job
 *
 * @return void
 *
 */
void
update_accruetype(int pbs_sd, server_info *sinfo,
	enum update_accruetype_mode mode, enum sched_error_code err_code,
	resource_resv *resresv)
{
	if (sinfo == NULL || resresv == NULL || resresv->job == NULL)
		return;

	/* if SCHD_ERROR, don't change accrue type */
	if (err_code == SCHD_ERROR)
		return;

	if (sinfo->eligible_time_enable == 0)
		return;

	/* behavior of job array's eligible_time calc differs from jobs/subjobs,
	 *  it depends on:
	 *    1) job array is empty - accrues ineligible time
	 *    2) job array has instantiated all subjobs - accrues ineligible time
	 *    3) job array has atleast one subjob to run - accrues eligible time
	 */

	if (resresv->job->is_array && resresv->job->is_begin &&
		(range_next_value(resresv->job->queued_subjobs, -1) < 0)) {
		make_ineligible(pbs_sd, resresv);
		return;
	}

	if ((resresv->job->preempt_status & PREEMPT_QUEUE_SERVER_SOFTLIMIT) >0) {
		make_ineligible(pbs_sd, resresv);
		return;
	}

	if (mode == ACCRUE_MAKE_INELIGIBLE) {
		make_ineligible(pbs_sd, resresv);
		return;
	}

	if (mode == ACCRUE_MAKE_ELIGIBLE) {
		make_eligible(pbs_sd, resresv);
		return;
	}

	/* determine accruetype from err code */
	switch (err_code) {

		case SUCCESS:
			/* server updates accrue_type to RUNNING, hence, simply move out */
			/* accrue type is set to running in update_resresv_on_run() */
			break;

		case SERVER_BYUSER_JOB_LIMIT_REACHED:
		case SERVER_BYUSER_RES_LIMIT_REACHED:
		case SERVER_USER_LIMIT_REACHED:
		case SERVER_USER_RES_LIMIT_REACHED:
		case SERVER_BYGROUP_JOB_LIMIT_REACHED:
		case SERVER_BYPROJECT_JOB_LIMIT_REACHED:
		case SERVER_BYGROUP_RES_LIMIT_REACHED:
		case SERVER_BYPROJECT_RES_LIMIT_REACHED:
		case SERVER_GROUP_LIMIT_REACHED:
		case SERVER_GROUP_RES_LIMIT_REACHED:
		case SERVER_PROJECT_LIMIT_REACHED:
		case SERVER_PROJECT_RES_LIMIT_REACHED:
		case QUEUE_BYUSER_JOB_LIMIT_REACHED:
		case QUEUE_BYUSER_RES_LIMIT_REACHED:
		case QUEUE_USER_LIMIT_REACHED:
		case QUEUE_USER_RES_LIMIT_REACHED:
		case QUEUE_BYGROUP_JOB_LIMIT_REACHED:
		case QUEUE_BYPROJECT_JOB_LIMIT_REACHED:
		case QUEUE_BYGROUP_RES_LIMIT_REACHED:
		case QUEUE_BYPROJECT_RES_LIMIT_REACHED:
		case QUEUE_GROUP_LIMIT_REACHED:
		case QUEUE_GROUP_RES_LIMIT_REACHED:
		case QUEUE_PROJECT_LIMIT_REACHED:
		case QUEUE_PROJECT_RES_LIMIT_REACHED:
		case NODE_GROUP_LIMIT_REACHED:
		case JOB_UNDER_THRESHOLD:
			make_ineligible(pbs_sd, resresv);
			break;

			/*
			 * The list of ineligible cases must be complete, the remainer are eligible.
			 * Some eligible cases include:
			 * - SERVER_JOB_LIMIT_REACHED
			 * - QUEUE_JOB_LIMIT_REACHED
			 * - CROSS_PRIME_BOUNDARY
			 * - CROSS_DED_TIME_BOUNDRY
			 * - ERR_SPECIAL
			 * - NO_NODE_RESOURCES
			 * - INSUFFICIENT_RESOURCE
			 * - BACKFILL_CONFLICT
			 * - RESERVATION_INTERFERENCE
			 * - PRIME_ONLY
			 * - NONPRIME_ONLY
			 * - DED_TIME
			 * - INSUFFICIENT_QUEUE_RESOURCE
			 * - INSUFFICIENT_SERVER_RESOURCE
			 */
		default:
			make_eligible(pbs_sd, resresv);
			break;
	}

	return;
}

/**
 * @brief
 *		Get AOE name from select of job/reservation.
 *
 * @see
 *		query_jobs
 *		query_reservations
 *
 * @param[in]	select	-	select of job/reservation
 *
 * @return	char *
 * @retval	NULL	: no AOE requested
 * @retval	aoe	: AOE requested
 *
 * @par Side Effects:
 *		None
 *
 * @par	MT-safe: Yes
 *
 */
char*
getaoename(selspec *select)
{
	int i = 0;

	if (select == NULL)
		return NULL;

	for (i = 0; select->chunks[i] != NULL; i++) {
		auto req = find_resource_req(select->chunks[i]->req, allres["aoe"]);
		if (req != NULL)
			return string_dup(req->res_str);
	}

	return NULL;
}

/**
 * @brief
 *	Get EOE name from select of job/reservation.
 *
 * @see
 *	query_jobs
 *	query_reservations
 *
 * @param[in]	select	  -	select of job/reservation
 *
 * @return	char *
 * @retval	NULL : no EOE requested
 * @retval	eoe  : EOE requested
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 *
 */
char*
geteoename(selspec *select)
{
	resource_req *req;

	if (select == NULL || select->chunks == NULL || select->chunks[0] == NULL)
		return NULL;

	/* we only need to look at 1st chunk since either all request eoe
	 * or none request eoe.
	 */
	req = find_resource_req(select->chunks[0]->req, allres["eoe"]);
	if (req != NULL)
		return string_dup(req->res_str);

	return NULL;
}

/**
 * @brief
 * 		updated the estimated.start_time and
 *		 estimated.exec_vnode attributes on a job
 *
 * @param[in]	pbs_sd	-	connection descriptor to pbs server
 * @param[in]	job	-	job to update
 * @param[in]	start_time	-	start time of job
 * @param[in]	exec_vnode	-	exec_vnode of job or NULL to create it from
 *			      				job->nspec_arr
 * @param[in]	force	-	forces attributes to update now -- no checks
 *
 * @return	int
 * @retval	1	: if attributes were successfully updated
 * @retval	0	: if attributes were not updated for a valid reason
 * @retval	-1	: if attributes were not updated for an error
 */
int
update_estimated_attrs(int pbs_sd, resource_resv *job,
	time_t start_time, char *exec_vnode, int force)
{
	struct attrl attr = {0};
	char timebuf[128];
	resource_resv *array = NULL;
	resource_resv *resresv;
	enum update_attr_flags aflags;

	if (job == NULL)
		return -1;

	if (job->is_job && job->job ==NULL)
		return -1;

	if (!force) {
		if (job->job->is_subjob) {
			array = find_resource_resv(job->server->jobs, job->job->array_id);
			if (array != NULL) {
				if (job->job->array_index !=
					range_next_value(array->job->queued_subjobs, -1)) {
					return -1;
				}
			}
			else
				return -1;
		}
		aflags = UPDATE_LATER;
	}
	else {
		aflags = UPDATE_NOW;
		if (!job->job->array_id.empty())
			array = find_resource_resv(job->server->jobs, job->job->array_id);
	}


	/* create attrl for estimated.exec_vnode to be passed as the 'extra' field
	 * to update_job_attr().  This will cause both attributes to be updated
	 * in one call to pbs_asyalterjob()
	 */
	attr.name = const_cast<char *>(ATTR_estimated);
	attr.resource = const_cast<char *>("exec_vnode");
	if (exec_vnode == NULL)
		attr.value = create_execvnode(job->nspec_arr);
	else
		attr.value = exec_vnode;

	snprintf(timebuf, 128, "%ld", (long) start_time);

	if (array)
		resresv = array;
	else
		resresv = job;

	return update_job_attr(pbs_sd, resresv, ATTR_estimated, "start_time",
		timebuf, &attr, aflags);
}

/**
 * @brief
 * 		This function checks if preemption set has been configured as TARGET_NONE
 *          If its found that preempt_targets = TARGET_NONE then this function returns PREEMPT_NONE.
 * @param[in]	res_list	-	list of resources created from comma seperated resource list.
 *
 * @return	int
 * @retval	PREEMPT_NONE	: If preemption set is set to TARGET_NONE
 * @retval	0	: If preemption set is not set as TARGET_NONE
 */
int
check_preempt_targets_for_none(char ** res_list)
{
	char *arg = NULL;
	int i = 0;
	if (res_list == NULL)
		return 0;

	for (arg = res_list[i]; arg != NULL; i++, arg = res_list[i]) {
		if (!strcasecmp(arg, TARGET_NONE)) {
			return PREEMPT_NONE;
		}
	}
	return 0;
}

/**
 * @brief
 * 		checks whether the IFL interface failed because it was a finished job
 *
 * @param[in]	error	-	pbs_errno set by server
 *
 * @retval	1	: if job is a finished job
 * @retval	0	: if job is not a finished job
 */
int
is_finished_job(int error)
{
	switch(error) {
		case PBSE_UNKJOBID:
		case PBSE_HISTJOBID:
			return(1);
		default:
			return(0);
	}
}


/**
 * @brief
 * 		preemption_similarity - Compare two running jobs to see if they have
 *			overlap.  Overlap is defined in terms of preemption.
 *			Can pre-emptee pjob help in running hjob.  In doing this
 *			we look at the full list of reasons hjob can not run and
 *			run a similarity heuristic against the two jobs to see if
 *			they are alike.
 *
 * @param[in]	hjob	-	high priority job
 * @param[in]	pjob	-	job to see if it is similar to the high priority job
 * @param[in]	full_err	-	list of reasons why hjob can not run right now.  It gets
 *                      		created by passing the RETURN_ALL_ERRS to is_ok_to_run()
 * @return	int
 * @retval	1	: jobs are similar
 * @retval	0	: jobs are not similar
 */
int
preemption_similarity(resource_resv *hjob, resource_resv *pjob, schd_error *full_err)
{
	schd_error *cur_err;
	int match = 0;
	schd_resource *res;
	int j;

	for (cur_err = full_err; match == 0 && cur_err != NULL; cur_err = cur_err->next) {
		switch (cur_err->error_code) {
			case QUEUE_JOB_LIMIT_REACHED:
			case QUEUE_RESOURCE_LIMIT_REACHED:
				if (pjob->job->queue == hjob->job->queue)
					match = 1;
				break;
			case SERVER_USER_LIMIT_REACHED:
			case SERVER_USER_RES_LIMIT_REACHED:
			case SERVER_BYUSER_JOB_LIMIT_REACHED:
			case SERVER_BYUSER_RES_LIMIT_REACHED:
				if (strcmp(pjob->user, hjob->user) == 0)
					match = 1;
				break;
			case QUEUE_USER_LIMIT_REACHED:
			case QUEUE_USER_RES_LIMIT_REACHED:
			case QUEUE_BYUSER_JOB_LIMIT_REACHED:
			case QUEUE_BYUSER_RES_LIMIT_REACHED:
				if (pjob->job->queue == hjob->job->queue &&
					strcmp(pjob->user, hjob->user) == 0)
					match = 1;

				break;
			case SERVER_GROUP_LIMIT_REACHED:
			case SERVER_GROUP_RES_LIMIT_REACHED:
			case SERVER_BYGROUP_JOB_LIMIT_REACHED:
			case SERVER_BYGROUP_RES_LIMIT_REACHED:
				if (strcmp(pjob->group, hjob->group) == 0)
					match = 1;
				break;

			case QUEUE_GROUP_LIMIT_REACHED:
			case QUEUE_GROUP_RES_LIMIT_REACHED:
			case QUEUE_BYGROUP_JOB_LIMIT_REACHED:
			case QUEUE_BYGROUP_RES_LIMIT_REACHED:
				if (pjob->job->queue == hjob->job->queue &&
					strcmp(pjob->group, hjob->group) == 0)
					match = 1;
				break;
			case SERVER_PROJECT_LIMIT_REACHED:
			case SERVER_PROJECT_RES_LIMIT_REACHED:
			case SERVER_BYPROJECT_RES_LIMIT_REACHED:
			case SERVER_BYPROJECT_JOB_LIMIT_REACHED:
				if (strcmp(pjob->project, hjob->project) == 0)
					match = 1;
				break;
			case QUEUE_PROJECT_LIMIT_REACHED:
			case QUEUE_PROJECT_RES_LIMIT_REACHED:
			case QUEUE_BYPROJECT_RES_LIMIT_REACHED:
			case QUEUE_BYPROJECT_JOB_LIMIT_REACHED:
				if (pjob->job->queue == hjob->job->queue &&
					strcmp(pjob->project, hjob->project) == 0)
					match = 1;
				break;
			case SERVER_JOB_LIMIT_REACHED:
			case SERVER_RESOURCE_LIMIT_REACHED:
				match = 1;
				break;
			/* Codes from check_nodes(): check_nodes() returns a code for one node.
			 * The code itself doesn't really help us.  What it does do is signal us
			 * that we searched the nodes and didn't find a match.  We need to check if
			 * there are nodes in the exec_vnodes that are similar
			 */
			case NO_AVAILABLE_NODE:
			case NOT_ENOUGH_NODES_AVAIL:
			case NO_NODE_RESOURCES:
			case INVALID_NODE_STATE:
			case INVALID_NODE_TYPE:
			case NODE_JOB_LIMIT_REACHED:
			case NODE_USER_LIMIT_REACHED:
			case NODE_GROUP_LIMIT_REACHED:
			case NODE_NO_MULT_JOBS:
			case NODE_UNLICENSED:
			case INSUFFICIENT_RESOURCE:
			case AOE_NOT_AVALBL:
			case PROV_RESRESV_CONFLICT:
			case NO_FREE_NODES:
			case NO_TOTAL_NODES:
			case NODE_NOT_EXCL:
			case CANT_SPAN_PSET:
			case IS_MULTI_VNODE:
			case RESERVATION_CONFLICT:
			case SET_TOO_SMALL:

				if (hjob->ninfo_arr != NULL && pjob->ninfo_arr != NULL) {
					for (j = 0; hjob->ninfo_arr[j] != NULL && !match; j++) {
						if (find_node_by_rank(pjob->ninfo_arr, hjob->ninfo_arr[j]->rank) != NULL)
							match = 1;
					}
				}

				break;
			case INSUFFICIENT_QUEUE_RESOURCE:
				if (hjob->job->queue == pjob->job->queue) {
					for (res = hjob->job->queue->qres; res != NULL; res = res->next) {
						if (res->avail != SCHD_INFINITY_RES)
							if (find_resource_req(pjob->resreq, res->def) != NULL)
								match = 1;
					}
				}
				break;
			case INSUFFICIENT_SERVER_RESOURCE:
				for (res = hjob->server->res; res != NULL; res = res->next) {
					if (res->avail != SCHD_INFINITY_RES)
						if (find_resource_req(pjob->resreq, res->def) != NULL)
							match = 1;
				}
				break;
			default:
				/* Something we didn't expect, err on the side of caution */
				match = 1;
				break;
		}
	}
	return match;
}

/**
 * @brief Create the resources_released and resource_released_list for a job
 *	    and also set execselect on the job based on resources_released
 *
 * @param[in] policy - policy object
 * @param[in] pjob - Job structure using which resources_released string is created
 *
 * @retval - void
 */
void create_res_released(status *policy, resource_resv *pjob)
{
	std::string selectspec ;
	if (pjob->job->resreleased == NULL) {
		pjob->job->resreleased = create_res_released_array(policy, pjob);
		if (pjob->job->resreleased == NULL) {
			return;
		}
		pjob->job->resreq_rel = create_resreq_rel_list(policy, pjob);
	}
	selectspec = create_select_from_nspec(pjob->job->resreleased);
	delete pjob->execselect;
	pjob->execselect = parse_selspec(selectspec);
	return;
}

/**
 * @brief This function populates resreleased job structure for a particular job.
 *	  It does so by duplicating the job's exec_vnode and only keeping the
 *	  consumable resources in policy->rel_on_susp
 *
 * @param[in] policy - policy object
 * @param[in] resresv - Job to create resources_released
 *
 * @return nspec **
 * @retval nspec array of released resources
 * @retval NULL
 *
 */
nspec **create_res_released_array(status *policy, resource_resv *resresv)
{
	nspec **nspec_arr = NULL;
	resource_req *req;

	if ((resresv == NULL) || (resresv->nspec_arr == NULL) || (resresv->ninfo_arr == NULL))
		return NULL;

	nspec_arr = dup_nspecs(resresv->nspec_arr, resresv->ninfo_arr, NULL);
	if (nspec_arr == NULL)
		return NULL;
	if (!policy->rel_on_susp.empty()) {
		for (int i = 0; nspec_arr[i] != NULL; i++) {
			for (req = nspec_arr[i]->resreq; req != NULL; req = req->next) {
				auto ros = policy->rel_on_susp;
				if (req->type.is_consumable == 1 && ros.find(req->def) == ros.end())
					req->amount = 0;
			}
		}
	}
	return nspec_arr;
}

/**
 * @brief create a resource_rel array for a job by accumulating all of the RASSN
 *	    resources in a resources_released nspec array.
 *
 * @note only uses RASSN resources on the sched_config resources line
 *
 * @param policy - policy info
 * @param pjob -  resource reservation structure
 * @return resource_req *
 * @retval newly created resreq_rel array
 * @retval NULL on error
 */
resource_req *create_resreq_rel_list(status *policy, resource_resv *pjob)
{
	resource_req *resreq_rel = NULL;
	resource_req *rel;
	resource_req *req;
	if (policy == NULL || pjob == NULL)
		return NULL;

	for (req = pjob->resreq; req != NULL; req = req->next) {
		auto rdc = policy->resdef_to_check_rassn;
		if (rdc.find(req->def) != rdc.end()) {
			auto ros = policy->rel_on_susp;
			if (!policy->rel_on_susp.empty() && ros.find(req->def) == ros.end())
				continue;
			rel = find_alloc_resource_req(resreq_rel, req->def);
			if (rel != NULL) {
				rel->amount += req->amount;
				if (resreq_rel == NULL)
					resreq_rel = rel;
			}
		}
	}
	return resreq_rel;
}

/**
 * @brief extend the soft walltime of job.  A job's soft_walltime will be extended by 100% of its
 * 		original soft_walltime.  If this extension would go past the job's normal walltime
 * 		the soft_walltime is set to the normal walltime.
 * @param[in] policy - policy info
 * @param[in] resresv - job to extend soft walltime
 * @param[in] server_time - current time on the sinfo
 * @return extended soft walltime duration
 */
long
extend_soft_walltime(resource_resv *resresv, time_t server_time)
{
	resource_req *walltime_req;
	resource_req *soft_walltime_req;

	int extension = 0;
	int num_ext_over;

	long job_duration = UNSPECIFIED;
	long extended_duration = UNSPECIFIED;


	if (resresv == NULL)
		return UNSPECIFIED;

	soft_walltime_req = find_resource_req(resresv->resreq, allres["soft_walltime"]);
	walltime_req = find_resource_req(resresv->resreq, allres["walltime"]);

	if (soft_walltime_req == NULL) { /* Nothing to extend */
		if(walltime_req != NULL)
			return walltime_req->amount;
		else
			return JOB_INFINITY;
	}


	job_duration = soft_walltime_req->amount;

	/* number of times the job has been extended */
	num_ext_over = (server_time - resresv->job->stime) / job_duration;

	extension = num_ext_over * job_duration;
	extended_duration = job_duration + extension;
	if (walltime_req != NULL) {
		if (extended_duration > walltime_req->amount) {
			extended_duration = walltime_req->amount;
		}
	}
	return extended_duration;
}

/**
 * @brief   This function is used as a callback with resource_resv_filter(). It	finds out whether or not
 *	    the job in question is appropriate to be preempted.
 * @param[in] job - job that is being analyzed
 * @param[in] arg - a pointer to resresv_filter structure which contains the job that could not run
 *		    and a sched error structure specifying the reason why it could not run.
 *
 * @return - integer
 * @retval - 0 if job is not valid for preemption
 * @retval - 1 if the job is valid for preemption
 */
static int cull_preemptible_jobs(resource_resv *job, const void *arg)
{
	struct resresv_filter *inp;
	resource_req *req_scan;

	if (arg == NULL || job == NULL)
		return 0;
	inp = (struct resresv_filter *)arg;
	if (inp->job == NULL)
		return 0;

	/* make sure that only running jobs are looked at */
	if (job->job->is_running == 0)
		return 0;

	if (job->job->preempt >= inp->job->job->preempt)
		return 0;

	switch (inp->err->error_code) {
		case SERVER_USER_RES_LIMIT_REACHED:
		case SERVER_BYUSER_RES_LIMIT_REACHED:
			if ((strcmp(job->user, inp->job->user) == 0) &&
			    find_resource_req(job->resreq, inp->err->rdef) != NULL)
				return 1;
			break;
		case QUEUE_USER_RES_LIMIT_REACHED:
		case QUEUE_BYUSER_RES_LIMIT_REACHED:
			if ((job->job->queue == inp->job->job->queue) &&
			    (strcmp(job->user, inp->job->user) == 0) &&
			    find_resource_req(job->resreq, inp->err->rdef) != NULL)
				return 1;
			break;
		case SERVER_GROUP_RES_LIMIT_REACHED:
		case SERVER_BYGROUP_RES_LIMIT_REACHED:
			if ((strcmp(job->group, inp->job->group) == 0) &&
			    find_resource_req(job->resreq, inp->err->rdef) != NULL)
				return 1;
			break;
		case QUEUE_GROUP_RES_LIMIT_REACHED:
		case QUEUE_BYGROUP_RES_LIMIT_REACHED:
			if ((job->job->queue == inp->job->job->queue) &&
			    (strcmp(job->group, inp->job->group) == 0) &&
			    find_resource_req(job->resreq, inp->err->rdef) != NULL)
				return 1;
			break;
		case SERVER_PROJECT_RES_LIMIT_REACHED:
		case SERVER_BYPROJECT_RES_LIMIT_REACHED:
			if ((strcmp(job->user, inp->job->user) == 0) &&
			    find_resource_req(job->resreq, inp->err->rdef) != NULL)
				return 1;
			break;
		case QUEUE_PROJECT_RES_LIMIT_REACHED:
		case QUEUE_BYPROJECT_RES_LIMIT_REACHED:
			if ((job->job->queue == inp->job->job->queue) &&
			    (strcmp(job->project, inp->job->project) == 0) &&
			    find_resource_req(job->resreq, inp->err->rdef) != NULL)
				return 1;
			break;
		case QUEUE_JOB_LIMIT_REACHED:
			if (job->job->queue == inp->job->job->queue)
				return 1;
			break;
		case SERVER_USER_LIMIT_REACHED:
		case SERVER_BYUSER_JOB_LIMIT_REACHED:
			if (strcmp(job->user, inp->job->user) == 0)
				return 1;
			break;
		case QUEUE_USER_LIMIT_REACHED:
		case QUEUE_BYUSER_JOB_LIMIT_REACHED:
			if ((job->job->queue == inp->job->job->queue) &&
			    (strcmp(job->user, inp->job->user) == 0))
				return 1;
			break;
		case SERVER_GROUP_LIMIT_REACHED:
		case SERVER_BYGROUP_JOB_LIMIT_REACHED:
			if (strcmp(job->group, inp->job->group) == 0)
				return 1;
			break;
		case QUEUE_GROUP_LIMIT_REACHED:
		case QUEUE_BYGROUP_JOB_LIMIT_REACHED:
			if((job->job->queue == inp->job->job->queue) &&
			   (strcmp(job->group, inp->job->group) == 0))
				return 1;
			break;
		case SERVER_PROJECT_LIMIT_REACHED:
		case SERVER_BYPROJECT_JOB_LIMIT_REACHED:
			if (strcmp(job->project, inp->job->project) == 0)
				return 1;
			break;
		case QUEUE_PROJECT_LIMIT_REACHED:
		case QUEUE_BYPROJECT_JOB_LIMIT_REACHED:
			if ((job->job->queue == inp->job->job->queue) &&
			   (strcmp(job->project, inp->job->project) == 0))
				return 1;
			break;
		case SERVER_JOB_LIMIT_REACHED:
			return 1;

		case QUEUE_RESOURCE_LIMIT_REACHED:
			if (job->job->queue != inp->job->job->queue)
				return 0;
		case SERVER_RESOURCE_LIMIT_REACHED:
			for (req_scan = job->resreq; req_scan != NULL; req_scan = req_scan->next)
			{
				if (req_scan->def == inp->err->rdef && req_scan->amount > 0)
					return 1;
			}
			break;
		case INSUFFICIENT_RESOURCE:
			/* special check for vnode and host resource because those resources
			 * do not get into chunk level resources. So in such a case we
			 * compare the resource name with the chunk name
			 */
			if (inp->err->rdef == allres["vnode"]) {
				if (inp->err->arg2 != NULL && find_node_info(job->ninfo_arr, inp->err->arg2) != NULL)
					return 1;
			} else if (inp->err->rdef == allres["host"]) {
				if (inp->err->arg2 != NULL && find_node_by_host(job->ninfo_arr, inp->err->arg2) != NULL)
					return 1;
			} else {
				if (inp->err->rdef->type.is_non_consumable) {
					/* In the non-consumable case, we need to pass the job on.
					 * There is a case when a job requesting a non-specific
					 * select is allocated a node with a non-consumable.
					 * We will check nodes to see if they are useful in
					 * select_index_to_preempt
					 */
					return 1;
				}
				for (int index = 0; job->select->chunks[index] != NULL; index++)
				{
					for (req_scan = job->select->chunks[index]->req; req_scan != NULL; req_scan = req_scan->next)
					{
						if (req_scan->def == inp->err->rdef) {
							if (req_scan->type.is_non_consumable ||
								req_scan->amount > 0) {
									return 1;
							}
						}
					}
				}
			}
			break;
		case INSUFFICIENT_QUEUE_RESOURCE:
			if (job->job->queue != inp->job->job->queue)
				return 0;
		case INSUFFICIENT_SERVER_RESOURCE:
			if (find_resource_req(job->resreq, inp->err->rdef))
				return 1;
			break;
		default:
			return 0;
	}
	return 0;
}

/**
 * @brief   This function looks at a list of running jobs and create a subset of preemptable candidates
 *	    according to the high priority job and the reason why it couldn't run
 * @param[in] arr - List of running jobs (preemptable candidates)
 * @param[in] job - high priority job that couldn't run
 * @param[in] err - error structure that has the reason why high priority job couldn't run
 *
 * @return - resource_resv **
 * @retval - a newly allocated list of preemptable candidates
 * @retval - NULL if no jobs can be preempted
 */
resource_resv **filter_preemptable_jobs(resource_resv **arr, resource_resv *job, schd_error *err)
{
	struct resresv_filter arg;
	resource_resv **temp = NULL;
	int i;
	int arr_length;

	if ((arr == NULL) || (job == NULL) || (err == NULL))
		return NULL;

	arr_length = count_array(arr);

	switch (err->error_code) {
		/* list of resources we care about */
		case SERVER_USER_RES_LIMIT_REACHED:
		case SERVER_BYUSER_RES_LIMIT_REACHED:
		case QUEUE_USER_RES_LIMIT_REACHED:
		case QUEUE_BYUSER_RES_LIMIT_REACHED:

		case SERVER_GROUP_RES_LIMIT_REACHED:
		case SERVER_BYGROUP_RES_LIMIT_REACHED:
		case QUEUE_GROUP_RES_LIMIT_REACHED:
		case QUEUE_BYGROUP_RES_LIMIT_REACHED:

		case SERVER_PROJECT_RES_LIMIT_REACHED:
		case SERVER_BYPROJECT_RES_LIMIT_REACHED:
		case QUEUE_PROJECT_RES_LIMIT_REACHED:
		case QUEUE_BYPROJECT_RES_LIMIT_REACHED:

		case SERVER_JOB_LIMIT_REACHED:
		case SERVER_RESOURCE_LIMIT_REACHED:
		case QUEUE_JOB_LIMIT_REACHED:
		case QUEUE_RESOURCE_LIMIT_REACHED:

		case SERVER_USER_LIMIT_REACHED:
		case SERVER_BYUSER_JOB_LIMIT_REACHED:
		case QUEUE_USER_LIMIT_REACHED:
		case QUEUE_BYUSER_JOB_LIMIT_REACHED:

		case SERVER_GROUP_LIMIT_REACHED:
		case SERVER_BYGROUP_JOB_LIMIT_REACHED:
		case QUEUE_GROUP_LIMIT_REACHED:
		case QUEUE_BYGROUP_JOB_LIMIT_REACHED:

		case SERVER_PROJECT_LIMIT_REACHED:
		case SERVER_BYPROJECT_JOB_LIMIT_REACHED:
		case QUEUE_PROJECT_LIMIT_REACHED:
		case QUEUE_BYPROJECT_JOB_LIMIT_REACHED:

		case INSUFFICIENT_RESOURCE:
		case INSUFFICIENT_QUEUE_RESOURCE:
		case INSUFFICIENT_SERVER_RESOURCE:
			arg.job = job;
			arg.err = err;
			temp = resource_resv_filter(arr, arr_length, cull_preemptible_jobs, &arg, 0);
			if (temp == NULL)
				return NULL;
			if (temp[0] == NULL) {
				free(temp);
				return NULL;
			}
			return temp;
		default:
			/* For all other errors return the copy of list back again */
			temp = static_cast<resource_resv **>(malloc((arr_length + 1) * sizeof(resource_resv *)));
			if (temp == NULL) {
				log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG, __func__, MEM_ERR_MSG);
				return NULL;
			}
			for (i = 0; arr[i] != NULL; i++)
				temp[i] = arr[i];
			temp[i] = NULL;
			return temp;
	}
	return NULL;
}

/**
 * @brief   This function looks at the job's depend attribute string and creates
 *	    an array of job ids having runone dependency.
 * @param[in] depend_val - job's dependency string
 *
 * @return - char **
 * @retval - a newly allocated list of jobs with runone dependeny
 * @retval - NULL in case of error
 */
static char **parse_runone_job_list(char *depend_val) {
	char *start;
	const char *depend_type = "runone:";
	int i;
	int len = 1;
	char *r;
	char **ret = NULL;
	char *depend_str = NULL;

	if (depend_val == NULL)
		return NULL;
	else
	    depend_str = string_dup(depend_val);

	start = strstr(depend_str, depend_type);
	if (start == NULL) {
		free(depend_str);
		return NULL;
	}

	r = start + strlen(depend_type);
	for (i = 0; r[i] != '\0'; i++) {
		if (r[i] == ':')
			len++;
	}

	ret = static_cast<char **>(calloc(len + 1, sizeof(char *)));
	if (ret == NULL) {
		free(depend_str);
		return NULL;
	}
	for (i = 0;  i < len; i++) {
		auto job_delim = strcspn(r, ":");
		r[job_delim] = '\0';
		auto svr_delim = strcspn(r, "@");
		r[svr_delim] = '\0';
		ret[i] = string_dup(r);
		if (ret[i] == NULL) {
			free_ptr_array(ret);
			free(depend_str);
			return NULL;
		}
		r = r + job_delim + 1;
	}
	free(depend_str);
	return ret;
}

/**
 * @brief   This function processes every job's depend attribute and
 *	    associate the jobs with runone dependency to its dependent_jobs list.
 * @param[in] sinfo - server info structure
 *
 * @return - void
 */
void associate_dependent_jobs(server_info *sinfo) {
	int i;
	char **job_arr = NULL;

	if (sinfo == NULL)
		return;
	for (i = 0; sinfo->jobs[i] != NULL; i++) {
		if (sinfo->jobs[i]->job->depend_job_str != NULL) {
			job_arr = parse_runone_job_list(sinfo->jobs[i]->job->depend_job_str);
			if (job_arr != NULL) {
				int j;
				int len = count_array(job_arr);
				sinfo->jobs[i]->job->dependent_jobs = static_cast<resource_resv **>(calloc((len + 1), sizeof(resource_resv *)));
				sinfo->jobs[i]->job->dependent_jobs[len] = NULL;
				for (j = 0; job_arr[j] != NULL; j++) {
					resource_resv *jptr;
					jptr = find_resource_resv(sinfo->jobs, job_arr[j]);
					if (jptr != NULL)
						sinfo->jobs[i]->job->dependent_jobs[j] = jptr;
					free(job_arr[j]);
				}
			}
		}
		if (job_arr != NULL) {
			free(job_arr);
			job_arr = NULL;
		}
	}
	return;
}

/**
 * @brief This function associates the subjob passed in to its parent job.
 *
 * @param[in] pjob	The subjob that needs association
 * @param[in] sinfo	server info structure
 *
 * @return int
 * @retval 1 - Failure
 * @retval 0 - Success
 */
int associate_array_parent(resource_resv *pjob, server_info *sinfo) {
	resource_resv *parent = NULL;

	if (pjob == NULL || sinfo == NULL || !pjob->job->is_subjob)
		return 1;

	parent = find_resource_resv(sinfo->jobs, pjob->job->array_id);
	if (parent == NULL)
		return 1;

	pjob->job->parent_job = parent;
	parent->job->running_subjobs++;

	return 0;
}

/**
 *	@brief set job's start, end, duration, and STF attributes if needed
 *
 * 	@param[in] pbs_sd - used to set estimated.soft_walltime
 * 	@param[in] resresv - the job
 * 	@param[in] server_time - current time in cycle
 * 
 * 	@return void
 */
void set_job_times(int pbs_sd, resource_resv *resresv, time_t server_time)
{
	long duration;
	resource_req *soft_walltime_req = NULL;
	resource_req *walltime_req = NULL;
	/* Find out if it is a shrink-to-fit job.
	 * If yes, set the duration to max walltime.
	 */
	auto req = find_resource_req(resresv->resreq, allres["min_walltime"]);
	if (req != NULL) {
		resresv->is_shrink_to_fit = true;
		/* Set the min duration */
		resresv->min_duration = (time_t) req->amount;
		req = find_resource_req(resresv->resreq, allres["max_walltime"]);

#ifdef NAS /* localmod 026 */
		/* if no max_walltime is set then we want to look at what walltime
		 * is (if it's set at all) - it may be user-specified, queue default,
		 * queue max, or server max.
		 */
		if (req == NULL) {
			req = find_resource_req(resresv->resreq, allres["walltime"]);

			/* if walltime is set, use it if it's greater than min_walltime */
			if (req != NULL && resresv->min_duration > req->amount) {
				req = find_resource_req(resresv->resreq, allres["min_walltime"]);
			}
		}
#endif /* localmod 026 */
	}

	if ((req == NULL) || (resresv->job->is_running == true)) {
		soft_walltime_req = find_resource_req(resresv->resreq, allres["soft_walltime"]);
		walltime_req = find_resource_req(resresv->resreq, allres["walltime"]);
		if (soft_walltime_req != NULL)
			req = soft_walltime_req;
		else
			req = walltime_req;
	}

	if (req != NULL)
		duration = (long) req->amount;
	else /* set to virtual job infinity: 5 years */
		duration = JOB_INFINITY;

	if (walltime_req != NULL)
		resresv->hard_duration = (long) walltime_req->amount;
	else if (resresv->min_duration != UNSPECIFIED)
		resresv->hard_duration = resresv->min_duration;
	else
		resresv->hard_duration = JOB_INFINITY;

	if (resresv->job->stime != UNSPECIFIED &&
	    !(resresv->job->is_queued || resresv->job->is_suspended) &&
	    resresv->ninfo_arr != NULL) {
		auto start = resresv->job->stime;
		time_t end;

		/* if a job is exiting, then its end time can be more closely
			 * estimated by setting it to now + EXITING_TIME
			 */
		if (resresv->job->is_exiting)
			end = server_time + EXITING_TIME;
		/* Normal Case: Job's end is start + duration and it ends in the future */
		else if (start + duration >= server_time)
			end = start + duration;
		/* Duration has been exceeded - either extend soft_walltime or expect the job to be killed */
		else {
			if (soft_walltime_req != NULL) {
				duration = extend_soft_walltime(resresv, server_time);
				if (duration > soft_walltime_req->amount) {
					char timebuf[128];
					convert_duration_to_str(duration, timebuf, 128);
					update_job_attr(pbs_sd, resresv, ATTR_estimated, "soft_walltime", timebuf, NULL, UPDATE_NOW);
				}
			} else
				/* Job has exceeded its walltime.  It'll soon be killed and be put into the exiting state.
				 * Change the duration of the job to match the current situation and assume it will end in
				 * now + EXITING_TIME
				 */
				duration = server_time - start + EXITING_TIME;
			end = start + duration;
		}
		resresv->start = start;
		resresv->end = end;
	}
	resresv->duration = duration;
}