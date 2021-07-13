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
 * @file    server_info.c
 *
 * @brief
 * server_info.c - contains functions related to server_info structure.
 *
 * Functions included are:
 * 	query_server_info()
 * 	query_server_dyn_res()
 * 	query_sched_obj()
 * 	find_alloc_resource()
 * 	find_alloc_resource_by_str()
 * 	find_resource_by_str()
 * 	find_resource()
 * 	free_server_info()
 * 	free_resource_list()
 * 	free_resource()
 * 	new_server_info()
 * 	new_resource()
 * 	create_resource()
 * 	add_resource_list()
 * 	add_resource_value()
 * 	add_resource_str_arr()
 * 	add_resource_bool()
 * 	free_server()
 * 	update_server_on_run()
 * 	update_server_on_end()
 * 	create_server_arrays()
 * 	check_run_job()
 * 	check_exit_job()
 * 	check_susp_job()
 * 	check_running_job_not_in_reservation()
 * 	check_running_job_in_reservation()
 * 	check_resv_running_on_node()
 * 	dup_server_info()
 * 	dup_resource_list()
 * 	dup_selective_resource_list()
 * 	dup_ind_resource_list()
 * 	dup_resource()
 * 	is_unassoc_node()
 * 	new_counts()
 * 	free_counts()
 * 	free_counts_list()
 * 	dup_counts()
 * 	dup_counts_list()
 * 	find_counts()
 * 	find_alloc_counts()
 * 	update_counts_on_run()
 * 	update_counts_on_end()
 * 	counts_max()
 * 	update_universe_on_end()
 * 	set_resource()
 * 	find_indirect_resource()
 * 	resolve_indirect_resources()
 * 	update_preemption_on_run()
 * 	read_formula()
 * 	dup_status()
 * 	free_queue_list()
 * 	create_total_counts()
 * 	update_total_counts()
 * 	update_total_counts_on_end()
 * 	get_sched_rank()
 * 	add_queue_to_list()
 * 	find_queue_list_by_priority()
 * 	append_to_queue_list()
 *
 */

#include <pbs_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>

#include "pbs_entlim.h"
#include "pbs_ifl.h"
#include "pbs_error.h"
#include "log.h"
#include "pbs_share.h"
#include "libpbs.h"
#include "constant.h"
#include "config.h"
#include "server_info.h"
#include "queue_info.h"
#include "job_info.h"
#include "misc.h"
#include "node_info.h"
#include "globals.h"
#include "resv_info.h"
#include "sort.h"
#include "resource_resv.h"
#include "state_count.h"
#include "node_partition.h"
#include "resource.h"
#include "assert.h"
#include "limits_if.h"
#include "resource.h"
#include "pbs_internal.h"
#include "simulate.h"
#include "fairshare.h"
#include "check.h"
#include "fifo.h"
#include "buckets.h"
#include "parse.h"
#include "hook.h"
#include "libpbs.h"
#ifdef NAS
#include "site_code.h"
#endif

extern char **environ;

/**
 *	@brief
 *		creates a structure of arrays consisting of a server
 *		and all the queues and jobs that reside in that server
 *
 * @par Order of Query
 *		query_server()
 *      -> query_sched()
 *	 	-> query_nodes()
 *	 	-> query_queues()
 *	    -> query_jobs()
 *	 	->query_reservations()
 *
 * @param[in]	pol		-	input policy structure - will be dup'd
 * @param[in]	pbs_sd	-	connection to pbs_server
 *
 * @return	the server_info struct
 * @retval	server_info -> policy - policy structure for cycle
* @retval	NULL	: error
 *
 */
server_info *
query_server(status *pol, int pbs_sd)
{
	struct batch_status *server;	/* info about the server */
	struct batch_status *bs_resvs;	/* batch status of the reservations */
	server_info *sinfo;		/* scheduler internal form of server info */
	counts *cts;			/* used to count running per user/grp */
	int num_express_queues = 0;	/* number of express queues */
	status *policy;
	int job_arrays_associated = FALSE;
	int i;

	if (pol == NULL)
		return NULL;

	PBSD_server_ready(pbs_sd);

	if (allres.empty())
		if (update_resource_defs(pbs_sd) == false)
			return NULL;

	/* get server information from pbs server */
	if ((server = send_statserver(pbs_sd, NULL, NULL)) == NULL) {
		const char *errmsg = pbs_geterrmsg(pbs_sd);
		if (errmsg == NULL)
			errmsg = "";
		log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_SERVER, LOG_NOTICE, "server_info",
			   "pbs_statserver failed: %s (%d)", errmsg, pbs_errno);
		return NULL;
	}

	/* convert batch_status structure into server_info structure */
	if ((sinfo = query_server_info(pol, server)) == NULL) {
		pbs_statfree(server);
		return NULL;
	}

	/* We dup'd the policy structure for the cycle */
	policy = sinfo->policy;

	if(query_server_dyn_res(sinfo) == -1) {
		pbs_statfree(server);
		sinfo->fstree = NULL;
		free_server(sinfo);
		return NULL;
	}

	if (!dflt_sched && (sc_attrs.partition == NULL)) {
		log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_SERVER, LOG_ERR, __func__, "Scheduler does not contain a partition");
		pbs_statfree(server);
		sinfo->fstree = NULL;
		free_server(sinfo);
		return NULL;
	}

	/* to avoid a possible race condition in which the time it takes to
	 * query nodes is long enough that a reservation may have crossed
	 * into running state, we stat the reservation just before nodes and
	 * will populate internal data structures based on this batch status
	 * after all other data is queried
	 */
	bs_resvs = stat_resvs(pbs_sd);

	/* get the nodes, if any - NOTE: will set sinfo -> num_nodes */
	if ((sinfo->nodes = query_nodes(pbs_sd, sinfo)) == NULL) {
		pbs_statfree(server);
		sinfo->fstree = NULL;
		free_server(sinfo);
		pbs_statfree(bs_resvs);
		return NULL;
	}

	/* sort the nodes before we filter them down to more useful lists */
	if (!policy->node_sort->empty())
		qsort(sinfo->nodes, sinfo->num_nodes, sizeof(node_info *),
			multi_node_sort);

	/* get the queues */
	if ((sinfo->queues = query_queues(policy, pbs_sd, sinfo)) == NULL) {
		pbs_statfree(server);
		sinfo->fstree = NULL;
		free_server(sinfo);
		pbs_statfree(bs_resvs);
		return NULL;
	}

	if (sinfo->has_nodes_assoc_queue)
		sinfo->unassoc_nodes =
			node_filter(sinfo->nodes, sinfo->num_nodes, is_unassoc_node, NULL, 0);
	else
		sinfo->unassoc_nodes = sinfo->nodes;

	/* count the queues and total up the individual queue states
	 * for server totals. (total up all the state_count structs)
	 */
	for (int i = 0; sinfo->queues[i] != NULL; i++) {
		sinfo->num_queues++;
		total_states(&(sinfo->sc), &(sinfo->queues[i]->sc));

		if (sinfo->queues[i]->priority >= sc_attrs.preempt_queue_prio)
			num_express_queues++;
	}

	if (num_express_queues > 1)
		sinfo->has_mult_express = 1;


	/* sort the queues before we collect the jobs list (i.e. set_jobs())
	 * in the case we don't sort the jobs and don't have by_queue turned on
	 */
	if ((policy->round_robin == 1) || (policy->by_queue == 1))
		qsort(sinfo->queues, sinfo->num_queues, sizeof(queue_info *),
			cmp_queue_prio_dsc);
	if (policy->round_robin == 1) {
		/* queues are already sorted in descending order of their priority */
		for (i = 0; i < sinfo->num_queues; i++) {
			auto ret_val = add_queue_to_list(&sinfo->queue_list, sinfo->queues[i]);
			if (ret_val == 0) {
				sinfo->fstree = NULL;
				free_server(sinfo);
				pbs_statfree(bs_resvs);
				return NULL;
			}
		}
	}

	/* get reservations, if any - NOTE: will set sinfo -> num_resvs */
	sinfo->resvs = query_reservations(pbs_sd, sinfo, bs_resvs);
	pbs_statfree(bs_resvs);

	if (create_server_arrays(sinfo) == 0) { /* bad stuff happened */
		sinfo->fstree = NULL;
		free_server(sinfo);
		return NULL;
	}
#ifdef NAS /* localmod 050 */
	/* Give site a chance to tweak values before jobs are sorted */
	if (site_tidy_server(sinfo) == 0) {
		free_server(sinfo);
		return NULL;
	}
#endif /* localmod 050 */
	associate_dependent_jobs(sinfo);

	/* create res_to_check arrays based on current jobs/resvs */
	policy->resdef_to_check = collect_resources_from_requests(sinfo->all_resresv);
	for (const auto& rd : policy->resdef_to_check) {
		if (!(rd == allres["host"] || rd == allres["vnode"]))
			policy->resdef_to_check_no_hostvnode.insert(rd);
		
		if (rd->flags & ATR_DFLAG_RASSN)
			policy->resdef_to_check_rassn.insert(rd);

		if ((rd->flags & ATR_DFLAG_RASSN) && (rd->flags & ATR_DFLAG_CVTSLT))
			policy->resdef_to_check_rassn_select.insert(rd);
	}

	sinfo->calendar = create_event_list(sinfo);

	sinfo->running_jobs =
		resource_resv_filter(sinfo->jobs, sinfo->sc.total, check_run_job,
				     NULL, FILTER_FULL);
	sinfo->exiting_jobs = resource_resv_filter(sinfo->jobs,
						   sinfo->sc.total, check_exit_job, NULL, 0);
	if (sinfo->running_jobs == NULL || sinfo->exiting_jobs == NULL) {
		sinfo->fstree = NULL;
		free_server(sinfo);
		return NULL;
	}

	if (sinfo->has_soft_limit || sinfo->has_hard_limit) {
		counts *allcts;
		allcts = find_alloc_counts(sinfo->alljobcounts, PBS_ALL_ENTITY);
		if (sinfo->alljobcounts == NULL)
			sinfo->alljobcounts = allcts;
		job_arrays_associated = TRUE;
		/* set the user, group , project counts */
		for (int i = 0; sinfo->running_jobs[i] != NULL; i++) {
			cts = find_alloc_counts(sinfo->user_counts,
						sinfo->running_jobs[i]->user);
			if (sinfo->user_counts == NULL)
				sinfo->user_counts = cts;

			update_counts_on_run(cts, sinfo->running_jobs[i]->resreq);

			cts = find_alloc_counts(sinfo->group_counts,
						sinfo->running_jobs[i]->group);

			if (sinfo->group_counts == NULL)
				sinfo->group_counts = cts;

			update_counts_on_run(cts, sinfo->running_jobs[i]->resreq);

			cts = find_alloc_counts(sinfo->project_counts,
						sinfo->running_jobs[i]->project);

			if (sinfo->project_counts == NULL)
				sinfo->project_counts = cts;

			update_counts_on_run(cts, sinfo->running_jobs[i]->resreq);

			update_counts_on_run(allcts, sinfo->running_jobs[i]->resreq);
			/* Since we are already looping on running jobs, associcate running
			 * subjobs to their parent.
			 */
			if ((sinfo->running_jobs[i]->job->is_subjob) &&
			    (associate_array_parent(sinfo->running_jobs[i], sinfo) == 1)) {
				sinfo->fstree = NULL;
				free_server(sinfo);
				return NULL;
			}
		}
		create_total_counts(sinfo, NULL, NULL, SERVER);
	}
	if (job_arrays_associated == FALSE) {
		for (i = 0; sinfo->running_jobs[i] != NULL; i++) {
			if ((sinfo->running_jobs[i]->job->is_subjob) &&
			    (associate_array_parent(sinfo->running_jobs[i], sinfo) == 1)) {
				sinfo->fstree = NULL;
				free_server(sinfo);
				return NULL;
			}
		}
	}

	policy->equiv_class_resdef = create_resresv_sets_resdef(policy);
	sinfo->equiv_classes = create_resresv_sets(policy, sinfo);

	/* To avoid duplicate accounting of jobs on nodes, we are only interested in
	 * jobs that are bound to the server nodes and not those bound to reservation
	 * nodes, which are accounted for by collect_jobs_on_nodes in
	 * query_reservation, hence the use of the filtered list of jobs
	 */
	auto jobs_alive = resource_resv_filter(sinfo->jobs, sinfo->sc.total, check_running_job_not_in_reservation, NULL, 0);
	collect_jobs_on_nodes(sinfo->nodes, jobs_alive, count_array(jobs_alive), DETECT_GHOST_JOBS);

	/* Now that the job_arr is created, garbage collect the jobs */
	free(jobs_alive);

	collect_resvs_on_nodes(sinfo->nodes, sinfo->resvs, sinfo->num_resvs);

	sinfo->unordered_nodes = static_cast<node_info **>(malloc((sinfo->num_nodes + 1) * sizeof(node_info *)));
	if (sinfo->unordered_nodes == NULL) {
		sinfo->fstree = NULL;
		free_server(sinfo);
		return NULL;
	}

	/* Ideally we'd query everything about a node in query_node().  We query
	 * nodes very early on in the query process.  Not all the information
	 * necessary for a node is available at that time.  We need to delay it till here.
	 */
	for (i = 0; sinfo->nodes[i] != NULL; i++) {
		auto *ninfo = sinfo->nodes[i];
		ninfo->nodesig = create_resource_signature(ninfo->res,
							   policy->resdef_to_check_no_hostvnode, ADD_ALL_BOOL);
		ninfo->nodesig_ind = add_str_to_unique_array(&(sinfo->nodesigs),
							     ninfo->nodesig);

		if (ninfo->has_ghost_job)
			create_resource_assn_for_node(ninfo);

		sinfo->nodes[i]->node_ind = i;
		sinfo->unordered_nodes[i] = ninfo;
	}
	sinfo->unordered_nodes[i] = NULL;

	generic_sim(sinfo->calendar, TIMED_RUN_EVENT, 0, 0, add_node_events, NULL, NULL);

	/* Create placement sets  after collecting jobs on nodes because
	 * we don't want to account for resources consumed by ghost jobs
	 */
	create_placement_sets(policy, sinfo);
	if (!sinfo->node_group_enable && sinfo->node_group_key != NULL &&
	    strcmp(sinfo->node_group_key[0], "msvr_node_group") == 0) {
		auto np = create_node_partitions(policy, sinfo->unassoc_nodes,
					    sinfo->node_group_key, NP_NONE, &sinfo->num_parts);

		/* For each job, we'll need the placement set of nodes which belong to its server
		 * So, we need to associate psets with their respective server ids
		 */
		if (np != NULL) {
			int i;

			for (i = 0; i < sinfo->num_parts; i++) {
				sinfo->svr_to_psets[np[i]->ninfo_arr[0]->svr_inst_id] = np[i];
			}
		}
		free(np);
	}

	sinfo->buckets = create_node_buckets(policy, sinfo->nodes, sinfo->queues, UPDATE_BUCKET_IND);

	if (sinfo->buckets != NULL) {
		int ct;
		ct = count_array(sinfo->buckets);
		qsort(sinfo->buckets, ct, sizeof(node_bucket *), multi_bkt_sort);
	}

	pbs_statfree(server);

	return sinfo;
}

/**
 * @brief
 * 		takes info from a batch_status structure about
 *		a server into a server_info structure for easy access
 *
 * @param[in]	pol		-	scheduler policy structure
 * @param[in]	server	-	batch_status struct of server info
 *							chain possibly NULL
 *
 * @return	newly allocated and filled server_info struct
 *
 */
server_info *
query_server_info(status *pol, struct batch_status *server)
{
	struct attrl *attrp;	/* linked list of attributes */
	server_info *sinfo;	/* internal scheduler structure for server info */
	schd_resource *resp;	/* a resource to help create the resource list */
	sch_resource_t count;	/* used to convert string -> integer */
	char *endp;		/* used with strtol() */
	status *policy;

	if (pol == NULL || server == NULL)
		return NULL;

	if ((sinfo = new_server_info(1)) == NULL)
		return NULL;		/* error */

	if (sinfo->liminfo == NULL)
		return NULL;

	if ((sinfo->name = string_dup(server->name)) == NULL) {
		free_server_info(sinfo);
		return NULL;
	}

	if ((sinfo->policy = dup_status(pol)) == NULL) {
		free_server_info(sinfo);
		return NULL;
	}

	policy = sinfo->policy;

	/* set the time to the current time */
	sinfo->server_time = policy->current_time;

	attrp = server->attribs;

	while (attrp != NULL) {
		if (is_reslimattr(attrp)) {
			(void) lim_setlimits(attrp, LIM_RES, sinfo->liminfo);
			if(strstr(attrp->value, "u:") != NULL)
				sinfo->has_user_limit = 1;
			if(strstr(attrp->value, "g:") != NULL)
				sinfo->has_grp_limit = 1;
			if(strstr(attrp->value, "p:") != NULL)
				sinfo->has_proj_limit = 1;
			if(strstr(attrp->value, "o:") != NULL)
				sinfo->has_all_limit = 1;
		} else if (is_runlimattr(attrp)) {
			(void) lim_setlimits(attrp, LIM_RUN, sinfo->liminfo);
			if(strstr(attrp->value, "u:") != NULL)
				sinfo->has_user_limit = 1;
			if(strstr(attrp->value, "g:") != NULL)
				sinfo->has_grp_limit = 1;
			if(strstr(attrp->value, "p:") != NULL)
				sinfo->has_proj_limit = 1;
			if(strstr(attrp->value, "o:") != NULL)
				sinfo->has_all_limit = 1;
		} else if (is_oldlimattr(attrp)) {
			const char *limname = convert_oldlim_to_new(attrp);
			(void) lim_setlimits(attrp, LIM_OLD, sinfo->liminfo);

			if(strstr(limname, "u:") != NULL)
				sinfo->has_user_limit = 1;
			if(strstr(limname, "g:") != NULL)
				sinfo->has_grp_limit = 1;
			/* no need to check for project limits because there were no old style project limits */
		} else if (!strcmp(attrp->name, ATTR_NodeGroupEnable)) {
			if (!strcmp(attrp->value, ATR_TRUE))
				sinfo->node_group_enable = 1;
			else
				sinfo->node_group_enable = 0;
		} else if (!strcmp(attrp->name, ATTR_NodeGroupKey))
			sinfo->node_group_key = break_comma_list(attrp->value);
		else if (!strcmp(attrp->name, ATTR_job_sort_formula)) {	/* Deprecated */
			sinfo->job_sort_formula = read_formula();
			if (policy->sort_by->size() > 1) /* 0 is the formula itself */
				log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_SERVER, LOG_DEBUG, __func__,
					"Job sorting formula and job_sort_key are incompatible.  "
					"The job sorting formula will be used.");

		} else if (!strcmp(attrp->name, ATTR_rescavail)) { /* resources_available*/
			resp = find_alloc_resource_by_str(sinfo->res, attrp->resource);

			if (resp != NULL) {
				if (sinfo->res == NULL)
					sinfo->res = resp;

				if (set_resource(resp, attrp->value, RF_AVAIL) == 0) {
					free_server_info(sinfo);
					return NULL;
				}
			}
		} else if (!strcmp(attrp->name, ATTR_rescassn)) { /* resources_assigned */
			resp = find_alloc_resource_by_str(sinfo->res, attrp->resource);
			if (sinfo->res == NULL)
				sinfo->res = resp;
			if (resp != NULL) {
				if (set_resource(resp, attrp->value, RF_ASSN) == 0) {
					free_server_info(sinfo);
					return NULL;
				}
			}
		} else if (!strcmp(attrp->name, ATTR_EligibleTimeEnable)) {
			if (!strcmp(attrp->value, ATR_TRUE))
				sinfo->eligible_time_enable = 1;
			else
				sinfo->eligible_time_enable = 0;
		} else if (!strcmp(attrp->name, ATTR_ProvisionEnable)) {
			if (!strcmp(attrp->value, ATR_TRUE))
				sinfo->provision_enable = 1;
			else
				sinfo->provision_enable = 0;
		} else if (!strcmp(attrp->name, ATTR_power_provisioning)) {
			if (!strcmp(attrp->value, ATR_TRUE))
				sinfo->power_provisioning = 1;
			else
				sinfo->power_provisioning = 0;
		} else if (!strcmp(attrp->name, ATTR_backfill_depth)) {
			count = strtol(attrp->value, &endp, 10);
			if (*endp == '\0')
				sinfo->policy->backfill_depth = count;
			if (count == 0)
				sinfo->policy->backfill = 0;
		} else if(!strcmp(attrp->name, ATTR_restrict_res_to_release_on_suspend)) {
			char **resl;
			resl = break_comma_list(attrp->value);
			if(resl != NULL) {
				policy->rel_on_susp = resstr_to_resdef(resl);
				free_string_array(resl);
			}
		} else if(!strcmp(attrp->name, ATTR_has_runjob_hook)) {
			if (!strcmp(attrp->value, ATR_TRUE))
				sinfo->has_runjob_hook = 1;
			else
				sinfo->has_runjob_hook = 0;
		}
		attrp = attrp->next;
	}

	if (sinfo->job_sort_formula == NULL && sc_attrs.job_sort_formula != NULL) {
		sinfo->job_sort_formula = string_dup(sc_attrs.job_sort_formula);
		if (sinfo->job_sort_formula == NULL) {
			free_server_info(sinfo);
			return NULL;
		}
	}

	if (has_hardlimits(sinfo->liminfo))
		sinfo->has_hard_limit = 1;
	if (has_softlimits(sinfo->liminfo))
		sinfo->has_soft_limit = 1;

	/* Since we want to keep track of fairshare changes from cycle to cycle
	 * copy in the global fairshare tree root.  Be careful to not free it
	 * at the end of the cycle.
	 */
	sinfo->fstree = fstree;
#ifdef NAS /* localmod 034 */
	site_set_share_head(sinfo);
#endif /* localmod 034 */

	if (sinfo->node_group_key == NULL && get_num_servers() > 1) {
		/* Set node_group_key to msvr_node_group for server local placement */
		sinfo->node_group_key = break_comma_list(const_cast<char *>("msvr_node_group"));

		/* This will ensure that create_placement_sets doesn't create placement sets,
		 * we'll create directly by calling create_node_partitions
		 */
		sinfo->node_group_enable = 0;
	}
	return sinfo;
}

/**
 * @brief
 * 		execute all configured server_dyn_res scripts
 *
 * @param[in]	sinfo	-	server info
 *
 * @retval	0	: on success
 * @retval -1	: on error
 */
int
query_server_dyn_res(server_info *sinfo)
{
	int k;
	int pipe_err;
	char res_zero[] = "0";	/* dynamic res failure implies resource <-0 */
	schd_resource *res;		/* used for updating node resources */
	FILE *fp;			/* for popen() for res_assn */


	for (const auto& dr : conf.dynamic_res) {
		res = find_alloc_resource_by_str(sinfo->res, dr.res);
		if (res != NULL) {
			fd_set set;
			sigset_t allsigs;
			pid_t pid = 0;			/* pid of child */
			int pdes[2];
			char buf[256]; /* buffer for reading from pipe */
			k = 0;
			buf[0] = '\0';

			if (sinfo->res == NULL)
				sinfo->res = res;

			pipe_err = errno = 0;

			/* Make sure file does not have open permissions */
			#if !defined(DEBUG) && !defined(NO_SECURITY_CHECK)
				int err;
				err = tmp_file_sec_user(const_cast<char *>(dr.script_name.c_str()), 0, 1, S_IWGRP|S_IWOTH, 1, getuid());
				if (err != 0) {
					log_eventf(PBSEVENT_SECURITY, PBS_EVENTCLASS_SERVER, LOG_ERR, "server_dyn_res",
						"error: %s file has a non-secure file access, setting resource %s to 0, errno: %d",
						dr.script_name.c_str(), res->name, err);
					set_resource(res, res_zero, RF_AVAIL);
					continue;
				}
			#endif

			if (pipe(pdes) < 0) {
				pipe_err = errno;
			}
			if (!pipe_err) {
				switch(pid = fork()) {
				case -1:	/* error */
					close(pdes[0]);
					close(pdes[1]);
					pipe_err = errno;
					break;
				case 0:		/* child */
					close(pdes[0]);
					if (pdes[1] != STDOUT_FILENO) {
						dup2(pdes[1], STDOUT_FILENO);
						close(pdes[1]);
					}
					setpgid(0, 0);
					if (sigemptyset(&allsigs) == -1) {
						log_err(errno, __func__, "sigemptyset failed");
					}
					if (sigprocmask(SIG_SETMASK, &allsigs, NULL) == -1) {	/* unblock all signals */
						log_err(errno, __func__, "sigprocmask(UNBLOCK)");
					}

					char *argv[4];
					argv[0] = const_cast<char *>("/bin/sh");
					argv[1] = const_cast<char *>("-c");
					argv[2] = const_cast<char *>(dr.command_line.c_str());
					argv[3] = NULL;

					execve("/bin/sh", argv, environ);
					_exit(127);
				}
			}

			k = 0;
			if (!pipe_err) {
				int ret;
				FD_ZERO(&set);
				FD_SET(pdes[0], &set);
				if (sc_attrs.server_dyn_res_alarm) {
					struct timeval timeout;
					timeout.tv_sec = sc_attrs.server_dyn_res_alarm;
					timeout.tv_usec = 0;
					ret = select(FD_SETSIZE, &set, NULL, NULL, &timeout);
				} else {
					ret = select(FD_SETSIZE, &set, NULL, NULL, NULL);
				}
				if (ret == -1) {
					log_eventf(PBSEVENT_ERROR, PBS_EVENTCLASS_SERVER, LOG_DEBUG, "server_dyn_res",
					"Select() failed for script %s", dr.command_line.c_str());
				} else if (ret == 0) {
					log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_SERVER, LOG_DEBUG, "server_dyn_res",
					"Program %s timed out", dr.command_line.c_str());
				}
				if (pid > 0) {
					close(pdes[1]);
					if (ret > 0) {
						/* Parent; only open if child created and select showed sth to read,
						* but assume fdopen can't fail
						*/
						fp = fdopen(pdes[0], "r");
						if (fgets(buf, sizeof(buf), fp) == NULL) {
							pipe_err = errno;
						} else
							k = strlen(buf);
						
						fclose(fp);
					} else
						close(pdes[0]);
				}
			}

			if (k > 0) {
				buf[k] = '\0';
				/* chop \r or \n from buf so that is_num() doesn't think it's a str */
				while (--k) {
					if ((buf[k] != '\n') && (buf[k] != '\r'))
						break;
					buf[k] = '\0';
				}
				if (set_resource(res, buf, RF_AVAIL) == 0) {
					log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_SERVER, LOG_DEBUG, "server_dyn_res",
						"Script %s returned bad output", dr.command_line.c_str());
					(void) set_resource(res, res_zero, RF_AVAIL);
				}
			} else {
				if (pipe_err != 0)
					log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_SERVER, LOG_DEBUG, "server_dyn_res",
						"Can't pipe to program %s: %s", dr.command_line.c_str(), strerror(pipe_err));
				log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_SERVER, LOG_DEBUG, "server_dyn_res",
					"Setting resource %s to 0", res->name);
				(void) set_resource(res, res_zero, RF_AVAIL);
			}
			if (res->type.is_non_consumable)
				log_eventf(PBSEVENT_DEBUG2, PBS_EVENTCLASS_SERVER, LOG_DEBUG, "server_dyn_res",
					"%s = %s", dr.command_line.c_str(), res_to_str(res, RF_AVAIL));
			else
				log_eventf(PBSEVENT_DEBUG2, PBS_EVENTCLASS_SERVER, LOG_DEBUG, "server_dyn_res",
					"%s = %s (\"%s\")", dr.command_line.c_str(), res_to_str(res, RF_AVAIL), buf);

			if (pid > 0) {
				kill(-pid, SIGTERM);
				if (waitpid(pid, NULL, WNOHANG) == 0) {
					usleep(250000);
					if (waitpid(pid, NULL, WNOHANG) == 0) {
						kill(-pid, SIGKILL);
						waitpid(pid, NULL, 0);
					}
				}
			}
		}
	}

	return 0;
}

/**
 * @brief
 * 		try and find a resource by resdef, and if it is not
 *		there, allocate space for it and add it to the resource list
 *
 * @param[in]	resplist	- 	the resource list
 * @param[in]	name 		-	the name of the resource
 *
 * @return	schd_resource
 * @retval	NULL	: error
 *
 * @par MT-Safe:	no
 */
schd_resource *
find_alloc_resource(schd_resource *resplist, resdef *def)
{
	schd_resource *resp;		/* used to search through list of resources */
	schd_resource *prev = NULL;	/* the previous resources in the list */

	if (def == NULL)
		return NULL;

	for (resp = resplist; resp != NULL && resp->def != def; resp = resp->next) {
		prev = resp;
	}

	if (resp == NULL) {
		if ((resp = new_resource()) == NULL)
			return NULL;

		resp->def = def;
		resp->type = def->type;
		resp->name = def->name.c_str();

		if (prev != NULL)
			prev->next = resp;
	}

	return resp;
}

/**
 * @brief
 * 		try and find a resource by name, and if it is not
 *		there, allocate space for it and add it to the resource list
 *
 * @param[in]	resplist 	- 	the resource list
 * @param[in]	name 		- 	the name of the resource
 *
 * @return	schd_resource
 * @retval	NULL :	Error
 *
 * @par MT-Safe:	no
 */
schd_resource *
find_alloc_resource_by_str(schd_resource *resplist, const char *name)
{
	schd_resource *resp;		/* used to search through list of resources */
	schd_resource *prev = NULL;	/* the previous resources in the list */

	if (name == NULL)
		return NULL;

	for (resp = resplist; resp != NULL && strcmp(resp->name, name);
		resp = resp->next) {
		prev = resp;
	}

	if (resp == NULL) {
		if ((resp = create_resource(name, NULL, RF_NONE)) == NULL)
			return NULL;

		if (prev != NULL)
			prev->next = resp;
	}

	return resp;
}
schd_resource *find_alloc_resource_by_str(schd_resource *resplist, const std::string& name)
{
	return find_alloc_resource_by_str(resplist, name.c_str());
}

/**
 * @brief
 * 		finds a resource by string in a resource list
 *
 * @param[in]	reslist - 	the resource list
 * @param[in]	name	- 	the name of the resource
 *
 * @return	schd_resource
 * @retval	NULL	: if not found
 *
 * @par MT-Safe:	no
 */
schd_resource *
find_resource_by_str(schd_resource *reslist, const char *name)
{
	schd_resource *resp;	/* used to search through list of resources */

	if (reslist == NULL || name == NULL)
		return NULL;

	resp = reslist;

	while (resp != NULL && strcmp(resp->name, name))
		resp = resp->next;

	return resp;
}

schd_resource *
find_resource_by_str(schd_resource *reslist, const std::string& name)
{
	return find_resource_by_str(reslist, name.c_str());
}

/**
 * @brief
 * 		find resource by resource definition
 *
 * @param 	reslist - 	resource list to search
 * @param 	def 	- 	resource definition to search for
 *
 * @return	the found resource
 * @retval	NULL	: if not found
 */
schd_resource *
find_resource(schd_resource *reslist, resdef *def)
{
	schd_resource *resp;

	if (reslist == NULL || def == NULL)
		return NULL;

	resp = reslist;

	while (resp != NULL && resp->def != def)
		resp = resp->next;

	return resp;
}

/**
 * @brief	free the sinfo->svr_to_psets map
 * 			Note: this won't be needed once we convert node_partition to a class
 *
 * @param[out]	spsets - the sinfo->svr_to_psets map
 *
 * @return void
 */
static void
free_server_psets(std::unordered_map<std::string, node_partition *>& spsets)
{
	for (auto& spset : spsets) {
		free_node_partition(spset.second);
		spset.second = NULL;
	}
}

/**
 * @brief	dup a sinfo->svr_to_psets map (deep copy)
 * 			Note: this might not be needed once we convert node_partition to a class
 *
 * @param[in]	spsets - map of server psets
 * @param[in]	sinfo - sinfo in the duplicated universe
 *
 * @return std::unordered_map<std::string, node_partition *>
 * @retval An unordered_map containing copy of the input
 */
static std::unordered_map<std::string, node_partition *>
dup_server_psets(std::unordered_map<std::string, node_partition *>& spsets, server_info *sinfo)
{
	std::unordered_map<std::string, node_partition *> newpset;

	for (const auto& spset : spsets) {
		newpset[spset.first] = dup_node_partition(spset.second, sinfo);
		if (newpset[spset.first] == NULL) {
			free_server_psets(newpset);
			return {};
		}
	}

	return newpset;
}

/**
 * @brief
 * 		free_server_info - free the space used by a server_info
 *		structure
 *
 * @param[in]	sinfo	-	the server_info structure to free
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
free_server_info(server_info *sinfo)
{
	if (sinfo->name != NULL)
		free(sinfo->name);
	if (sinfo->jobs != NULL)
		free(sinfo->jobs);
	if (sinfo->all_resresv != NULL)
		free(sinfo->all_resresv);
	if (sinfo->running_jobs != NULL)
		free(sinfo->running_jobs);
	if (sinfo->exiting_jobs != NULL)
		free(sinfo->exiting_jobs);
	/* if we don't have nodes associated with queues, this is a reference */
	if (sinfo->has_nodes_assoc_queue == 0)
		sinfo->unassoc_nodes = NULL;
	else if (sinfo->unassoc_nodes != NULL)
		free(sinfo->unassoc_nodes);
	if (sinfo->alljobcounts != NULL)
		free_counts_list(sinfo->alljobcounts);
	if (sinfo->group_counts != NULL)
		free_counts_list(sinfo->group_counts);
	if (sinfo->project_counts != NULL)
		free_counts_list(sinfo->project_counts);
	if (sinfo->user_counts != NULL)
		free_counts_list(sinfo->user_counts);
	if (sinfo->total_alljobcounts != NULL)
		free_counts_list(sinfo->total_alljobcounts);
	if (sinfo->total_group_counts != NULL)
		free_counts_list(sinfo->total_group_counts);
	if (sinfo->total_project_counts != NULL)
		free_counts_list(sinfo->total_project_counts);
	if (sinfo->total_user_counts != NULL)
		free_counts_list(sinfo->total_user_counts);
	if (sinfo->nodepart != NULL)
		free_node_partition_array(sinfo->nodepart);
	if (sinfo->allpart)
		free_node_partition(sinfo->allpart);
	if (!(sinfo->svr_to_psets.empty()))
		free_server_psets(sinfo->svr_to_psets);
	if (sinfo->hostsets != NULL)
		free_node_partition_array(sinfo->hostsets);
	if (sinfo->nodesigs)
		free_string_array(sinfo->nodesigs);
	if (sinfo->npc_arr != NULL)
		free_np_cache_array(sinfo->npc_arr);
	if (sinfo->node_group_key != NULL)
		free_string_array(sinfo->node_group_key);
	if (sinfo->calendar != NULL)
		free_event_list(sinfo->calendar);
	if (sinfo->policy != NULL)
		delete sinfo->policy;
	if (sinfo->fstree != NULL)
		delete sinfo->fstree;
	if (sinfo->liminfo != NULL) {
		lim_free_liminfo(sinfo->liminfo);
		sinfo->liminfo = NULL;
	}
	if (sinfo->queue_list != NULL)
		free_queue_list(sinfo->queue_list);
	if(sinfo->equiv_classes != NULL)
		free_resresv_set_array(sinfo->equiv_classes);
	if(sinfo->buckets != NULL)
		free_node_bucket_array(sinfo->buckets);

	if(sinfo->unordered_nodes != NULL)
		free(sinfo->unordered_nodes);

	free_resource_list(sinfo->res);
	free(sinfo->job_sort_formula);

#ifdef NAS
	/* localmod 034 */
	site_free_shares(sinfo);
#endif
}

/**
 * @brief
 *		free_resource_list - frees the memory used by a resource list
 *
 * @param[in]	reslist	-	the resource list to free
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
free_resource_list(schd_resource *reslist)
{
	schd_resource *resp, *tmp;

	if (reslist == NULL)
		return;

	resp = reslist;
	while (resp != NULL) {
		tmp = resp->next;
		free_resource(resp);

		resp = tmp;
	}
}

/**
 * @brief
 * 		free_resource - frees the memory used by a resource structure
 *
 * @param[in]	reslist	-	the resource to free
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
free_resource(schd_resource *resp)
{
	if (resp == NULL)
		return;

	if (resp->orig_str_avail != NULL)
		free(resp->orig_str_avail);

	if (resp->indirect_vnode_name != NULL)
		free(resp->indirect_vnode_name);

	if (resp->str_avail != NULL)
		free_string_array(resp->str_avail);

	if (resp->str_assigned != NULL)
		free(resp->str_assigned);

	free(resp);
}

/**
 * @brief
 * 		new_server_info - allocate and initialize a new
 *		server_info struct
 *
 * @param[in]	limallocflag	-	if nonzero, a liminfo structure will
 *									also be allocated
 *
 * @return	new allocated struct
 *
 * @see	lim_alloc_liminfo
 *
 * @par MT-Safe:	no
 */
server_info *
new_server_info(int limallocflag)
{
	server_info *sinfo;			/* the new server */

	if ((sinfo = new server_info()) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	sinfo->has_soft_limit = 0;
	sinfo->has_hard_limit = 0;
	sinfo->has_user_limit = 0;
	sinfo->has_grp_limit = 0;
	sinfo->has_proj_limit = 0;
	sinfo->has_all_limit = 0;
	sinfo->has_mult_express = 0;
	sinfo->has_multi_vnode = 0;
	sinfo->has_prime_queue = 0;
	sinfo->has_nonprime_queue = 0;
	sinfo->has_nodes_assoc_queue = 0;
	sinfo->has_ded_queue = 0;
	sinfo->has_runjob_hook = 0;
	sinfo->node_group_enable = 0;
	sinfo->eligible_time_enable = 0;
	sinfo->provision_enable = 0;
	sinfo->power_provisioning = 0;
	sinfo->use_hard_duration = 0;
	sinfo->pset_metadata_stale = 0;
	sinfo->num_parts = 0;
	sinfo->name = NULL;
	sinfo->res = NULL;
	sinfo->queues = NULL;
	sinfo->queue_list = NULL;
	sinfo->jobs = NULL;
	sinfo->all_resresv = NULL;
	sinfo->calendar = NULL;
	sinfo->running_jobs = NULL;
	sinfo->exiting_jobs = NULL;
	sinfo->nodes = NULL;
	sinfo->unassoc_nodes = NULL;
	sinfo->resvs = NULL;
	sinfo->alljobcounts = NULL;
	sinfo->group_counts = NULL;
	sinfo->project_counts = NULL;
	sinfo->user_counts = NULL;
	sinfo->total_alljobcounts = NULL;
	sinfo->total_group_counts = NULL;
	sinfo->total_project_counts = NULL;
	sinfo->total_user_counts = NULL;
	sinfo->nodepart = NULL;
	sinfo->allpart = NULL;
	sinfo->hostsets = NULL;
	sinfo->nodesigs = NULL;
	sinfo->node_group_key = NULL;
	sinfo->npc_arr = NULL;
	sinfo->qrun_job = NULL;
	sinfo->policy = NULL;
	sinfo->fstree = NULL;
	sinfo->equiv_classes = NULL;
	sinfo->buckets = NULL;
	sinfo->unordered_nodes = NULL;
	sinfo->num_queues = 0;
	sinfo->num_nodes = 0;
	sinfo->num_resvs = 0;
	sinfo->num_hostsets = 0;
	sinfo->server_time = 0;
	sinfo->job_sort_formula = NULL;

	if ((limallocflag != 0))
		sinfo->liminfo = lim_alloc_liminfo();
	init_state_count(&(sinfo->sc));
	memset(sinfo->preempt_count, 0, (NUM_PPRIO + 1) * sizeof(int));

#ifdef NAS
	/* localmod 034 */
	sinfo->share_head = NULL;
#endif

	return sinfo;
}

/**
 * @brief
 * 		new_resource - allocate and initialize new resource struct
 *
 * @return	schd_resource
 * @retval	NULL	: Error
 *
 * @par MT-Safe:	yes
 */
schd_resource *
new_resource()
{
	schd_resource *resp;		/* the new resource */

	if ((resp = static_cast<schd_resource *>(calloc(1,  sizeof(schd_resource)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	/* member type zero'd by calloc() */

	resp->name = NULL;
	resp->next = NULL;
	resp->def = NULL;
	resp->orig_str_avail = NULL;
	resp->indirect_vnode_name = NULL;
	resp->indirect_res = NULL;
	resp->str_avail = NULL;
	resp->str_assigned = NULL;
	resp->assigned = RES_DEFAULT_ASSN;
	resp->avail = RES_DEFAULT_AVAIL;

	return resp;
}

/**
 * @brief
 * 		Create new resource with given data
 *
 * @param[in]	name	-	name of resource
 * @param[in] 	value	-	value of resource
 * @param[in] 	field	-	is the value RF_AVAIL or RF_ASSN
 *
 * @see	set_resource()
 *
 * @return schd_resource *
 * @retval newly created resource
 * @retval NULL	: on error
 */
schd_resource *
create_resource(const char *name, const char *value, enum resource_fields field)
{
	schd_resource *nres = NULL;
	resdef *rdef;

	if (name == NULL)
		return NULL;

	if(value == NULL && field != RF_NONE)
		return NULL;

	rdef = find_resdef(name);

	if (rdef != NULL) {
		if ((nres = new_resource()) != NULL) {
			nres->def = rdef;
			nres->name = rdef->name.c_str();
			nres->type = rdef->type;

			if (value != NULL) {
				if (set_resource(nres, value, field) == 0) {
					free_resource(nres);
					return NULL;
				}
			}
		}
	} else {
		log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_SCHED, LOG_DEBUG, name,
			"Resource definition does not exist, resource may be invalid");
		return NULL;
	}

	return nres;
}

/**
 * @brief modify the resources_assigned values for a resource_list
 * 		(e.g. either A += B or A -= B) where
 * 		A is a resource list and B is a resource_req list.
 *
 * @param[in] res_list - The schd_resource list which is modified
 * @param[in] req_list - What is modifing the schd_resource list
 * @param[in] type - SCHD_INCR for += or SCHD_DECR for -=
 *
 * @return int
 * @retval 1 - success
 * @retval 0 - failure
 */
int
modify_resource_list(schd_resource *res_list, resource_req *req_list, int type)
{
	schd_resource *cur_res;
	resource_req *cur_req;
	schd_resource *end_res = NULL;

	if (res_list == NULL || req_list == NULL)
		return 0;

	for(cur_req = req_list; cur_req != NULL; cur_req = cur_req->next) {
		if (cur_req->type.is_consumable) {
			cur_res = find_resource(res_list, cur_req->def);
			if (cur_res == NULL && type == SCHD_INCR) {
				if (end_res == NULL)
					for (end_res = res_list; end_res->next != NULL; end_res = end_res->next)
						;
				end_res->next = create_resource(cur_req->name, cur_req->res_str, RF_AVAIL);
				if (end_res->next == NULL)
					return 0;
				end_res = end_res->next;
			} else {
				if (type == SCHD_INCR)
					cur_res->assigned += cur_req->amount;
				else if (type == SCHD_DECR)
					    cur_res->assigned -= cur_req->amount;
			}
		}
	}
	return 1;
}

/**
 * @brief
 * 		add_resource_list - add one resource list to another
 *		i.e. r1 += r2
 *
 * @param[in]	policy	-	policy info
 * @param[in]	r1 		- 	lval resource
 * @param[in]	r2 		- 	rval resource
 * @param[in]	flags 	-
 *							NO_UPDATE_NON_CONSUMABLE - do not update
 *							non consumable resources
 *							USE_RESOURCE_LIST - use policy->resdef_to_check
 *							(and all bools) instead of all resources
 *							ADD_UNSET_BOOLS_FALSE - add unset bools as false
 *
 * @return	int
 * @retval	1	: success
 * @retval	0	: failure
 *
 * @par MT-Safe:	no
 */
int
add_resource_list(status *policy, schd_resource *r1, schd_resource *r2, unsigned int flags)
{
	schd_resource *cur_r1;
	schd_resource *cur_r2;
	schd_resource *end_r1 = NULL;
	schd_resource *nres;
	sch_resource_t assn;

	if (r1 == NULL || r2 == NULL)
		return 0;

	for (cur_r2 = r2; cur_r2 != NULL; cur_r2 = cur_r2->next) {
		if ((flags & NO_UPDATE_NON_CONSUMABLE) && cur_r2->def->type.is_non_consumable)
			continue;
		if ((flags & USE_RESOURCE_LIST)) {
			const auto& rtc = policy->resdef_to_check;
			if (rtc.find(cur_r2->def) == rtc.end() && !cur_r2->type.is_boolean)
				continue;
		}

		cur_r1 = find_resource(r1, cur_r2->def);
		if (cur_r1 == NULL) { /* resource in r2 which is not in r1 */
			if (!(flags & NO_UPDATE_NON_CONSUMABLE) || cur_r2->type.is_consumable) {
				if(end_r1 == NULL)
					for (end_r1 = r1; end_r1->next != NULL; end_r1 = end_r1->next)
						;
				end_r1->next = dup_resource(cur_r2);
				if (end_r1->next == NULL)
					return 0;
				end_r1 = end_r1->next;
			}
		} else if (cur_r1->type.is_consumable) {
			if ((flags & ADD_AVAIL_ASSIGNED)) {
				if (cur_r2->avail == RES_DEFAULT_AVAIL)
					assn = RES_DEFAULT_ASSN; /* nothing is set, so add nothing */
				else
					assn = cur_r2->avail;
			} else
				assn = cur_r2->assigned;
			add_resource_value(&(cur_r1->avail), &(cur_r2->avail),
					   RES_DEFAULT_AVAIL);
			add_resource_value(&(cur_r1->assigned),
					   &assn, RES_DEFAULT_ASSN);
		} else {
			if (!(flags & NO_UPDATE_NON_CONSUMABLE)) {
				if (cur_r1->type.is_string) {
					if (cur_r1->def == allres["vnode"])
						add_resource_str_arr(cur_r1, cur_r2->str_avail, 1);
					else
						add_resource_str_arr(cur_r1, cur_r2->str_avail, 0);
				} else if (cur_r1->type.is_boolean)
					(void)add_resource_bool(cur_r1, cur_r2);
			}
		}
	}

	if (flags & ADD_UNSET_BOOLS_FALSE) {
		for (const auto &br : boolres) {
			if (find_resource(r2, br) == NULL) {
				cur_r1 = find_resource(r1, br);
				if (cur_r1 == NULL) {
					nres = create_resource(br->name.c_str(), ATR_FALSE, RF_AVAIL);
					if (nres == NULL)
						return 0;

					if (end_r1 == NULL)
						for (end_r1 = r1; end_r1->next != NULL; end_r1 = end_r1->next)
							;
					end_r1->next = nres;
					end_r1 = nres;
				} else {
					nres = false_res();
					if (nres == NULL)
						return 0;
					nres->name = br->name.c_str();
					(void) add_resource_bool(cur_r1, nres);
				}
			}
		}
	}
	return 1;
}

/**
 * @brief
 *		add_resource_value - add a resource value to another
 *				i.e. val1 += val2
 *
 * @param[in]	val1		-	value 1
 * @param[in]	val2		-	value 2
 * @param[in]	initial_val - 	value set by resource constructor
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
add_resource_value(sch_resource_t *val1, sch_resource_t *val2,
	sch_resource_t initial_val)
{
	if (val1 == NULL || val2 == NULL)
		return;

	if (*val1 == initial_val)
		*val1 = *val2;
	else if (*val2 != initial_val)
		*val1 += *val2;
	/* else val2 is default and val1 isn't, so we leave val1 alone */
}

/**
 * @brief
 * 		Add values from a string array to a string resource (available).
 *		Only add values if they do not exist.
 *
 * @param[in]	res			-	resource to add values to
 * @param[in]	str_arr		-	string array of values to add
 * @param[in]	allow_dup 	- 	should we allow dups or not?
 *
 * @return	int
 *
 * @retval	1	: success
 * @retval	0	: failure
 *
 * @par MT-Safe:	no
 */
int
add_resource_str_arr(schd_resource *res, char **str_arr, int allow_dup)
{
	int i;

	if (res == NULL || str_arr == NULL)
		return 0;

	if (!res->type.is_string)
		return 0;

	for (i = 0; str_arr[i] != NULL; i++) {
		if (add_str_to_unique_array(&(res->str_avail), str_arr[i]) < 0)
			return 0;
	}
	return 1;
}

/**
 * @brief
 * 		accumulate two boolean resources together
 *		T + T = True | F + F = False | T + F = TRUE_FALSE
 *
 * @param[in] 	r1	-	lval : left side boolean to add to
 * @param[in]	r2 	-	rval : right side boolean - if NULL, treat as false
 *
 * @return int
 * @retval	1	: Ok
 * @retval	0	: Error
 */
int
add_resource_bool(schd_resource *r1, schd_resource *r2)
{
	int r1val, r2val;
	if (r1 == NULL)
		return 0;

	if (!r1->type.is_boolean || (r2 != NULL && !r2->type.is_boolean))
		return 0;

	/* We can't accumulate any more values than TRUE and FALSE,
	 * so if we have both than return success early
	 */
	r1val = r1->avail;
	if (r1val == TRUE_FALSE)
		return 1;

	r2val = r2 == NULL ? FALSE : r2->avail;

	/********************************************
	 *        Possible Value Combinations       *
	 *       r1     *    r2   *    r1 result    *
	 * ******************************************
	 *       T      *     T    *       T        *
	 *       T      *     F    *   TRUE_FALSE   *
	 *       F      *     T    *   TRUE_FALSE   *
	 *       F      *     F    *       F        *
	 ********************************************/

	if (r1val && !r2val)
		r1->avail = TRUE_FALSE;
	else if (!r1val && r2val)
		r1->avail = TRUE_FALSE;

	return 1;
}

/**
 * @brief
 * 		free_server - free a server_info and possibly its queues also
 *
 * @param[in]	sinfo -	server_info list head
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
free_server(server_info *sinfo)
{
	if (sinfo == NULL)
		return;
	/* We need to free the sinfo first to free the calendar.
	 * When the calendar is freed, the job events modify the jobs.  We can't
	 * free the jobs before then.
	 */
	free_server_info(sinfo);

	free_queues(sinfo->queues);
	free_nodes(sinfo->nodes);
	free_resource_resv_array(sinfo->resvs);

#ifdef NAS /* localmod 053 */
	site_restore_users();
#endif /* localmod 053 */
	delete sinfo;
}

/**
 * @brief
 * 		update_server_on_run - update server_info structure
 *				when a resource resv is run
 *
 * @policy[in]	policy 	- 	policy info
 * @param[in]	sinfo 	- 	server_info to update
 * @param[in]	qinfo 	- 	queue_info the job is in
 *							(if resresv is a job)
 * @param[in]	resresv - 	resource_resv that was run
 * @param[in]  job_state -	the old state of a job if resresv is a job
 *				If the old_state is found to be suspended
 *				then only resources that were released
 *				during suspension will be accounted.
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
update_server_on_run(status *policy, server_info *sinfo,
	queue_info *qinfo, resource_resv *resresv, char * job_state)
{
	if (sinfo == NULL || resresv == NULL)
		return;

	if (resresv->is_job) {
		if (resresv->job == NULL)
			return;
		if (qinfo == NULL)
			return;
	}


	/*
	 * Update the server level resources
	 *   -- if a job is in a reservation, the resources have already been
	 *      accounted for and assigned to the reservation.  We don't want to
	 *      double count them
	 */
	if (resresv->is_resv || (qinfo != NULL && qinfo->resv == NULL)) {
		resource_req *req; /* used to cycle through resources to update */

		if (resresv->is_job && (job_state != NULL) && (*job_state == 'S') && (resresv->job->resreq_rel != NULL))
			req = resresv->job->resreq_rel;
		else
			req = resresv->resreq;
		while (req != NULL) {
			if (req->type.is_consumable) {
				auto res = find_resource(sinfo->res, req->def);

				if (res)
					res->assigned += req->amount;
			}
			req = req->next;
		}
	}

	if (resresv->is_job) {
		sinfo->sc.running++;
		/* note: if job is suspended, counts will get off.
		 *       sc.queued is not used, and sc.suspended isn't used again
		 *       after this point
		 *       BZ 5798
		 */
		sinfo->sc.queued--;

		/* sort the nodes before we filter them down to more useful lists */
		if (!cstat.node_sort->empty() && conf.node_sort_unused) {
			if (resresv->job->resv != NULL &&
				resresv->job->resv->resv != NULL) {
				node_info **resv_nodes;
				int num_resv_nodes;

				resv_nodes = resresv->job->resv->resv->resv_nodes;
				num_resv_nodes = count_array(resv_nodes);
				qsort(resv_nodes, num_resv_nodes, sizeof(node_info *),
					multi_node_sort);
			} else {
				qsort(sinfo->nodes, sinfo->num_nodes, sizeof(node_info *),
					multi_node_sort);

				if (sinfo->nodes != sinfo->unassoc_nodes) {
					auto num_unassoc = count_array(sinfo->unassoc_nodes);
					qsort(sinfo->unassoc_nodes, num_unassoc, sizeof(node_info *),
						multi_node_sort);
				}
			}
		}

		/* We're running a job or reservation, which will affect the cached data.
		 * We'll flush the cache and rebuild it if needed
		 */
		if (sinfo->npc_arr != NULL) {
			free_np_cache_array(sinfo->npc_arr);
			sinfo->npc_arr = NULL;
		}


		/* a new job has been run, update running jobs array */
		sinfo->running_jobs = add_resresv_to_array(sinfo->running_jobs, resresv, NO_FLAGS);
	}

	if (sinfo->has_soft_limit || sinfo->has_hard_limit) {
		if (resresv->is_job) {
			counts *cts; /* used in updating project/group/user counts */

			update_total_counts(sinfo, NULL , resresv, SERVER);

			cts = find_alloc_counts(sinfo->group_counts, resresv->group);

			if (sinfo->group_counts == NULL)
				sinfo->group_counts = cts;

			update_counts_on_run(cts, resresv->resreq);

			cts = find_alloc_counts(sinfo->project_counts, resresv->project);

			if (sinfo->project_counts == NULL)
				sinfo->project_counts = cts;

			update_counts_on_run(cts, resresv->resreq);

			cts = find_alloc_counts(sinfo->user_counts, resresv->user);

			if (sinfo->user_counts == NULL)
				sinfo->user_counts = cts;

			update_counts_on_run(cts, resresv->resreq);

			auto allcts = find_alloc_counts(sinfo->alljobcounts, PBS_ALL_ENTITY);

			if (sinfo->alljobcounts == NULL)
				sinfo->alljobcounts = allcts;

			update_counts_on_run(allcts, resresv->resreq);
		}
	}
}

/**
 * @brief
 * 		update_server_on_end - update a server_info structure when a
 *		resource resv has finished running
 *
 * @param[in]   policy 	- policy info
 * @param[in]	sinfo	- server_info to update
 * @param[in]	qinfo 	- queue_info the job is in
 * @param[in]	resresv - resource_resv that finished running
 * @param[in]  job_state -	the old state of a job if resresv is a job
 *				If the old_state is found to be suspended
 *				then only resources that were released
 *				during suspension will be accounted.
 *
 * @return	void
 *
 * @note
 * 		Job must be in pre-ended state (job_state is new state)
 *
 * @par MT-Safe:	no
 */
void
update_server_on_end(status *policy, server_info *sinfo, queue_info *qinfo,
	resource_resv *resresv, const char *job_state)
{
	if (sinfo == NULL ||  resresv == NULL)
		return;
	if (resresv->is_job) {
		if (resresv->job == NULL)
			return;
		if (qinfo == NULL)
			return;
	}

	if (resresv->is_job) {
		if (resresv->job->is_running) {
			sinfo->sc.running--;
			remove_resresv_from_array(sinfo->running_jobs, resresv);
		} else if (resresv->job->is_exiting) {
			sinfo->sc.exiting--;
			remove_resresv_from_array(sinfo->exiting_jobs, resresv);
		}
		state_count_add(&(sinfo->sc), job_state, 1);
	}

	/*
	 *	if the queue is a reservation then the resources belong to it and not
	 *	the server
	 */
	if (resresv->is_resv || (qinfo != NULL && qinfo->resv == NULL)) {
		resource_req *req; /* resource request from job */

		if (resresv->is_job && (job_state != NULL) && (*job_state == 'S') && (resresv->job->resreq_rel != NULL))
			req = resresv->job->resreq_rel;
		else
			req = resresv->resreq;

		while (req != NULL) {
			auto res = find_resource(sinfo->res, req->def);

			if (res != NULL) {
				res->assigned -= req->amount;

				if (res->assigned < 0) {
					log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_SERVER, LOG_DEBUG, __func__,
						"%s turned negative %.2lf, setting it to 0", res->name, res->assigned);
					res->assigned = 0;
				}
			}

			req = req->next;
		}
	}

	/* We're ending a job or reservation, which will affect the cached data.
	 * We'll flush the cache and rebuild it if needed
	 */
	if (sinfo->npc_arr != NULL) {
		free_np_cache_array(sinfo->npc_arr);
		sinfo->npc_arr = NULL;
	}

	if (sinfo->has_soft_limit || sinfo->has_hard_limit) {
		if (resresv->is_job && resresv->job->is_running) {
			counts *cts;			/* update user/group/project counts */

			update_total_counts_on_end(sinfo, NULL, resresv, SERVER);
			cts = find_counts(sinfo->group_counts, resresv->group);

			if (cts != NULL)
				update_counts_on_end(cts, resresv->resreq);

			cts = find_counts(sinfo->project_counts, resresv->project);

			if (cts != NULL)
				update_counts_on_end(cts, resresv->resreq);

			cts = find_counts(sinfo->user_counts, resresv->user);

			if (cts != NULL)
				update_counts_on_end(cts, resresv->resreq);

			cts = find_alloc_counts(sinfo->alljobcounts, PBS_ALL_ENTITY);

			if (cts != NULL)
				update_counts_on_end(cts, resresv->resreq);

		}
	}
}
/**
 * @brief
 * 		copy_server_arrays - copy server arrays of all jobs and all reservations
 *
 * @param[in]	nsinfo	-	the server to copy lists to
 * @param[in]	osinfo	-	the server to copy lists from
 *
 * @return	int
 * @retval	1	: success
 * @retval	0 	: failure
 *
 * @par MT-Safe:	no
 */
int
copy_server_arrays(server_info *nsinfo, server_info *osinfo)
{
	resource_resv **job_arr;	/* used in copying jobs to job array */
	resource_resv **all_arr;	/* used in copying jobs to job/resv array */
	resource_resv **resresv_arr;	/* used as source array to copy */
	int i = 0;
	int j = 0;
	int index;

	if (nsinfo == NULL || osinfo == NULL)
		return 0;

	if ((job_arr = static_cast<resource_resv **>(calloc((osinfo->sc.total + 1), sizeof(resource_resv *)))) == NULL) {
		log_err(errno, __func__, "Error allocating memory");
		return 0;
	}

	if ((all_arr = static_cast<resource_resv **>(calloc((osinfo->sc.total + osinfo->num_resvs + 1),
						 sizeof(resource_resv *)))) == NULL) {
		free(job_arr);
		log_err(errno, __func__, "Error allocating memory");
		return 0;
	}

	for (index = 0; nsinfo->queues[index] != NULL; index++) {
		resresv_arr = nsinfo->queues[index]->jobs;

		if (resresv_arr != NULL) {
			for (i = 0; resresv_arr[i] != NULL; i++, j++)
				job_arr[j] = all_arr[resresv_arr[i]->resresv_ind] = resresv_arr[i];
		}
	}

	if (nsinfo->resvs != NULL) {
		for (i = 0; nsinfo->resvs[i] != NULL; i++)
			all_arr[nsinfo->resvs[i]->resresv_ind] = nsinfo->resvs[i];
	}
	nsinfo->jobs = job_arr;
	nsinfo->all_resresv = all_arr;
	nsinfo->num_resvs = osinfo->num_resvs;
	return 1;
}

/**
 * @brief
 * 		create_server_arrays - create a large server resresv array
 *		of all jobs on the system by coping all the jobs from the
 *		queue job arrays.  Also create an array of both jobs and
 *		reservations.
 *
 * @param[in]	sinfo	-	the server
 *
 * @return	int
 * @retval	1	: success
 * @retval	0 	: failure
 *
 * @par MT-Safe:	no
 */
int
create_server_arrays(server_info *sinfo)
{
	queue_info **qinfo;		/* used to cycle through the array of queues */
	resource_resv **job_arr;	/* used in copying jobs to job array */
	resource_resv **all_arr;	/* used in copying jobs to job/resv array */
	resource_resv **resresv_arr;	/* used as source array to copy */
	int i = 0, j;

	if ((job_arr = static_cast<resource_resv **>(malloc(sizeof(resource_resv *) * (sinfo->sc.total + 1)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return 0;
	}

	if ((all_arr = static_cast<resource_resv **>(malloc(sizeof(resource_resv *) *
		(sinfo->sc.total + sinfo->num_resvs + 1)))) == NULL) {
		free(job_arr);
		log_err(errno, __func__, MEM_ERR_MSG);
		return 0;
	}

	qinfo = sinfo->queues;
	while (*qinfo != NULL) {
		resresv_arr = (*qinfo)->jobs;

		if (resresv_arr != NULL) {
			for (j = 0; resresv_arr[j] != NULL; j++, i++) {
				job_arr[i] = all_arr[i] = resresv_arr[j];
				all_arr[i]->resresv_ind = i;
			}
			if (i > sinfo->sc.total) {
				free(job_arr);
				free(all_arr);
				return 0;
			}
		}
		qinfo++;
	}
	job_arr[i] = NULL;

#ifdef NAS /* localmod 054 */
	if (i != sinfo->sc.total) {
		sprintf(log_buffer, "Expected %d jobs, but found %d", sinfo->sc.total, i);
		log_err(-1, __func__, log_buffer);
		sinfo->sc.total = i;
	}
#endif /* localmod 054 */

	if (sinfo->resvs != NULL) {
		for (j = 0; sinfo->resvs[j] != NULL; j++, i++) {
			all_arr[i] = sinfo->resvs[j];
			all_arr[i]->resresv_ind = i;
		}
#ifdef NAS /* localmod 054 */
		if (j != sinfo->num_resvs) {
			sprintf(log_buffer, "Expected %d resv, but found %d", sinfo->num_resvs, j);
			log_err(-1, __func__, log_buffer);
			if (j > sinfo->num_resvs) {
				abort();
			}
			sinfo->num_resvs = j;
		}
#endif /* localmod 054 */
	}
	all_arr[i] = NULL;

	sinfo->jobs = job_arr;
	sinfo->all_resresv = all_arr;

	return 1;
}

/**
 * @brief
 * 		helper function for resource_resv_filter() - returns 1 if
 *		job is running
 *
 * @param[in]	job	-	resource reservation job.
 * @param[in]	arg	-	argument (not used here)
 *
 * @return	int
 * @retval	0	: job not running
 * @retval	1	: job is running
 */
int
check_run_job(resource_resv *job, const void *arg)
{
	if (job->is_job && job->job != NULL)
		return job->job->is_running;

	return 0;
}

/**
 * @brief
 * 		helper function for resource_resv_filter()
 *
 * @param[in]	job	-	resource reservation job.
 * @param[in]	arg	-	argument (not used here)
 *
 * @return	int
 * @retval	1	: if job is exiting
 */
int
check_exit_job(resource_resv *job, const void *arg)
{
	if (job->is_job && job->job != NULL)
		return job->job->is_exiting;

	return 0;
}

/**
 * @brief
 * 		helper function for resource_resv_filter()
 *
 * @param[in]	job	-	resource reservation job.
 * @param[in]	arg	-	argument (not used here)
 *
 * @return	int
 * @retval	1	: if job is suspended
 * @retval	0	: if job is not suspended
 */
int
check_susp_job(resource_resv *job, const void *arg)
{
	if (job->is_job && job->job != NULL)
		return job->job->is_suspended;

	return 0;
}

/**
 * @brief
 * 		helper function for resource_resv_filter()
 *
 * @param[in]	job	-	resource reservation job.
 * @param[in]	arg	-	argument (not used here)
 *
 * @return	int
 * @retval	1	: if job is running
 * @retval	0	: if job is not running
 */
int
check_job_running(resource_resv *job, const void *arg)
{
	if (job->is_job && (job->job->is_running || job->job->is_exiting || job->job->is_userbusy))
		return 1;

	return 0;
}

/**
 * @brief
 * 		helper function for resource_resv_filter()
 *
 * @param[in]	job	-	resource reservation job.
 * @param[in]	arg	-	argument (not used here)
 *
 * @return	int
 * @retval	1	: if job is running and in a reservation
 * @retval	0	: if job is not running or not in a reservation
 */
int
check_running_job_in_reservation(resource_resv *job, const void *arg)
{
	if (job->is_job && job->job != NULL && job->job->resv != NULL &&
		(check_job_running(job, arg) == 1))
		return 1;

	return 0;
}

/**
 * @brief
 * 		helper function for resource_resv_filter()
 *
 * @param[in]	job	-	resource reservation job.
 * @param[in]	arg	-	argument (not used here)
 *
 * @return	int
 * @retval	1	: if job is running and not in a reservation
 * @retval	0	: if job is not running or in a reservation
 */
int
check_running_job_not_in_reservation(resource_resv *job, const void *arg)
{
	if (job->is_job && job->job != NULL && job->job->resv == NULL &&
		(check_job_running(job, arg) == 1))
		return 1;

	return 0;
}

/**
 * @brief
 * 		helper function for resource_resv_filter()
 *
 * @param[in]	resv	-	resource reservation structure
 * @param[in]	arg		-	the array of nodes to look in
 *
 * @return	int
 * @retval	1	: if reservation is running on node passed in arg
 * @retval	0	: if reservation is not running on node passed in arg
 */
int
check_resv_running_on_node(resource_resv *resv, const void *arg)
{
	if (resv->is_resv && resv->resv != NULL) {
		if (resv->resv->is_running || resv->resv->resv_state == RESV_BEING_DELETED)
			if (find_node_info(resv->ninfo_arr, (char *) arg))
				return 1;
	}
	return 0;
}

/**
 * @brief
 * 		dup_server_info - duplicate a server_info struct
 *
 * @param[in]	osinfo	-	the struct to copy
 *
 * @return	duplicated server_info
 * @retval	NULL	: something wrong!
 *
 * @par MT-Safe:	no
 */
server_info *
dup_server_info(server_info *osinfo)
{
	server_info *nsinfo;		/* scheduler internal form of server info */
	int i;

	if (osinfo == NULL)
		return NULL;

	/* duplicate the server information */
	if ((nsinfo = new_server_info(0)) == NULL)
		return NULL;

	if (osinfo->fstree != NULL) {
		nsinfo->fstree = new fairshare_head(*osinfo->fstree);
		if (nsinfo->fstree == NULL) {
			free_server(nsinfo);
			return NULL;
		}
	}
	nsinfo->has_mult_express = osinfo->has_mult_express;
	nsinfo->has_soft_limit = osinfo->has_soft_limit;
	nsinfo->has_hard_limit = osinfo->has_hard_limit;
	nsinfo->has_user_limit = osinfo->has_user_limit;
	nsinfo->has_all_limit = osinfo->has_all_limit;
	nsinfo->has_grp_limit = osinfo->has_grp_limit;
	nsinfo->has_proj_limit = osinfo->has_proj_limit;
	nsinfo->has_multi_vnode = osinfo->has_multi_vnode;
	nsinfo->has_prime_queue = osinfo->has_prime_queue;
	nsinfo->has_nonprime_queue = osinfo->has_nonprime_queue;
	nsinfo->has_ded_queue = osinfo->has_ded_queue;
	nsinfo->has_nodes_assoc_queue = osinfo->has_nodes_assoc_queue;
	nsinfo->node_group_enable = osinfo->node_group_enable;
	nsinfo->eligible_time_enable = osinfo->eligible_time_enable;
	nsinfo->provision_enable = osinfo->provision_enable;
	nsinfo->power_provisioning = osinfo->power_provisioning;
	nsinfo->use_hard_duration = osinfo->use_hard_duration;
	nsinfo->pset_metadata_stale = osinfo->pset_metadata_stale;
	nsinfo->name = string_dup(osinfo->name);
	nsinfo->liminfo = lim_dup_liminfo(osinfo->liminfo);
	nsinfo->server_time = osinfo->server_time;
	nsinfo->res = dup_resource_list(osinfo->res);
	nsinfo->alljobcounts = dup_counts_list(osinfo->alljobcounts);
	nsinfo->group_counts = dup_counts_list(osinfo->group_counts);
	nsinfo->project_counts = dup_counts_list(osinfo->project_counts);
	nsinfo->user_counts = dup_counts_list(osinfo->user_counts);
	nsinfo->total_alljobcounts = dup_counts_list(osinfo->total_alljobcounts);
	nsinfo->total_group_counts = dup_counts_list(osinfo->total_group_counts);
	nsinfo->total_project_counts = dup_counts_list(osinfo->total_project_counts);
	nsinfo->total_user_counts = dup_counts_list(osinfo->total_user_counts);
	nsinfo->node_group_key = dup_string_arr(osinfo->node_group_key);
	nsinfo->nodesigs = dup_string_arr(osinfo->nodesigs);

	nsinfo->policy = dup_status(osinfo->policy);

	nsinfo->num_nodes = osinfo->num_nodes;

	/* dup the nodes, if there are any nodes */
	nsinfo->nodes = dup_nodes(osinfo->nodes, nsinfo, NO_FLAGS);

	if (nsinfo->has_nodes_assoc_queue) {
		nsinfo->unassoc_nodes =
			node_filter(nsinfo->nodes, nsinfo->num_nodes, is_unassoc_node, NULL, 0);
	} else
		nsinfo->unassoc_nodes = nsinfo->nodes;

	nsinfo->unordered_nodes = dup_unordered_nodes(osinfo->unordered_nodes, nsinfo->nodes);

	/* dup the reservations */
	nsinfo->resvs = dup_resource_resv_array(osinfo->resvs, nsinfo, NULL);
	nsinfo->num_resvs = osinfo->num_resvs;

#ifdef NAS /* localmod 053 */
	site_save_users();
#endif /* localmod 053 */

	/* duplicate the queues */
	nsinfo->num_queues = osinfo->num_queues;
	if ((nsinfo->queues = dup_queues(osinfo->queues, nsinfo)) == NULL) {
		free_server(nsinfo);
		return NULL;
	}

	if (osinfo->queue_list != NULL) {
		/* queues are already sorted in descending order of their priority */
		for (i = 0; i < nsinfo->num_queues; i++) {
			auto ret_val = add_queue_to_list(&nsinfo->queue_list, nsinfo->queues[i]);
			if (ret_val == 0) {
				nsinfo->fstree = NULL;
				free_server(nsinfo);
				return NULL;
			}
		}
	}

	nsinfo->sc = osinfo->sc;

	/* sets nsinfo -> jobs and nsinfo -> all_resresv */
#ifdef NAS /* localmod 054 */
	if (create_server_arrays(nsinfo) == 0) {
		free_server(nsinfo);
		return NULL;
	}
#else
	copy_server_arrays(nsinfo, osinfo);
#endif /* localmod 054 */

	nsinfo->equiv_classes = dup_resresv_set_array(osinfo->equiv_classes, nsinfo);

	/* the event list is created dynamically during the evaluation of resource
	 * reservations. It is a sorted list of all_resresv, initialized to NULL to
	 * appropriately be freed in free_event_list */
	nsinfo->calendar = dup_event_list(osinfo->calendar, nsinfo);
	if (nsinfo->calendar == NULL) {
		free_server(nsinfo);
		return NULL;
	}

	nsinfo->running_jobs =
		resource_resv_filter(nsinfo->jobs, nsinfo->sc.total,
		check_run_job, NULL, FILTER_FULL);

	nsinfo->exiting_jobs =
		resource_resv_filter(nsinfo->jobs, nsinfo->sc.total,
		check_exit_job, NULL, 0);

	nsinfo->num_preempted = osinfo->num_preempted;

	if (osinfo->qrun_job != NULL)
		nsinfo->qrun_job = find_resource_resv(nsinfo->jobs,
			osinfo->qrun_job->name);

	for (i = 0; i < NUM_PPRIO; i++)
		nsinfo->preempt_count[i] = osinfo->preempt_count[i];

#ifdef NAS /* localmod 034 */
	if (!site_dup_shares(osinfo, nsinfo)) {
		free_server(nsinfo);
		return NULL;
	}
#endif /* localmod 034 */

	/* Now we do any processing which has to happen last */

	/* the jobs are not dupped when we dup the nodes, so we need to copy
	 * the node's job arrays now
	 */
	for (i = 0; osinfo->nodes[i] != NULL; i++)
		nsinfo->nodes[i]->job_arr =
			copy_resresv_array(osinfo->nodes[i]->job_arr, nsinfo->jobs);

	nsinfo->num_parts = osinfo->num_parts;
	if (osinfo->nodepart != NULL) {
		nsinfo->nodepart = dup_node_partition_array(osinfo->nodepart, nsinfo);
		if (nsinfo->nodepart == NULL) {
			free_server(nsinfo);
			return NULL;
		}
	}
	nsinfo->allpart = dup_node_partition(osinfo->allpart, nsinfo);
	if (osinfo->hostsets != NULL) {
		int j, k;
		nsinfo->hostsets = dup_node_partition_array(osinfo->hostsets, nsinfo);
		if (nsinfo->hostsets == NULL) {
			free_server(nsinfo);
			return NULL;
		}
		/* reattach nodes to their host sets*/
		for (j = 0; nsinfo->hostsets[j] != NULL; j++) {
			node_partition *hset = nsinfo->hostsets[j];
			for (k = 0; hset->ninfo_arr[k] != NULL; k++)
				hset->ninfo_arr[k]->hostset = hset;
		}
		nsinfo->num_hostsets = osinfo->num_hostsets;
	}

	/* the running resvs are not dupped when we dup the nodes, so we need to copy
	 * the node's running resvs arrays now
	 */
	for (i = 0; osinfo->nodes[i] != NULL; i++) {
		nsinfo->nodes[i]->run_resvs_arr =
			copy_resresv_array(osinfo->nodes[i]->run_resvs_arr, nsinfo->resvs);
		nsinfo->nodes[i]->np_arr =
			copy_node_partition_ptr_array(osinfo->nodes[i]->np_arr, nsinfo->nodepart);
		if (nsinfo->calendar != NULL)
			nsinfo->nodes[i]->node_events = dup_te_lists(osinfo->nodes[i]->node_events, nsinfo->calendar->next_event);
	}
	nsinfo->buckets = dup_node_bucket_array(osinfo->buckets, nsinfo);
	/* Now that all job information has been created, time to associate
	 * jobs to each other if they have runone dependency
	 */
	associate_dependent_jobs(nsinfo);

	for (i = 0; nsinfo->running_jobs[i] != NULL; i++) {
		if ((nsinfo->running_jobs[i]->job->is_subjob) &&
		    (associate_array_parent(nsinfo->running_jobs[i], nsinfo) == 1)) {
			free_server_info(nsinfo);
			return NULL;
		}
	}

	if (osinfo->job_sort_formula != NULL) {
		nsinfo->job_sort_formula = string_dup(osinfo->job_sort_formula);
		if (nsinfo->job_sort_formula == NULL) {
			free_server_info(nsinfo);
			return NULL;
		}
	}

	/* Copy the map of server psets */
	nsinfo->svr_to_psets = dup_server_psets(osinfo->svr_to_psets, nsinfo);

	return nsinfo;
}

/**
 * @brief
 * 		dup_resource_list - dup a resource list
 *
 * @param[in]	res - the resource list to duplicate
 *
 * @return	duplicated resource list
 * @retval	NULL	: Error
 *
 * @par MT-Safe:	no
 */
schd_resource *
dup_resource_list(schd_resource *res)
{
	schd_resource *pres;
	schd_resource *nres;
	schd_resource *prev = NULL;
	schd_resource *head = NULL;

	for (pres = res; pres != NULL; pres = pres->next) {
		nres = dup_resource(pres);
		if (prev == NULL)
			head = nres;
		else
			prev->next = nres;

		prev = nres;
	}

	return head;
}
/**
 * @brief
 * 		dup a resource list selectively + booleans (set or unset=false)
 *
 *
 *	@param[in]	res - the resource list to duplicate
 *	@param[in]	deflist -  dup resources in this list
 *	@param[in]	flags - @see add_resource_list()
 *
 * @return	duplicated resource list
 * @retval	NULL	: Error
 *
 * @par MT-Safe:	no
 */
schd_resource *
dup_selective_resource_list(schd_resource *res, std::unordered_set<resdef *>& deflist, unsigned flags)
{
	schd_resource *pres;
	schd_resource *nres;
	schd_resource *prev = NULL;
	schd_resource *head = NULL;

	for (pres = res; pres != NULL; pres = pres->next) {
		if (((flags & ADD_ALL_BOOL) && pres->type.is_boolean) ||
			deflist.find(pres->def) != deflist.end()) {
			nres = dup_resource(pres);
			if (nres == NULL) {
				free_resource_list(head);
				return NULL;
			}
			if ((flags & ADD_AVAIL_ASSIGNED)) {
				if (nres->avail == RES_DEFAULT_AVAIL)
					nres->assigned = RES_DEFAULT_ASSN;
				else
					nres->assigned = nres->avail;
			}
			if (prev == NULL)
				head = nres;
			else
				prev->next = nres;
			prev = nres;
		}
	}
	/* add on any booleans which are unset (i.e.,  false) */
	if (flags & ADD_UNSET_BOOLS_FALSE) {
		for (const auto& br : boolres) {
			if (find_resource(res, br) == NULL) {
				nres = create_resource(br->name.c_str(), ATR_FALSE, RF_AVAIL);
				if (nres == NULL) {
					free_resource_list(head);
					return NULL;
				}
				if (prev == NULL)
					head = nres;
				else
					prev->next = nres;
				prev = nres;
			}
		}
	}
	return head;
}

/**
 * @brief
 * 		dup_ind_resource_list - dup a resource list - if a resource in
 *		the list is indirect, dup the pointed to resource instead
 *
 * @param[in]	res - the resource list to duplicate
 *
 * @return	duplicated resource list
 * @retval	NULL	: Error
 *
 * @par MT-Safe:	no
 */
schd_resource *
dup_ind_resource_list(schd_resource *res)
{
	schd_resource *pres;
	schd_resource *nres;
	schd_resource *prev = NULL;
	schd_resource *head = NULL;

	for (pres = res; pres != NULL; pres = pres->next) {
		if (pres->indirect_res != NULL)
			nres = dup_resource(pres->indirect_res);
		else
			nres = dup_resource(pres);

		if (nres == NULL) {
			free_resource_list(head);
			return NULL;
		}

		if (prev == NULL)
			head = nres;
		else
			prev->next = nres;

		prev = nres;
	}

	return head;
}

/**
 * @brief
 * 		dup_resource - duplicate a resource struct
 *
 * @param[in]	res	- the resource to dup
 *
 * @return	the duplicated resource
 * @retval	NULL	: Error
 *
 * @par MT-Safe:	no
 */
schd_resource *
dup_resource(schd_resource *res)
{
	schd_resource *nres;

	if ((nres = new_resource()) == NULL)
		return NULL;

	nres->def = res->def;
	if (nres->def != NULL)
		nres->name = nres->def->name.c_str();


	if (res->indirect_vnode_name != NULL)
		nres->indirect_vnode_name = string_dup(res->indirect_vnode_name);

	if (res->orig_str_avail != NULL)
		nres->orig_str_avail = string_dup(res->orig_str_avail);

	if (res->str_avail != NULL)
		nres->str_avail = dup_string_arr(res->str_avail);

	if (res->str_assigned != NULL)
		nres->str_assigned = string_dup(res->str_assigned);

	nres->avail = res->avail;
	nres->assigned = res->assigned;

	memcpy(&(nres->type), &(res->type), sizeof(struct resource_type));

	return nres;
}

/**
 * @brief
 * 		is_unassoc_node - finds nodes which are not associated
 *		with queues used with node_filter
 *
 * @param[in]	ninfo - the node to check
 *
 * @return	int
 * @retval	1	-	if the node does not have a queue associated with it
 * @retval	0	- 	otherwise
 *
 * @par MT-Safe:	yes
 */
int
is_unassoc_node(node_info *ninfo, void *arg)
{
	if (ninfo->queue_name.empty())
		return 1;

	return 0;
}

/**
 * @brief
 * 		new_counts - create a new counts structure and return it
 *
 * @return	new counts structure
 * @retval	NULL	: malloc failed
 *
 * @par MT-Safe:	yes
 */
counts *
new_counts(void)
{

	counts *cts;

	if ((cts = static_cast<struct counts *>(malloc(sizeof(struct counts)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	cts->name = NULL;
	cts->running = 0;
	cts->rescts = NULL;
	cts->soft_limit_preempt_bit = 0;
	cts->next = NULL;

	return cts;
}

/**
 * @brief
 * 		free_counts - free a counts structure
 *
 * @param[in]	cts	-	the counts structure to free
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
free_counts(counts *cts)
{
	if (cts == NULL)
		return;

	if (cts->name != NULL)
		free(cts->name);

	if (cts->rescts != NULL)
		free_resource_count_list(cts->rescts);

	cts->next = NULL;

	free(cts);
}

/**
 * @brief
 * 		free_counts_list - free a list of counts structures
 *
 * @param[in]	ctslist	- the counts structure to free
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
free_counts_list(counts *ctslist)
{
	counts *prev, *cur;

	if (ctslist == NULL)
		return;

	cur = prev = ctslist;

	while (cur != NULL) {
		prev = cur->next;
		free_counts(cur);
		cur = prev;
	}
}

/**
 * @brief
 * 		dup_counts - duplicate a counts structure
 *
 * @param[in]	ctslist	- the counts structure to duplicate
 *
 * @return	new counts structure
 * @retval	NULL	: on error
 *
 * @par MT-Safe:	no
 */
counts *
dup_counts(counts *octs)
{
	counts *ncts;

	ncts = new_counts();

	if (ncts != NULL) {
		if (octs->name != NULL)
			ncts->name = string_dup(octs->name);

		ncts->running = octs->running;
		ncts->soft_limit_preempt_bit = octs->soft_limit_preempt_bit;

		ncts->rescts = dup_resource_count_list(octs->rescts);
	}

	return ncts;
}

/**
 * @brief
 * 		dup_counts_list - duplicate a counts list
 *
 * @param[in]	octs - the counts structure to duplicate
 *
 * @return	duplicated counts list
 * @retval	NULL	: error
 *
 * @par MT-Safe:	no
 */
counts *
dup_counts_list(counts *ctslist)
{
	counts *cur;
	counts *nhead;
	counts *prev;
	counts *ncts;

	nhead = NULL;
	prev = NULL;
	cur = ctslist;

	while (cur != NULL) {
		if ((ncts = dup_counts(cur)) != NULL) {
			if (nhead == NULL)
				nhead = ncts;
			else
				prev->next = ncts;

			prev = ncts;
		}
		cur = cur->next;
	}

	return nhead;
}

/**
 * @brief
 * 		find_counts - find a counts structure by name
 *
 * @param[in]	ctslist - the counts list to search
 * @param[in]	name 	- the name to find
 *
 * @return	found counts structure
 * @retval	NULL	: error
 *
 * @par MT-Safe:	no
 */
counts *
find_counts(counts *ctslist, const char *name)
{
	counts *cur;

	if (ctslist == NULL || name == NULL)
		return NULL;

	cur = ctslist;

	while (cur != NULL && strcmp(cur->name, name))
		cur = cur->next;

	return cur;
}

/**
 * @brief
 * 		find_alloc_counts - find a counts structure by name or allocate
 *		 a new counts, name it, and add it to the end of the list
 *
 * @param[in]	ctslist - the counts list to search
 * @param[in]	name 	- the name to find
 *
 * @return	found or newly-allocated counts structure
 * @retval	NULL	: error
 *
 * @par MT-Safe:	no
 */
counts *
find_alloc_counts(counts *ctslist, const char *name)
{
	counts *cur, *prev;
	counts *ncounts;

	if (name == NULL)
		return NULL;

	prev = cur = ctslist;

	while (cur != NULL && strcmp(cur->name, name)) {
		prev = cur;
		cur = cur->next;
	}

	if (cur == NULL) {
		ncounts = new_counts();

		if (ncounts != NULL)
			ncounts->name = string_dup(name);

		if (prev != NULL)
			prev->next = ncounts;

		return ncounts;
	} else
		return cur;
}

/**
 * @brief
 * 		update_counts_on_run - update a counts struct on the running of
 *		a job
 *
 * @param[in]	cts 	- the counts structure to update
 * @param[in]	resreq 	- the resource requirements of the job which ran
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
update_counts_on_run(counts *cts, resource_req *resreq)
{
	resource_count *ctsreq;			/* rescts to update */
	resource_req *req;			/* current in resreq */

	if (cts == NULL)
		return;

	cts->running++;

	if (resreq == NULL)
		return;

	req = resreq;

	while (req != NULL) {
		ctsreq = find_alloc_resource_count(cts->rescts, req->def);

		if (ctsreq != NULL) {
			if (cts->rescts == NULL)
				cts->rescts = ctsreq;

			ctsreq->amount += req->amount;
		}
		req = req->next;
	}
}

/**
 * @brief
 * 		update_counts_on_end - update a counts structure on the end
 *		of a job
 *
 * @param[in]	cts 	- counts structure to update
 * @param[in]	resreq 	- the resource requirements of the job which
 *							ended
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
update_counts_on_end(counts *cts, resource_req *resreq)
{
	if (cts == NULL || resreq == NULL)
		return;

	cts->running--;

	for(auto req = resreq; req != NULL; req = req->next) {
		auto ctsreq = find_resource_count(cts->rescts, req->def);
		if (ctsreq != NULL)
			ctsreq->amount -= req->amount;
	}
}

/**
 * @brief
 * 		perform a max() between the current list of maxes and a
 *		new list.  If any element from the new list is greater
 *		than the current max, we free the old, and dup the new
 *		and attach it in.
 *
 * @param[in]	cmax - current max
 * @param[in]	new  - new counts lists.  If anything in this list is
 *						greater than the cur_max, it needs to be dup'd.
 *
 * @return	the new max
 * @retval	NULL : error
 */
counts *
counts_max(counts *cmax, counts *ncounts)
{
	counts *cur;
	counts *cur_fmax;
	counts *cmax_head;
	resource_count *cur_res;
	resource_count *cur_res_max;

	if (ncounts == NULL)
		return cmax;

	if (cmax == NULL)
		return dup_counts_list(ncounts);

	cmax_head = cmax;

	for (cur = ncounts; cur != NULL; cur = cur->next) {
		cur_fmax = find_counts(cmax, cur->name);
		if (cur_fmax == NULL) {
			cur_fmax = dup_counts(cur);
			if (cur_fmax == NULL) {
				free_counts_list(cmax_head);
				return NULL;
			}

			cur_fmax->next = cmax_head;
			cmax_head = cur_fmax;
		} else {
			if (cur->running > cur_fmax->running)
				cur_fmax->running = cur->running;

			for (cur_res = cur->rescts; cur_res != NULL; cur_res = cur_res->next) {
				cur_res_max = find_resource_count(cur_fmax->rescts, cur_res->def);
				if (cur_res_max == NULL) {
					cur_res_max = dup_resource_count(cur_res);
					if (cur_res_max == NULL) {
						free_counts_list(cmax_head);
						return NULL;
					}

					cur_res_max->next = cur_fmax->rescts;
					cur_fmax->rescts = cur_res_max;
				} else {
					if (cur_res->amount > cur_res_max->amount)
						cur_res_max->amount = cur_res->amount;
				}
			}
		}
	}
	return cmax_head;
}

/**
 * @brief
 * 		update_universe_on_end - update a pbs universe when a job/resv
 *		ends
 *
 * @param[in]   policy 		- policy info
 * @param[in]	resresv 	- the resresv itself which is ending
 * @param[in]	job_state 	- the new state of a job if resresv is a job
 * @param[in]	flags		- flags to modify behavior of the function
 * 					NO_ALLPART - do not update most of the metadata of the allpart
 *
 * @return	void
 *
 * @par MT-Safe:	no
 */
void
update_universe_on_end(status *policy, resource_resv *resresv, const char *job_state, unsigned int flags)
{
	server_info *sinfo = NULL;
	queue_info *qinfo = NULL;


	if (resresv == NULL)
		return;

	if (resresv->is_job && job_state == NULL)
		return;

	if (!is_resource_resv_valid(resresv, NULL))
		return;

	sinfo = resresv->server;

	if (resresv->is_job) {
		qinfo = resresv->job->queue;
		if (resresv->job != NULL && resresv->execselect != NULL) {
			int need_metadata_update = 0;
			for (const auto& sdef : resresv->execselect->defs) {
				const auto &rdc = policy->resdef_to_check;
				if (rdc.find(sdef) == rdc.end()) {
					policy->resdef_to_check.insert(sdef);
					need_metadata_update = 1;
				}
			}
			if (need_metadata_update) {
				/* Since a new resource was added to resdef_to_check, the meta data needs to be recreated.
				 * This will happen on the next call to node_partition_update()
				 */
				if (sinfo->allpart != NULL) {
					free_resource_list(sinfo->allpart->res);
					sinfo->allpart->res = NULL;
				}
				for (int j = 0; sinfo->queues[j] != NULL; j++) {
					queue_info *q = sinfo->queues[j];
					if (q->allpart != NULL) {
						free_resource_list(q->allpart->res);
						q->allpart->res = NULL;
					}
				}
			}
		}
	}

	if (resresv->ninfo_arr != NULL) {
		for (int i = 0; resresv->ninfo_arr[i] != NULL; i++)
			update_node_on_end(resresv->ninfo_arr[i], resresv, job_state);
	}


	update_server_on_end(policy, sinfo, qinfo, resresv, job_state);

	if (qinfo != NULL)
		update_queue_on_end(qinfo, resresv, job_state);
	/* update soft limits for jobs that are not in reservation */
	if (resresv->is_job && resresv->job->resv_id == NULL)
		update_soft_limits(sinfo, qinfo, resresv);
	/* Mark the metadata stale.  It will be updated in the next call to is_ok_to_run() */
	sinfo->pset_metadata_stale = 1;

	update_resresv_on_end(resresv, job_state);

#ifdef NAS /* localmod 057 */
	site_update_on_end(sinfo, qinfo, resresv);
#endif /* localmod 057 */
	update_preemption_priority(sinfo, resresv);
}

/**
 * @brief
 * 		set_resource - set the values of the resource structure.  This
 *		function can be called in one of two ways.  It can be called
 *		with resources_available value, or the resources_assigned
 *		value.
 *
 * @param[in]	res 	- the resource to set
 * @param[in]	val 	- the value to set upon the resource
 * @param[in]	field 	- the type of field to set (available or assigned)
 *
 * @return	int
 * @retval	1 : success
 * @retval	0 : failure/error
 *
 * @note
 * 		If we have resource type information from the server,
 *		we will use it.  If not, we will try to set the
 *		resource type from the resources_available value first,
 *		then from the resources_assigned
 *
 * @par MT-Safe:	no
 */
int
set_resource(schd_resource *res, const char *val, enum resource_fields field)
{
	resdef *rdef;

	if (res == NULL || val == NULL)
		return 0;

	rdef = find_resdef(res->name);
	res->def = rdef;

	if (rdef != NULL)
		res->type = rdef->type;
	else {
		log_event(PBSEVENT_ERROR, PBS_EVENTCLASS_RESC, LOG_WARNING, res->name, "Can't find resource definition");
		return 0;
	}

	if (field == RF_AVAIL) {
		/* if this resource is being re-set, lets free the memory we previously
		 * allocated in the last call to this function.  We NULL the values just
		 * incase we don't reset them later (e.g. originally set a resource
		 * indirect and then later set it directly)
		 */
		if (res->orig_str_avail != NULL) {
			free(res->orig_str_avail);
			res->orig_str_avail = NULL;
		}
		if (res->indirect_vnode_name != NULL) {
			free(res->indirect_vnode_name);
			res->indirect_vnode_name = NULL;
		}
		if (res->str_avail != NULL) {
			free_string_array(res->str_avail);
			res->str_avail = NULL;
		}

		res->orig_str_avail = string_dup(val);
		if (res->orig_str_avail == NULL)
			return 0;

		if (val[0] == '@') {
			res->indirect_vnode_name = string_dup(&val[1]);
			/* res -> indirect_res is assigned by a call to
			 * resolve_indirect_resources()
			 */
			if (res->indirect_vnode_name == NULL)
				return 0;
		} else {
			/* if val is a string, avail will be set to SCHD_INFINITY_RES */
			res->avail = res_to_num(val, NULL);
			if (res->avail == SCHD_INFINITY_RES) {
				/* Verify that this is a string type resource */
				if (!res->def->type.is_string) {
					log_event(PBSEVENT_ERROR, PBS_EVENTCLASS_RESC, LOG_WARNING, res->name, "Invalid value for consumable resource");
					return 0;
				}
			}
			res->str_avail = break_comma_list(const_cast<char *>(val));
			if (res->str_avail == NULL) {
				log_eventf(PBSEVENT_ERROR, PBS_EVENTCLASS_RESC, LOG_WARNING, res->name, "Invalid value: %s", val);
				return 0;
			}
		}
	} else if (field == RF_ASSN) {
		/* clear previously allocated memory in the case of a reassignment */
		if (res->str_assigned != NULL) {
			free(res->str_assigned);
			res->str_assigned = NULL;
		}
		if (val[0] == '@') /* Indirect resources will be found elsewhere, assign 0 */
			res->assigned = 0;
		else
			res->assigned = res_to_num(val, NULL);
		res->str_assigned = string_dup(val);
		if (res->str_assigned == NULL)
			return 0;
	}

	return 1;
}

/**
 * @brief
 * 		find_indirect_resource - follow the indirect resource pointers
 *		to find the real resource at the end
 *
 * @param[in]	res 	- the indirect resource
 * @param[in]	nodes 	- the nodes to search
 *
 * @return	the indirect resource
 * @retval	NULL	: on error
 *
 * @par MT-Safe:	no
 */
schd_resource *
find_indirect_resource(schd_resource *res, node_info **nodes)
{
	schd_resource *cur_res = NULL;
	int i;
	int error = 0;
	const int max = 10;

	if (res == NULL || nodes == NULL)
		return NULL;

	cur_res = res;

	for (i = 0; i < max && cur_res != NULL &&
		cur_res->indirect_vnode_name != NULL && !error; i++) {
		auto ninfo = find_node_info(nodes, cur_res->indirect_vnode_name);
		if (ninfo != NULL) {
			cur_res = find_resource(ninfo->res, cur_res->def);
			if (cur_res == NULL) {
				error = 1;
				log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_NODE, LOG_DEBUG, __func__,
						"Resource %s is indirect, and does not exist on indirect node %s",
						res->name, ninfo->name.c_str());
			}
		} else {
			error = 1;
			log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_NODE, LOG_DEBUG, __func__,
				"Resource %s is indirect but points to node %s, which was not found",
				res->name, cur_res->indirect_vnode_name);
			cur_res = NULL;
		}
	}
	if (i == max) {
		log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_NODE, LOG_DEBUG, __func__,
			"Attempted %d indirection lookups for resource %s=@%s-- "
			"looks like a cycle, bailing out",
			max, cur_res->name, cur_res->indirect_vnode_name);
		return NULL;
	}

	if (error)
		return NULL;

	return cur_res;
}

/**
 * @brief
 * 		resolve_indirect_resources - resolve indirect resources for node
 *		array
 *
 * @param[in,out]	nodes	-	the nodes to resolve
 *
 * @return	int
 * @retval	1	: if successful
 * @retval	0	: if there were any errors
 *
 * @par MT-Safe:	no
 */
int
resolve_indirect_resources(node_info **nodes)
{
	int i;
	schd_resource *cur_res;
	int error = 0;

	if (nodes == NULL)
		return 0;

	for (i = 0; nodes[i] != NULL; i++) {
		cur_res = nodes[i]->res;
		while (cur_res != NULL) {
			if (cur_res->indirect_vnode_name) {
				cur_res->indirect_res = find_indirect_resource(cur_res, nodes);
				if (cur_res->indirect_res == NULL)
					error = 1;
			}
			cur_res = cur_res->next;
		}
	}

	if (error)
		return 0;

	return 1;
}

/**
 * @brief
 * 		update_preemption_priority - update preemption status when a
 *		job runs/ends
 *
 * @param[in]	sinfo 	- server where job was run
 * @param[in]	resresv - job which was run
 *
 * @return	void
 *
 * @note
 * 		Must be called after update_server_on_run/end() and
 *		update_queue_on_run/end()
 *
 * @note
 * 		The only thing which will change preemption priorities
 *		in the middle of a scheduling cycle is soft user/group/project
 *		limits. If a user, group, or project goes under a limit because
 *		of this job running, we need to update those jobs
 *
 * @par MT-Safe:	no
 */
void
update_preemption_priority(server_info *sinfo, resource_resv *resresv)
{
	if (cstat.preempting && resresv->is_job) {
		if (sinfo->has_soft_limit || resresv->job->queue->has_soft_limit) {
			for (int i = 0; sinfo->jobs[i] != NULL; i++) {
				if (sinfo->jobs[i]->job !=NULL) {
					int usrlim = resresv->job->queue->has_user_limit || sinfo->has_user_limit;
					int grplim = resresv->job->queue->has_grp_limit || sinfo->has_grp_limit;
					int projlim = resresv->job->queue->has_proj_limit || sinfo->has_proj_limit;
					if ((usrlim && (!strcmp(resresv->user, sinfo->jobs[i]->user))) ||
					    (grplim && (!strcmp(resresv->group, sinfo->jobs[i]->group))) ||
					    (projlim && (!strcmp(resresv->project, sinfo->jobs[i]->project))))
						set_preempt_prio(sinfo->jobs[i],
							sinfo->jobs[i]->job->queue, sinfo);
				}
			}

			/* now that we've set all the preempt levels, we need to count them */
			memset(sinfo->preempt_count, 0, NUM_PPRIO * sizeof(int));
			for (int i = 0; sinfo->running_jobs[i] != NULL; i++)
				if (!sinfo->running_jobs[i]->job->can_not_preempt)
					sinfo->
					preempt_count[preempt_level(sinfo->running_jobs[i]->job->preempt)]++;
		}
	}
}

/**
 * @brief
 * 		read_formula - read the formula from a well known file
 *
 * @return	formula in malloc'd buffer
 * @retval	NULL	: on error
 *
 * @par MT-Safe:	no
 */
#define RF_BUFSIZE 1024
char *
read_formula(void)
{
	char *form;
	char *tmp;
	char buf[RF_BUFSIZE];
	size_t bufsize = RF_BUFSIZE;
	FILE *fp;

	if ((fp = fopen(FORMULA_FILENAME, "r")) == NULL) {
		log_event(PBSEVENT_SYSTEM, PBS_EVENTCLASS_REQUEST, LOG_INFO, __func__,
			"Can not open file to read job_sort_formula.  Please reset formula with qmgr.");
		return NULL;
	}

	if ((form = static_cast<char *>(malloc(bufsize + 1))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		fclose(fp);
		return NULL;
	}

	form[0] = '\0';

	/* first line is a comment */
	fgets(buf, RF_BUFSIZE, fp);

	while (fgets(buf, RF_BUFSIZE, fp) != NULL) {
		auto len = strlen(form) + strlen(buf);
		if (len > bufsize) {
			tmp = static_cast<char *>(realloc(form, len*2 + 1));
			if (tmp == NULL) {
				log_err(errno, __func__, MEM_ERR_MSG);
				free(form);
				fclose(fp);
				return NULL;
			}
			form = tmp;
			bufsize = len;
		}
		strcat(form, buf);
	}

	if (form[strlen(form) - 1] == '\n')
		form[strlen(form) - 1] = '\0';

	fclose(fp);
	return form;
}

/**
 * @brief
 * 		dup_status - status copy constructor
 *
 * @param[in]	ost	-	status input
 *
 * @return	duplicated status
 * @retval	NULL	: on error
 */
status *
dup_status(status *ost)
{
	status *nst;

	if (ost == NULL)
		return NULL;

	nst = new status();
	if (nst == NULL)
		return NULL;

	*nst = *ost;

	return nst;
}

/**
 * @brief
 * 		free_queue_list - to free two dimensional queue_list array
 *
 * @param[in]	in_list - List which need to be deleted.
 *
 * @return	void
 */
void
free_queue_list(queue_info *** queue_list)
{
	int i;

	if (queue_list == NULL)
		return;
	for (i = 0; queue_list[i] != NULL; i++)
		free(queue_list[i]);
	free(queue_list);
}

/**
 * @brief
 * 		create_total_counts - This function checks if
 *	    total_*_counts list for user/group/project and alljobcounts
 *	    is empty and if so, it duplicates or creates new counts with the
 *	    user/group/project name mentioned in resource_resv structure.
 *
 * @param[in,out]  sinfo	-	server_info structure used to check and set
 *                          	total_*_counts
 * @param[in,out]  qinfo   	-	queue_info structure used to check and set
 *                          	total_*_counts
 * @param[in]      resresv 	-	resource_resv structure to get user/group/project
 * @param[in]      mode    	-	To state whether total_*_counts in server_info
 *                          	structure needs to be created or in queue_info.
 *
 * @return	void
 */
void
create_total_counts(server_info *sinfo, queue_info *qinfo,
	resource_resv *resresv, int mode)
{
	if (mode == SERVER || mode == ALL) {
		if (sinfo->total_group_counts == NULL) {
			if (sinfo->group_counts != NULL)
				sinfo->total_group_counts = dup_counts_list(
					sinfo->group_counts);
			else if (resresv != NULL)
				sinfo->total_group_counts = find_alloc_counts(
					sinfo->total_group_counts, resresv->group);
		}
		if (sinfo->total_user_counts == NULL) {
			if (sinfo->user_counts != NULL)
				sinfo->total_user_counts = dup_counts_list(
					sinfo->user_counts);
			else if (resresv != NULL)
				sinfo->total_user_counts = find_alloc_counts(
					sinfo->total_user_counts, resresv->user);
		}
		if (sinfo->total_project_counts == NULL) {
			if (sinfo->project_counts != NULL)
				sinfo->total_project_counts = dup_counts_list(
					sinfo->project_counts);
			else if (resresv != NULL)
				sinfo->total_project_counts = find_alloc_counts(
					sinfo->total_project_counts, resresv->project);
		}
		if (sinfo->total_alljobcounts == NULL) {
			if (sinfo->alljobcounts != NULL)
				sinfo->total_alljobcounts = dup_counts_list(sinfo->alljobcounts);
			else
				sinfo->total_alljobcounts = find_alloc_counts(
					sinfo->total_alljobcounts, PBS_ALL_ENTITY);
		}
	}
	if (mode == QUEUE || mode == ALL) {
		if (qinfo->total_group_counts == NULL) {
			if (qinfo->group_counts != NULL)
				qinfo->total_group_counts = dup_counts_list(
					qinfo->group_counts);
			else if (resresv != NULL)
				qinfo->total_group_counts = find_alloc_counts(
					qinfo->total_group_counts, resresv->group);
		}
		if (qinfo->total_user_counts == NULL) {
			if (qinfo->user_counts != NULL)
				qinfo->total_user_counts = dup_counts_list(
					qinfo->user_counts);
			else if (resresv != NULL)
				qinfo->total_user_counts = find_alloc_counts(
					qinfo->total_user_counts, resresv->user);
		}
		if (qinfo->total_project_counts == NULL) {
			if (qinfo->project_counts != NULL)
				qinfo->total_project_counts = dup_counts_list(
					qinfo->project_counts);
			else if (resresv != NULL)
				qinfo->total_project_counts = find_alloc_counts(
					qinfo->total_project_counts, resresv->project);
		}
		if (qinfo->total_alljobcounts == NULL) {
			if (qinfo->alljobcounts != NULL)
				qinfo->total_alljobcounts = dup_counts_list(qinfo->alljobcounts);
			else if (resresv != NULL)
				qinfo->total_alljobcounts = find_alloc_counts(
					qinfo->total_alljobcounts, PBS_ALL_ENTITY);
		}
	}
	return;
}

/**
 * @brief
 * 		update_total_counts update a total counts list on running or
 *         queing a job
 *
 * @param[in]	si		-	server_info structure to use for count updation
 * @param[in]	qi		-	queue_info structure to use for count updation
 * @param[in]	rr		-	resource_resv structure to use for count updation
 * @param[in]  	mode	-	To state whether total_*_counts in server_info
 *                      	structure needs to be updated or in queue_info.
 *
 * @return	void
 *
 */
void
update_total_counts(server_info *si, queue_info* qi,
	resource_resv *rr, int mode)
{
	counts *cts = NULL;
	create_total_counts(si, qi, rr, mode);
	if (((mode == SERVER) || (mode == ALL)) &&
		((si != NULL) && si->has_hard_limit)) {
		cts = si->total_group_counts;
		update_counts_on_run(find_alloc_counts(cts, rr->group), rr->resreq);
		cts = si->total_project_counts;
		update_counts_on_run(find_alloc_counts(cts, rr->project), rr->resreq);
		cts = si->total_alljobcounts;
		update_counts_on_run(cts, rr->resreq);
		cts = si->total_user_counts;
		update_counts_on_run(find_alloc_counts(cts, rr->user), rr->resreq);
	} else if (((mode == QUEUE) || (mode == ALL)) &&
		((qi != NULL) && qi->has_hard_limit)) {
		cts = qi->total_group_counts;
		update_counts_on_run(find_alloc_counts(cts, rr->group), rr->resreq);
		cts = qi->total_project_counts;
		update_counts_on_run(find_alloc_counts(cts, rr->project), rr->resreq);
		cts = qi->total_alljobcounts;
		update_counts_on_run(cts, rr->resreq);
		cts = qi->total_user_counts;
		update_counts_on_run(find_alloc_counts(cts, rr->user), rr->resreq);
	}
}

/**
 * @brief
 * 		update_total_counts_on_end update a total counts list on preempting
 *         a running job
 *
 * @param[in]	si		-	server_info structure to use for count updation
 * @param[in]	qi		-	queue_info structure to use for count updation
 * @param[in]	rr		-	resource_resv structure to use for count updation
 * @param[in]  	mode	-	To state whether total_*_counts in server_info
 *                      	structure needs to be updated or in queue_info.
 *
 * @return	void
 *
 */
void
update_total_counts_on_end(server_info *si, queue_info* qi,
	resource_resv *rr, int mode)
{
	counts *cts = NULL;
	create_total_counts(si, qi, rr, mode);
	if (((mode == SERVER) || (mode == ALL)) &&
		((si != NULL) && si->has_hard_limit)) {
		cts = si->total_group_counts;
		update_counts_on_end(find_alloc_counts(cts, rr->group), rr->resreq);
		cts = si->total_project_counts;
		update_counts_on_end(find_alloc_counts(cts, rr->project), rr->resreq);
		cts = si->total_alljobcounts;
		update_counts_on_end(cts, rr->resreq);
		cts = si->total_user_counts;
		update_counts_on_end(find_alloc_counts(cts, rr->user), rr->resreq);
	} else if (((mode == QUEUE) || (mode == ALL)) &&
		((qi != NULL) &&  qi->has_hard_limit)) {
		cts = qi->total_group_counts;
		update_counts_on_end(find_alloc_counts(cts, rr->group), rr->resreq);
		cts = qi->total_project_counts;
		update_counts_on_end(find_alloc_counts(cts, rr->project), rr->resreq);
		cts = qi->total_alljobcounts;
		update_counts_on_end(cts, rr->resreq);
		cts = qi->total_user_counts;
		update_counts_on_end(find_alloc_counts(cts, rr->user), rr->resreq);
	}
}

/**
 * @brief
 * 		get a unique rank to uniquely identify an object
 *
 * @return	int
 * @retval	unique number for this scheduling cycle
 */
int
get_sched_rank()
{
	cstat.order++;
	return cstat.order;
}


/**
 * @brief
 * 		add_queue_to_list - This function alligns all queues according to
 *                              their priority.
 *
 * @param[in,out]	qlhead	-	address of 3 dimensional queue list.
 * @param[in]		qinfo	-	queue which is getting added in queue_list.
 *
 * @return	int
 * @retval	1	: If successful in adding the qinfo to queue_list.
 * @retval	0	: If failed to add qinfo to queue_list.
 */
int
add_queue_to_list(queue_info **** qlhead, queue_info * qinfo)
{
	int queue_list_size = 0;
	void * temp = NULL;
	queue_info ***temp_list = NULL;
	queue_info ***list_head;

	if (qlhead == NULL)
	    return 0;

	list_head = *qlhead;
	queue_list_size = count_array(list_head);

	temp_list = find_queue_list_by_priority(list_head, qinfo->priority);
	if (temp_list == NULL) {
		temp = realloc(list_head, (queue_list_size + 2) * sizeof(queue_info**));
		if (temp == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			return 0;
		}
		*qlhead = list_head = static_cast<queue_info ***>(temp);
		list_head[queue_list_size] = NULL;
		list_head[queue_list_size + 1] = NULL;
		if (append_to_queue_list(&list_head[queue_list_size], qinfo) == NULL)
			return 0;
	} else {
		if (append_to_queue_list(temp_list, qinfo) == NULL)
			return 0;
	}
	return 1;
}

/**
 * @brief
 * 		find_queue_list_by_priority - function finds out the array of queues
 *                               which matches with the priority passed to this
 *                               function. It returns the base address of matching
 *                               array.
 *
 * @param[in]	list_head 	- 	Head pointer to queue_info list.
 * @param[in]	priority 	-  	Priority of the queue that needs to be searched.
 *
 * @return	queue_info*** - base address of array which has given priority.
 * @retval	NULL	: when function is not able to find the array.
 *
 */
struct queue_info ***find_queue_list_by_priority(queue_info *** list_head, int priority)
{
	int i;
	if (list_head == NULL)
		return NULL;
	for (i = 0; list_head[i] != NULL; i++) {
		if ((list_head[i][0] != NULL) && list_head[i][0]->priority == priority)
			return (&list_head[i]);
	}
	return NULL;

}

/**
 * @brief
 * 		append_to_queue_list - function that will reallocate and append
 *                               "add" to the list provided.
 * @param[in,out]	list	-	pointer to queue_info** which gets reallocated
 *                       		and "add" is appended to it.
 * @param[in] 		add 	-   queue_info  that needs to be appended.
 *
 * @return	queue_info** : newly appended list.
 * @retval	NULL	: when realloc fails.
 *           			pointer to appended list.
 */
struct queue_info **append_to_queue_list(queue_info ***list, queue_info *add)
{
	int count = 0;
	queue_info ** temp = NULL;

	if ((list == NULL) || (add == NULL))
		return NULL;
	count = count_array(*list);

	/* count contains number of elements in list (excluding NULL). we add 2 to add the NULL
	 * back in, plus our new element.
	 */
	temp = (queue_info**) realloc(*list, (count + 2) * sizeof(queue_info*));
	if (temp == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}
	temp[count]  = add;
	temp[count + 1] = NULL;
	*list = temp;
	return (*list);
}

/**
 * @brief basically do a reslist->assigned += reqlist->amount for all of reqlist
 * @param reslist - resource list
 * @param reqlist - resource_req list
 * @return
 */
void
add_req_list_to_assn(schd_resource *reslist, resource_req *reqlist)
{
	if(reslist == NULL || reqlist == NULL)
		return;

	for(auto req = reqlist; req != NULL; req = req->next) {
		auto r = find_resource(reslist, req->def);
		if(r != NULL && r->type.is_consumable)
			r->assigned += req->amount;
	}
}

/**
 * @brief create the ninfo->res->assigned values for the node
 * @param ninfo - the node
 * @return int
 * @retval 1 success
 * @retval 0 failure
 */
int
create_resource_assn_for_node(node_info *ninfo)
{
	schd_resource *r;
	schd_resource *ncpus_res = NULL;
	int i;

	if(ninfo == NULL)
		return 0;

	for (r = ninfo->res; r != NULL; r = r->next)
		if(r->type.is_consumable) {
			r->assigned = 0;
			if (r->def == allres["ncpus"])
				ncpus_res = r;
		}

	/* First off, add resource from running jobs (that aren't in resvs) */
	if (ninfo->job_arr != NULL) {
		for (i = 0; ninfo->job_arr[i] != NULL; i++) {
			/* ignore jobs in reservations.  The resources will be accounted for with the reservation itself.  */
			if (ninfo->job_arr[i]->job != NULL && ninfo->job_arr[i]->job->resv == NULL) {
				if (ninfo->job_arr[i]->nspec_arr != NULL) {
					int j;
					for (j = 0; ninfo->job_arr[i]->nspec_arr[j] != NULL; j++) {
						nspec *n = ninfo->job_arr[i]->nspec_arr[j];
						if (n->ninfo->rank == ninfo->rank)
							add_req_list_to_assn(ninfo->res, n->resreq);
					}
				}
			}
		}
	}

	/* Next up, account for running reservations.  Running reservations consume all resources on the node when they start.  */
	if (ninfo->run_resvs_arr != NULL) {
		for (i = 0; ninfo->run_resvs_arr[i] != NULL; i++) {
			if (ninfo->run_resvs_arr[i]->nspec_arr != NULL) {
				int j;
				for (j = 0; ninfo->run_resvs_arr[i]->nspec_arr[j] != NULL; j++) {
					nspec *n = ninfo->run_resvs_arr[i]->nspec_arr[j];
					if (n->ninfo->rank == ninfo->rank)
						add_req_list_to_assn(ninfo->res, n->resreq);
				}
			}
		}
	}

	/* Lastly if restrict_res_to_release_on_suspend is set, suspended jobs may not have released all their resources
	 * This is tricky since a suspended job knows what resources they released.
	 * We need to know what they didn't release to account for in the nodes resources_assigned
	 * Also, we only need to deal with suspended jobs outside of reservations since resources for reservations were handled above.
	 */
	if (ninfo->num_susp_jobs > 0) {
		int i;
		server_info *sinfo = ninfo->server;
		for (i = 0; sinfo->jobs[i] != NULL; i++) {
			if (sinfo->jobs[i]->job->is_suspended && sinfo->jobs[i]->job->resv == NULL) {
				nspec *ens;
				ens = find_nspec(sinfo->jobs[i]->nspec_arr, ninfo);
				if (ens != NULL) {
					nspec *rns;
					rns = find_nspec(sinfo->jobs[i]->job->resreleased, ninfo);
					if (rns != NULL) {
						resource_req *cur_req;
						for (cur_req = ens->resreq; cur_req != NULL; cur_req = cur_req->next) {
							if (cur_req->type.is_consumable)
								if (find_resource_req(rns->resreq, cur_req->def) == NULL) {
									schd_resource *nres;
									nres = find_resource(ninfo->res, cur_req->def);
									if (nres != NULL)
										nres->assigned += cur_req->amount;
								}
						}
					}
				}
			}
		}
	}

	if (ncpus_res != NULL && ncpus_res->assigned < ncpus_res->avail)
		remove_node_state(ninfo, ND_jobbusy);

	return 1;
}

/**
 * @brief compares two schd_resource structs for equality
 *
 * @return int
 * @retval 1 if equal
 * @retval 0 if not equal
 */
int
compare_resource_avail(schd_resource *r1, schd_resource *r2) {
	if (r1 == NULL && r2 == NULL)
		return 1;
	if (r1 == NULL || r2 == NULL)
		return 0;

	if (r1->def->type.is_string) {
		if(match_string_array(r1->str_avail, r2->str_avail) == SA_FULL_MATCH)
			return 1;
		else
			return 0;
	}
	if (r1->avail == r2->avail)
		return 1;

	return 0;
}

/**
 * @brief compare two schd_resource lists for equality
 * @return int
 * @retval 1 if equal
 * @retval 0 if not equal
 */
int
compare_resource_avail_list(schd_resource *r1, schd_resource *r2) {
	schd_resource *cur;

	if (r1 == NULL && r2 == NULL)
		return 1;
	if (r1 == NULL || r2 == NULL)
		return 0;

	for (cur = r1; cur != NULL; cur = cur->next) {
		schd_resource *res;

		res = find_resource(r2, cur->def);
		if(res != NULL) {
			if(compare_resource_avail(cur, res) == 0)
				return 0;
		} else if (cur->type.is_boolean) { /* Unset boolean == False */
			if (cur->avail != 0)
				return 0;
		} else
			return 0;
	}

	return 1;
}

/**
 * @brief dup sinfo->unordered_nodes from the nnodes array.
 * @param[in] old_unordered_nodes - unordered_nodes array to dup
 * @param[in] nnodes - nodes from new universe.  Nodes are references into this
 * 		array
 *
 * @return new unordered_nodes
 */
node_info **
dup_unordered_nodes(node_info **old_unordered_nodes, node_info **nnodes)
{
	int i;
	int ct1;
	int ct2;
	node_info **new_unordered_nodes;

	if (old_unordered_nodes == NULL || nnodes == NULL)
		return NULL;

	ct1 = count_array(nnodes);
	ct2 = count_array(old_unordered_nodes);

	if(ct1 != ct2)
		return NULL;

	new_unordered_nodes = static_cast<node_info **>(calloc((ct1 + 1), sizeof(node_info *)));
	if (new_unordered_nodes == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	for (i = 0; i < ct1; i++)
		new_unordered_nodes[nnodes[i]->node_ind] = nnodes[i];

	new_unordered_nodes[ct1] = NULL;

	return new_unordered_nodes;
}
