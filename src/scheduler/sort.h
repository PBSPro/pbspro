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

#ifndef	_SORT_H
#define	_SORT_H

/*
 *	compare two new numerical resource numbers
 *
 *	returns -1 if r1 < r2
 *		0  if r1 == r2
 *		1  if r1 > r2
 */
int cmpres(sch_resource_t r1, sch_resource_t r2);

/*
 * cmp_nspec - sort nspec by sequence number
 *
 */
int cmp_nspec(const void *v1, const void *v2);

/*
 * cmp_nspec_by_sub_seq - sort nspec by sub sequence number
 */
int cmp_nspec_by_sub_seq(const void *v1, const void *v2);

/*
 *	cmp_placement_sets - sort placement sets by
 *			total cpus
 *			total memory
 *			free cpus
 *			free memory
 */
int cmp_placement_sets(const void *v1, const void *v2);

/*
 * cmp_fairshare - compare based on compare_path()
 */
int cmp_fairshare(const void *j1, const void *j2);

/*
 *
 *      cmp_queue_prio_dsc - compare function used by qsort to sort queues
 *                           by decending priority
 *
 */
bool cmp_queue_prio_dsc(const queue_info *q1, const queue_info *q2);

/*
 *      cmp_fair_share - compare function for the fair share algorithm
 */
int cmp_fair_share(const void *j1, const void *j2);

/*
 *      cmp_preempt_priority_asc - used to sort jobs in decending preemption
 *                                 priority
 */
int cmp_preempt_priority_asc(const void *j1, const void *j2);

/*
 *      cmp_preempt_stime_asc - used to soft jobs in ascending preemption
 *                              start time
 */
int cmp_preempt_stime_asc(const void *v1, const void *v2);

/*
 *      multi_sort - a multi keyed sortint compare function
 */
int multi_sort(resource_resv *r1, resource_resv *r2);

/*
 *	cmp_job_sort_formula - used to sort jobs in descending order of their evaluated formula value
 */
int cmp_job_sort_formula(const void *j1, const void *j2);

/*
 *
 *      cmp_sort - entrypoint into job sort used by qsort
 */
int cmp_sort(const void *v1, const void *v2);

/*
 *      find_resresv_amount - find resource amount for jobs + special cases
 */
sch_resource_t find_resresv_amount(resource_resv *resresv, const std::string& res, resdef *def);

/*
 *      find_node_amount - find the resource amount for nodes + special cases
 */
sch_resource_t find_node_amount(node_info *ninfo, const std::string& res, resdef *def, enum resource_fields res_type);

/* return resource values based on res_type for node partition */
sch_resource_t find_nodepart_amount(node_partition *np, const std::string& res, resdef *def, enum resource_fields res_type);

sch_resource_t find_bucket_amount(node_bucket *bkt, const std::string& res, resdef *def, enum resource_fields res_type);


/*
 * Compares either two nodes or node_partitions based on a resource,
 * Ascending/Descending, and what part of the resource to use (total, unused, etc)
 */
int node_sort_cmp(const void *vp1, const void *vp2, const struct sort_info& si, enum sort_obj_type obj_type);

/*
 *      resresv_sort_cmp - compares 2 jobs on the current resresv sort
 *                      used with qsort (qsort calls multi_sort())
 */
int resresv_sort_cmp(resource_resv *r1, resource_resv *r2, const sort_info& si);

/*
 *      multi_node_sort - a multi keyed sorting compare function for nodes
 */
int multi_node_sort(const void *n1, const void *n2);


/* qsort() compare function for multi-resource node partition sorting */
int multi_nodepart_sort(const void *n1, const void *n2);

/* qsort() compare function for multi-resource bucket sorting */
int multi_bkt_sort(const void *b1, const void *b2);

/*
 *	cmp_events - sort jobs/resvs into a timeline of the next even to
 *		happen: running jobs ending, advanced reservations starting
 *		or ending
 */
int cmp_events(const void *v1, const void *v2);

/* sort nodes by resources_available.host */
int cmp_node_host(const void *v1, const void *v2);


/* sorting routine to be used with PROVPOLICY_AVOID only */
int cmp_aoe(const void *v1, const void *v2);

/*
 *      cmp_job_preemption_time_asc- used to sort jobs in ascending preempted
 *                                 time.
 */
int cmp_job_preemption_time_asc(const void *j1, const void *j2);

/*
 *  cmp_preemption - This function is use to sort jobs according to their
 *  preemption priority
 */
int cmp_preemption(resource_resv *r1, resource_resv *r2);

/*
 * cmp_resv_state - compare based on resv_state
 */
int cmp_resv_state(const void *r1, const void *r2);

/*
 * sort_jobs - This function sorts all jobs according to their preemption
 *             priority, preempted time and fairshare.
 */
void sort_jobs(status *policy, server_info *sinfo);

#endif	/* _SORT_H */
