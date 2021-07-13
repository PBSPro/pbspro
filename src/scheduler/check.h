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


#ifndef	_CHECK_H
#define	_CHECK_H

#include <unordered_set>

#include "server_info.h"
#include "queue_info.h"
#include "job_info.h"

/*
 *	is_ok_to_run_in_queue - check to see if jobs can be run in queue
 */
enum sched_error_code is_ok_to_run_queue(status *policy, queue_info *qinfo);

/*
 *	is_ok_to_run - check to see if it ok to run a job on the server
 */
nspec **
is_ok_to_run(status *policy, server_info *sinfo,
	queue_info *qinfo, resource_resv *resresv, unsigned int flags, schd_error *perr);

/**
 *
 *	is_ok_to_run_STF - check to see if the STF job is OK to run.
 *
 */
nspec **
is_ok_to_run_STF(status *policy, server_info *sinfo,
	queue_info *qinfo, resource_resv *njob, unsigned int flags, schd_error *err,
	nspec **(*shrink_heuristic)(status *policy, server_info *sinfo,
	queue_info *qinfo, resource_resv *njob, unsigned int flags, schd_error *err));
/*
 * shrink_job_algorithm - generic algorithm for shrinking a job
 */
nspec **
shrink_job_algorithm(status *policy, server_info *sinfo,
	queue_info *qinfo, resource_resv *njob, unsigned int flags, schd_error *err);/* Generic shrinking heuristic */

/*
 * shrink_to_boundary - Shrink job to dedicated/prime time boundary
 */
nspec **
shrink_to_boundary(status *policy, server_info *sinfo,
	queue_info *qinfo, resource_resv *njob, unsigned int flags, schd_error *err);

/*
 * shrink_to_minwt - Shrink job to it's minimum walltime
 */
nspec **
shrink_to_minwt(status *policy, server_info *sinfo,
	queue_info *qinfo, resource_resv *njob, unsigned int flags, schd_error *err);

/*
 * shrink_to_run_event - Shrink job before reservation or top job.
 */
nspec **
shrink_to_run_event(status *policy, server_info *sinfo,
	queue_info *qinfo, resource_resv *njob, unsigned int flags, schd_error *err);

/*
 *      check_avail_resources - This function will calculate the number of
 *				multiples of the requested resources in reqlist
 *				which can be satisfied by the resources
 *				available in the reslist for the resources in
 *				checklist
 *
 *
 *      returns number of chunks which can be allocated or -1 on error
 *
 */
long long
check_avail_resources(schd_resource *reslist, resource_req *reqlist,
	unsigned int flags, std::unordered_set<resdef *>& res_to_check,
	enum sched_error_code fail_code, schd_error *err);
long long
check_avail_resources(schd_resource *reslist, resource_req *reqlist,
		      unsigned int flags, enum sched_error_code fail_code, schd_error *perr);

/*
 *	dynamic_avail - find out how much of a resource is available on a
 */
sch_resource_t dynamic_avail(schd_resource *res);

/*
 *	find_counts_elm - find a element of a counts structure by name.
 *			  If res arg is NULL return number of running jobs
 *			  otherwise return named resource
 *
 *	cts_list - counts map to search
 *	name     - name of counts structure to find
 *	res      - resource to find or if NULL, return number of running
 *			resource amount
 *	cnt	- output param for address of the matching counts structure
 *	rreq	- output param for address of the matching resource_count structure
 */
sch_resource_t find_counts_elm(counts_umap &cts_list, const std::string &name, resdef *res, counts **cnt, resource_count **rreq);


/*
 *      check_nodes - check to see if there is sufficient nodes available to
 *                    run a job/resv.
 */
nspec **check_nodes(status *policy, server_info *sinfo, queue_info *qinfo, resource_resv *resresv, unsigned int flags, schd_error *err);

/* Normal node searching algorithm */
nspec **
check_normal_node_path(status *policy, server_info *sinfo, queue_info *qinfo, resource_resv *resresv, unsigned int flags, schd_error *err);


/*
 *      is_node_available - determine that there is a node available to run
 *                          the job
 */
int is_node_available(resource_resv *job, node_info **ninfo_arr);

/*
 *      check_ded_time_queue - check if it is the approprate time to run jobs
 *                             in a dedtime queue
 */
enum sched_error_code check_ded_time_queue(queue_info *qinfo);

/*
 *      dedtime_conflict - check for dedtime conflicts
 */
int dedtime_conflict(resource_resv *resresv);

/*
 *      check_ded_time_boundary  - check to see if a job would cross into
 */
enum sched_error_code check_ded_time_boundary(resource_resv *job);

/*
 *      check_prime_queue - Check primetime status of the queue.  If the queue
 *                          is a primetime queue and it is primetime or if the
 *                          queue is an anytime queue, jobs can run in it.
 */
enum sched_error_code check_prime_queue(status *policy, queue_info *qinfo);

/*
 *      check_nonprime_queue - Check nonprime status of the queue.  If the
 *                             queue is a nonprime queue and it is nonprimetime
 *                             of the queue is an anytime queue, jobs can run
 */
enum sched_error_code check_nonprime_queue(status *policy, queue_info *qinfo);

/*
 *      check_prime_boundary - check to see if the job can run before the prime
 *                            status changes (from primetime to nonprime etc)
 */
enum sched_error_code check_prime_boundary(status *policy, resource_resv *resresv, struct schd_error *err);


/*
 *      check_node_resources - check to see if resources are available on
 *                             timesharing nodes for a job to run
 */
int check_node_resources(resource_resv *resresv, node_info **ninfo_arr);

/*
 *	false_res - return a static struct of resource which is a boolean
 *		    set to false
 */
schd_resource *false_res(void);

/*
 *
 *	zero_res -  return a static struct of resource which is numeric and
 *		    consumable set to 0
 *	returns zero resource ptr
 *
 */
schd_resource *zero_res(void);

/*
 *	unset_str_res - return a static struct of resource which is a string
 *		    set to ""
 *	returns unset string resource ptr
 *
 */
schd_resource *unset_str_res(void);

/*
 *	get_resresv_spec - gets the correct select and placement specification
 *
 *	returns void
 */
void get_resresv_spec(resource_resv *resresv, selspec **spec, place **pl);
#endif	/* _CHECK_H */
