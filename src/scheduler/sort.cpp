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
 * @file    sort.c
 *
 * @brief
 * 		sort.c - This file will hold the compare functions used by qsort
 *		to sort the jobs
 *
 * Functions included are:
 * 	cmpres()
 * 	cmp_placement_sets()
 * 	cmp_nspec()
 * 	cmp_queue_prio_dsc()
 * 	cmp_events()
 * 	cmp_fairshare()
 * 	cmp_preempt_priority_asc()
 * 	cmp_preempt_stime_asc()
 * 	cmp_preemption()
 * 	multi_sort()
 * 	cmp_job_sort_formula()
 * 	multi_node_sort()
 * 	multi_nodepart_sort()
 * 	resresv_sort_cmp()
 * 	node_sort_cmp()
 * 	cmp_sort()
 * 	find_nodepart_amount()
 * 	find_node_amount()
 * 	find_resresv_amount()
 * 	cmp_node_host()
 * 	cmp_aoe()
 * 	cmp_job_preemption_time_asc()
 * 	sort_jobs()
 * 	swapfunc()
 * 	med3()
 * 	qsort()
 *
 */
#include <pbs_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <log.h>
#include "data_types.h"
#include "sort.h"
#include "resource_resv.h"
#include "misc.h"
#include "globals.h"
#include "fairshare.h"
#include "fifo.h"
#include "node_info.h"
#include "check.h"
#include "constant.h"
#include "server_info.h"
#include "resource.h"

#ifdef NAS
#include "site_code.h"
#endif



/**
 * @brief
 *		compare two new numerical resource numbers for a descending sort
 *
 * @param[in]	r1	-	resource 1
 * @param[in]	r2	-	resource 2
 *
 * @return	int
 * @retval	-1	: if r1 < r2
 * @retval	0 	: if r1 == r2
 * @retval	1  	: if r1 > r2
 */
int
cmpres(sch_resource_t r1, sch_resource_t r2)
{
	if (r1 == SCHD_INFINITY_RES && r2 == SCHD_INFINITY_RES)
		return 0;
	if (r1 == SCHD_INFINITY_RES)
		return -1;
	if (r2 == SCHD_INFINITY_RES)
		return 1;
	if (r1 < r2)
		return -1;
	if (r1 == r2)
		return 0;

	return 1;
}

/**
 * @brief
 *		cmp_placement_sets - sort placement sets by
 *			total cpus
 *			total memory
 *			free cpus
 *			free memory
 *
 * @param[in]	v1	-	node partition 1
 * @param[in]	v2	-	node partition 2
 *
 * @return	int
 * @retval	-1	: if v1 < v2
 * @retval	0 	: if v1 == v2
 * @retval	1  	: if v1 > v2
 */
int
cmp_placement_sets(const void *v1, const void *v2)
{
	node_partition *np1, *np2;
	schd_resource *ncpus1, *ncpus2;
	schd_resource *mem1, *mem2;
	int rc = 0;


	if (v1 == NULL && v2 == NULL)
		return 0;

	if (v1 == NULL && v2 != NULL)
		return -1;

	if (v1 != NULL && v2 == NULL)
		return 1;

	np1 = *((node_partition **) v1);
	np2 = *((node_partition **) v2);

	ncpus1 = find_resource(np1->res, allres["ncpus"]);
	ncpus2 = find_resource(np2->res, allres["ncpus"]);

	if (ncpus1 != NULL && ncpus2 != NULL)
		rc = cmpres(ncpus1->avail, ncpus2->avail);

	if (!rc) {
		mem1 = find_resource(np1->res, allres["mem"]);
		mem2 = find_resource(np2->res, allres["mem"]);

		if (mem1 != NULL && mem2 != NULL)
			rc = cmpres(mem1->avail, mem2->avail);
	}

	if (!rc) {
		if (ncpus1 != NULL && ncpus2 != NULL)
			rc = cmpres(dynamic_avail(ncpus1), dynamic_avail(ncpus2));
	}


	if (!rc) {
		if (mem1 != NULL && mem2 != NULL)
			rc = cmpres(dynamic_avail(mem1), dynamic_avail(mem2));
	}

	return rc;

}

/**
 * @brief
 * 		cmp_nspec - sort nspec by sequence number
 *
 * @param[in]	v1	-	nspec 1
 * @param[in]	v2	-	nspec 2
 *
 * @return	int
 * @retval	-1	: if v1 < v2
 * @retval	0 	: if v1 == v2
 * @retval	1  	: if v1 > v2
 */
int
cmp_nspec(const void *v1, const void *v2)
{
	int s1, s2;
	if (v1 == NULL && v2 == NULL)
		return 0;

	if (v1 == NULL && v2 != NULL)
		return -1;

	if (v1 != NULL && v2 == NULL)
		return 1;

	s1 = (*(nspec**) v1)->seq_num;
	s2 = (*(nspec**) v2)->seq_num;

	if (s1 < s2)
		return -1;
	else if (s1 > s2)
		return 1;
	else
	    return cmp_nspec_by_sub_seq(v1, v2);
}

/**
 * @brief
 * 		cmp_nspec_by_sub_seq - sort nspec by sub sequence number
 *
 * @param[in]	v1	-	nspec 1
 * @param[in]	v2	-	nspec 2
 *
 * @return	int
 * @retval	-1	: if v1 < v2
 * @retval	0 	: if v1 == v2
 * @retval	1  	: if v1 > v2
 */
int
cmp_nspec_by_sub_seq(const void *v1, const void *v2)
{
	int ss1, ss2;
	if (v1 == NULL && v2 == NULL)
		return 0;

	if (v1 == NULL && v2 != NULL)
		return -1;

	if (v1 != NULL && v2 == NULL)
		return 1;

	ss1 = (*(nspec**) v1)->sub_seq_num;
	ss2 = (*(nspec**) v2)->sub_seq_num;

	if (ss1 < ss2)
		return -1;
	else if (ss1 > ss2)
		return 1;
	else
		return 0;
}


/**
 * @brief
 *      cmp_queue_prio_dsc - sort queues in decending priority
 *
 * @param[in]	q1	-	queue_info 1
 * @param[in]	q2	-	queue_info 2
 *
 * @return	int
 * @retval	1	: if q1 < q2
 * @retval	0 	: if q1 == q2
 * @retval	-1  : if q1 > q2
 */
bool
cmp_queue_prio_dsc (const queue_info *q1, const queue_info *q2)
{
	return (q2->priority < q1->priority);
}

/**
 * @brief
 *		cmp_events - sort jobs/resvs into a timeline of the next event
 *		to happen: running jobs ending, advanced reservations starting
 *		or ending
 *
 * @param[in]	v1	-	resource_resv 1
 * @param[in]	v2	-	resource_resv 2
 *
 * @return	int
 * @retval	-1	: if v1 < v2
 * @retval	0 	: if v1 == v2
 * @retval	1  	: if v1 > v2
 */
int
cmp_events(const void *v1, const void *v2)
{
	resource_resv *r1, *r2;
	time_t t1, t2;
	int run1, run2;			/* are r1 and r2 in runnable states? */
	int end_event1 = 0, end_event2 = 0;	/* are r1 and r2 end events? */

	r1 = *((resource_resv **) v1);
	r2 = *((resource_resv **) v2);

	if (r1->start != UNSPECIFIED && r2->start ==UNSPECIFIED)
		return -1;

	if (r1->start == UNSPECIFIED && r2->start ==UNSPECIFIED)
		return 0;

	if (r1->start == UNSPECIFIED && r2->start !=UNSPECIFIED)
		return 1;

	run1 = in_runnable_state(r1);
	run2 = in_runnable_state(r2);


	if (r1->start >= r1->server->server_time && run1)
		t1 = r1->start;
	else {
		end_event1 = 1;
		t1 = r1->end;
	}

	if (r2->start >= r2->server->server_time && run2)
		t2 = r2->start;
	else {
		end_event2 = 1;
		t2 = r2->end;
	}

	if (t1 < t2)
		return -1;
	else if (t1 == t2) {
		/* if event times are equal, this means that events which consume
		 * resources and release resources happen at the same time.  We need to
		 * make sure events which release resources come first, so the events
		 * which consume them, can indeed do that.
		 */
		if (end_event1)
			return -1;
		else if (end_event2)
			return 1;
		else
			return 0;
	}
	else
		return 1;
}

/**
 * @brief
 *		cmp_fair_share - compare on fair share percentage only.
 *			 This is for strict priority.
 *
 * @param[in]	j1	-	resource_resv 1
 * @param[in]	j2	-	resource_resv 2
 *
 * @return	int
 * @retval	1	: if j1 < j2
 * @retval	0 	: if j1 == j2
 * @retval	-1  : if j1 > j2
 */
int
cmp_fairshare(const void *j1, const void *j2)
{
	resource_resv *r1 = *(resource_resv**)j1;
	resource_resv *r2 = *(resource_resv**)j2;
	if (r1->job != NULL && r1->job->ginfo != NULL &&
		r2->job != NULL && r2->job->ginfo != NULL)
		return compare_path(r1->job->ginfo->gpath , r2->job->ginfo->gpath);

	return 0;
}

/**
 * @brief
 *		cmp_preempt_priority_asc - used to sort jobs in ascending preemption
 *				   priority
 *
 * @param[in]	j1	-	resource_resv 1
 * @param[in]	j2	-	resource_resv 2
 *
 * @return	int
 * @retval	-1	: if j1 < j2
 * @retval	0 	: if j1 == j2
 * @retval	1  : if j1 > j2
 */
int
cmp_preempt_priority_asc(const void *j1, const void *j2)
{
	if ((*(resource_resv **) j1)->job->preempt < (*(resource_resv **) j2)->job->preempt)
		return -1;
	else if ((*(resource_resv **) j1)->job->preempt > (*(resource_resv **) j2)->job->preempt)
		return 1;
	else {
		if ((*(resource_resv **) j1)->rank < (*(resource_resv **) j2)->rank)
			return -1;
		else if ((*(resource_resv **) j1)->rank > (*(resource_resv **) j2)->rank)
			return 1;
	}

	return 0;
}

/**
 * @brief
 *      cmp_preempt_stime_asc - used to soft jobs in ascending preemption
 *                              priority and start time
 *
 * @param[in]	j1	-	resource_resv 1
 * @param[in]	j2	-	resource_resv 2
 *
 * @return	int
 * @retval	-1	: if j1 < j2
 * @retval	0 	: if j1 == j2
 * @retval	1   : if j1 > j2
 */
int
cmp_preempt_stime_asc(const void *j1, const void *j2)
{
	if ((*(resource_resv **) j1)->job->preempt < (*(resource_resv **) j2)->job->preempt)
		return -1;
	else if ((*(resource_resv **) j1)->job->preempt > (*(resource_resv **) j2)->job->preempt)
		return 1;
	else {
		/* sort by start time */
		if ((*(resource_resv **) j1)->job->stime > (*(resource_resv **) j2)->job->stime)
			return -1;
		if ((*(resource_resv **) j1)->job->stime < (*(resource_resv **) j2)->job->stime)
			return 1;
	}

	return 0;
}

/**
 * @brief
 *  	cmp_premeption  - compare first by preemption priority and then
 *                   		if the job had been preempted, the order preempted.
 *
 * @param[in]	r1	-	resource_resv 1
 * @param[in]	r2	-	resource_resv 2
 *
 * @return	int
 * @retval	1	: if r1 < r2
 * @retval	0 	: if r1 == r2
 * @retval	-1  : if r1 > r2
 */
int
cmp_preemption(resource_resv *r1, resource_resv *r2)
{
	if (r1 != NULL && r2 == NULL)
		return -1;

	if (r1 == NULL && r2 == NULL)
		return 0;

	if (r1 == NULL && r2 != NULL)
		return 1;

	/* error, allow some other sort key to take over */
	if (r1->job == NULL || r2->job ==NULL)
		return 0;

	if (r1->job->preempt < r2->job->preempt)
		return 1;
	else if (r1->job->preempt > r2->job->preempt)
		return -1;

	return 0;
}

/* multi keyed sorting
 * call compare function to sort for the first key
 * if the two keys are equal, call the compare funciton for the second key
 * repeat for all keys
 */


/**
 * @brief
 * 		multi_sort - a multi keyed sorting compare function for jobs
 *
 * @param[in] r1 - job to compare
 * @param[in] r2 - job to compare
 *
 * @return int
 * @retval -1, 0, 1 : standard qsort() cmp
 */
int
multi_sort(resource_resv *r1, resource_resv *r2)
{
	int ret = 0;

	for (const auto& si : *cstat.sort_by) {
		ret = resresv_sort_cmp(r1, r2, si);
		if (ret)
			break;
	}

	return ret;
}

/**
 * @brief
 * 		cmp_job_sort_formula - used to sort jobs based on their evaluated
 *				      job_sort_formula value (in DESC order)
 * @param[in] r1 - job to compare
 * @param[in] r2 - job to compare
 *
 * @return int
 * @retval -1, 0, 1 : standard qsort() cmp
 */
int
cmp_job_sort_formula(const void *j1, const void *j2)
{
	resource_resv *r1 = *(resource_resv**)j1;
	resource_resv *r2 = *(resource_resv**)j2;

	if (r1->job->formula_value < r2->job->formula_value)
	    return 1;
	if (r1->job->formula_value > r2->job->formula_value)
	    return -1;
	return 0;
}

/**
 * @brief
 *		multi_node_sort - a multi keyed sorting compare function for nodes
 *
 * @param[in] n1 - node1 to compare
 * @param[in] n2 - node2 to compare
 *
 * @return int
 *	@retval -1, 0, 1 - standard qsort() cmp
 */
int
multi_node_sort(const void *n1, const void *n2)
{
	int ret = 0;

	for (const auto& si : *cstat.node_sort) {
		ret = node_sort_cmp(n1, n2, si, SOBJ_NODE);
		if (ret)
			break;
	}

	return ret;
}


/**
 * @brief
 * 		qsort() compare function for multi-resource node partition sorting
 *
 * @param[in] n1 - nodepart 1 to compare
 * @param[in] n2 - nodepart 2 to compare
 *
 * @return int
 *	@retval -1, 0, 1 - standard qsort() cmp
 */
int
multi_nodepart_sort(const void *n1, const void *n2)
{
	int ret = 0;

	for (const auto& si : *cstat.node_sort) {
		ret = node_sort_cmp(n1, n2, si, SOBJ_PARTITION);
		if (ret)
			break;
	}
	return ret;
}

/**
 * @brief
 *		multi_bkt_sort - a multi keyed sorting compare function for node buckets
 *
 * @param[in] b1 - bkt1 to compare
 * @param[in] b2 - bkt2 to compare
 *
 * @return int
 *	@retval -1, 0, 1 - standard qsort() cmp
 */
int
multi_bkt_sort(const void *b1, const void *b2)
{
	int ret = 0;

	for (const auto& si : *cstat.node_sort) {
		ret = node_sort_cmp(b1, b2, si, SOBJ_BUCKET);
		if (ret)
			break;
	}

	return ret;
}

/**
 * @brief
 * 		compares two jobs using a sort defined by a sort_info
 *		This is used downstream by qsort()
 *
 * @param[in] r1	-	first job to compare
 * @param[in] r2 	-	second job to compare
 * @param[in] si 	- 	sort_info describing how to sort the jobs
 *
 * @returns -1, 0, 1 : standard qsort()) cmp
 *
 */
int
resresv_sort_cmp(resource_resv *r1, resource_resv *r2, const struct sort_info& si)
{
	sch_resource_t v1, v2;

	if (r1 != NULL && r2 == NULL)
		return -1;

	if (r1 == NULL && r2 == NULL)
		return 0;

	if (r1 == NULL && r2 != NULL)
		return 1;

	v1 = find_resresv_amount(r1, si.res_name, si.def);
	v2 = find_resresv_amount(r2, si.res_name, si.def);

	if (v1 == v2)
		return 0;

	if (si.order == ASC) {
		if (v1 < v2)
			return -1;
		else
			return 1;
	}
	else {
		if (v1 < v2)
			return 1;
		else
			return -1;
	}
}

/**
 * @brief
 * 		compares either two nodes or node_partitions based on a resource,
 *              Ascending/Descending, and what part of the resource to use (total, unused, etc)
 *
 * @param[in] vp1 		- the node/parts/bkts to compare
 * @param[in] vp2 		- the node/parts/bkts to compare
 * @param[in] si 		- sort info describing how to sort nodes
 * @param[in] obj_type 	- node or node_partition
 *
 * @return int
 * @retval -1, 0, 1 : standard qsort() cmp
 */
int
node_sort_cmp(const void *vp1, const void *vp2, const struct sort_info& si, const enum sort_obj_type obj_type)
{
	sch_resource_t v1, v2;
	node_info **n1 = NULL;
	node_info **n2 = NULL;
	node_partition **np1 = NULL;
	node_partition **np2 = NULL;
	node_bucket **b1 = NULL;
	node_bucket **b2 = NULL;
	int rank1, rank2;

	if (vp1 != NULL && vp2 == NULL)
		return -1;

	if (vp1 == NULL && vp2 == NULL)
		return 0;

	if (vp1 == NULL && vp2 != NULL)
		return 1;

	switch(obj_type)
	{
		case SOBJ_NODE:
			n1 = (node_info **) vp1;
			n2 = (node_info **) vp2;
			v1 = find_node_amount(*n1, si.res_name, si.def, si.res_type);
			v2 = find_node_amount(*n2, si.res_name, si.def, si.res_type);
			rank1 = (*n1)->rank;
			rank2 = (*n2)->rank;
			break;
		case SOBJ_PARTITION:
			np1 = (node_partition **) vp1;
			np2 = (node_partition **) vp2;
			v1 = find_nodepart_amount(*np1, si.res_name, si.def, si.res_type);
			v2 = find_nodepart_amount(*np2, si.res_name, si.def, si.res_type);
			rank1 = (*np1)->rank;
			rank2 = (*np2)->rank;
			break;
		case SOBJ_BUCKET:
			b1 = (node_bucket **) vp1;
			b2 = (node_bucket **) vp2;
			v1 = find_bucket_amount(*b1, si.res_name, si.def, si.res_type);
			v2 = find_bucket_amount(*b2, si.res_name, si.def, si.res_type);
			rank1 = 0;
			rank2 = 0;
			break;

		default:
			return 0;
			break;
	}

	if (v1 == v2)
		return 0;

	if (si.order == ASC) {
		if (v1 < v2)
			return -1;
		else if (v1 > v2)
			return 1;
		else {
			if (rank1 < rank2)
				return -1;
			else
				return 1;
		}
	} else {
		if (v1 < v2)
			return 1;
		else if (v1 > v2)
			return -1;
		else {
			if (rank1 < rank2)
				return 1;
			else
				return -1;
		}
	}
}

/**
 * @brief
 * 		entrypoint into job sort used by qsort
 *
 *		1. Sort all preemption priority jobs in the front
 *		2. Sort all preempted jobs in ascending order of their preemption time
 *		3. Sort jobs according to their fairshare usage.
 *		4. sort by unique rank to stabilize the sort
 *
 * @param[in]	v1	-	resource_resv 1
 * @param[in]	v2	-	resource_resv 2
 *
 * @return	-1,0,1 : based on sorting function.
 */
int
cmp_sort(const void *v1, const void *v2)
{
	int cmp;
	resource_resv *r1;
	resource_resv *r2;

	r1 = *((resource_resv **) v1);
	r2 = *((resource_resv **) v2);

	if (r1 != NULL && r2 == NULL)
		return -1;

	if (r1 == NULL && r2 == NULL)
		return 0;

	if (r1 == NULL && r2 != NULL)
		return 1;

	if (in_runnable_state(r1) && !in_runnable_state(r2))
		return -1;
	else if (in_runnable_state(r2) && !in_runnable_state(r1))
		return 1;
	/* both jobs are runnable */
	else {
		/* sort based on preemption */
		cmp = cmp_preemption(r1, r2);
		if (cmp != 0)
			return cmp;

		cmp = cmp_job_preemption_time_asc(&r1, &r2);
		if (cmp != 0)
			return cmp;

		/* sort on the basis of job sort formula */
		cmp = cmp_job_sort_formula(&r1, &r2);
		if (cmp != 0)
			return cmp;
#ifndef NAS /* localmod 041 */
		if (r1->server->policy->fair_share) {
			cmp = cmp_fairshare(&r1, &r2);
			if (cmp != 0)
				return cmp;
		}
#endif /* localmod 041 */

		/* normal resource based sort */
		cmp = multi_sort(r1, r2);
		if (cmp != 0)
			return cmp;

		/* stabilize the sort */
		else {
			if (r1->qrank < r2->qrank)
				return -1;
			else if (r1->qrank > r2->qrank)
				return 1;
			if (r1->rank < r2->rank)
				return -1;
			else if (r1->rank > r2->rank)
				return 1;
			else {
				return 0;
			}
		}
	}
}
/**
 * @brief
 * 		return resource values based on res_type for node partition
 *
 * @param[in] np 		- node partition
 * @param[in] res 		- resource name
 * @param[in] def 		- resource definition of res
 * @param[in] res_type 	- type of resource value to use
 *
 * @note
 * 		special case sorting "resource" SORT_PRIORITY is not meaningful for
 *       	node partitions.  0 will always be returned
 *
 * @return	sch_resource_t
 */
sch_resource_t
find_nodepart_amount(node_partition *np, const std::string& res, resdef *def,
	enum resource_fields res_type)
{
	schd_resource *nres;

	if (def != NULL)
		nres = find_resource(np->res, def);
	else
		nres = find_resource_by_str(np->res, res);

	if (nres != NULL) {
		if (res_type == RF_AVAIL)
			return nres->avail;
		else if (res_type == RF_ASSN)
			return nres->assigned;
		else if (res_type == RF_UNUSED)
			return nres->avail - nres->assigned;
		else/* error */
			return 0;
	}

	return 0;
}

/**
 * @brief
 * 		return resource values based on res_type for node bucket
 *
 * @param[in] bkt 		- node bucket
 * @param[in] res 		- resource name
 * @param[in] def 		- resource definition of res
 * @param[in] res_type 	- type of resource value to use
 *
 * @return	sch_resource_t
 */
sch_resource_t
find_bucket_amount(node_bucket *bkt, const std::string& res, resdef *def, enum resource_fields res_type)
{
	schd_resource *nres;

	if (def != NULL)
		nres = find_resource(bkt->res_spec, def);
	else if (res == SORT_PRIORITY)
		return bkt->priority;
	else
		nres = find_resource_by_str(bkt->res_spec, res);

	if (nres != NULL) {
		if (res_type == RF_AVAIL)
			return nres->avail;
		else if (res_type == RF_ASSN)
			return nres->assigned;
		else if (res_type == RF_UNUSED)
			return nres->avail - nres->assigned;
		else /* error */
			return 0;
	}

	return 0;
}

/**
 * @brief
 * 		return resource values based on res_type for a node
 *
 * @param[in] ninfo 	- node
 * @param[in] res 		- resource name or special case
 * @param[in] def 		- resource definition of res
 * @param[in] res_type 	- type of resource value to use
 *  *
 * @return sch_resource_t
 * @retval	0	: error
 */
sch_resource_t
find_node_amount(node_info *ninfo, const std::string& res, resdef *def,
	enum resource_fields res_type)
{
	/* def is NULL on special case sort keys */
	if(def != NULL) {
		schd_resource *nres;
		nres = find_resource(ninfo->res, def);

		if (nres != NULL) {
			if(nres->indirect_res != NULL)
				nres = nres -> indirect_res;
			if (res_type == RF_AVAIL)
				return nres->avail;
			else if (res_type == RF_ASSN)
				return nres->assigned;
			else if (res_type == RF_UNUSED)
				return nres->avail - nres->assigned;
			else /* error */
				return 0;
		}

	} else if (res == SORT_PRIORITY)
		return ninfo->priority;
	else if (res == SORT_USED_TIME)
		return ninfo->last_used_time;

	return 0;
}

/**
 * @brief
 * 		find resource or special case sorting values for jobs
 *
 * @param[in] resresv 	- the job
 * @param[in] res 		- the resource/special case name
 * @param[in] def 		- the resource definition of res (NULL for special case)
 *
 * @return	sch_resource_t
 * @retval	0	: on error
 */
sch_resource_t
find_resresv_amount(resource_resv *resresv, const std::string& res, resdef *def)
{
	/* def is NULL on special case sort keys */
	if( def != NULL) {
		resource_req *req;

		req = find_resource_req(resresv->resreq, def);

		if (req != NULL)
			return req->amount;
	}

	if (res == SORT_JOB_PRIORITY)
#ifdef NAS /* localmod 045 */
		return(sch_resource_t) resresv->job->NAS_pri;
#else
		return(sch_resource_t) resresv->job->priority;
#endif /* localmod 045 */
	else if (res == SORT_FAIR_SHARE && resresv->job->ginfo != NULL)
		return(sch_resource_t) resresv->job->ginfo->tree_percentage;
	else if (res == SORT_PREEMPT)
		return(sch_resource_t) resresv->job->preempt;
#ifdef NAS
		/* localmod 034 */
	else if (res == SORT_ALLOC)
		return(sch_resource_t) (100.0 * site_get_share(resresv));
		/* localmod 039 */
	else if (res == SORT_QPRI && resresv->job->queue != NULL)
		return(sch_resource_t) resresv->job->queue->priority;
		/* localmod 040 */
	else if (res == SORT_NODECT) {
		/* return the node count - dpr */
		int ndct = resresv->job->nodect;
		return(sch_resource_t) ndct;
	}
#endif
	return 0;
}

/**
 * @brief
 * 	 	sort nodes by resources_available.host
 *
 * @param[in]	v1	-	node_info 1
 * @param[in]	v2	-	node_info 2
 *
 * @return int
 * @retval -1, 0, 1 - standard qsort() cmp
*/
int
cmp_node_host(const void *v1, const void *v2)
{
	schd_resource *res1;
	schd_resource *res2;
	node_info **n1;
	node_info **n2;
	int rc = 0;

	n1 = (node_info **) v1;
	n2 = (node_info **) v2;

	res1 = find_resource((*n1)->res, allres["host"]);
	res2 = find_resource((*n2)->res, allres["host"]);

	if (res1 != NULL && res2 != NULL)
		rc = strcmp(res1->orig_str_avail, res2->orig_str_avail);

	/* if the host is the same and we have a node_sort_key, preserve the sort */
	if (rc == 0 && !cstat.node_sort->empty())
		return multi_node_sort(v1, v2);

	return rc;
}

/**
 * @brief
 *		Sorting function used with 'avoid_provision' policy
 *
 * @par Functionality:
 *		This function compares two nodes and determines the order by comparing
 *		the AOE instantiated on them with AOE requested by job (or reservation).
 *		If order cannot be determined then node_sort_key is used.
 *
 * @param[in]	v1		-	pointer to const void
 * @param[in]	v2		-	pointer to const void
 *
 * @return	int
 * @retval	 -1 : v1 has precendence
 * @retval	  0 : v1 and v2 both have equal precedence
 * @retval	  1 : v2 has precendence
 *
 * @par Side Effects:
 *		Unknown
 *
 * @par MT-safe: No
 *
 */
int
cmp_aoe(const void *v1, const void *v2)
{
	node_info **n1;
	node_info **n2;
	int v1rank = 0, v2rank = 0; /* to reduce strcmp() calls */
	int ret;

	n1 = (node_info **) v1;
	n2 = (node_info **) v2;

	/* Between nodes, one with aoe and other without aoe, one with aoe
	 * comes first.
	 */

	if ((*n1)->current_aoe) {
		if (strcmp((*n1)->current_aoe, cmp_aoename) == 0)
			v1rank = 1;
		else
			v1rank = -1;
	}

	if ((*n2)->current_aoe) {
		if (strcmp((*n2)->current_aoe, cmp_aoename) == 0)
			v2rank = 1;
		else
			v2rank = -1;
	}

	ret = v2rank - v1rank;

	if (ret == 0)
		return multi_node_sort(v1, v2);

	return ret;
}

/**
 * @brief
 * 		A function which sorts the jobs according to the time they are
 *        preempted (in ascending order)
 *
 * @param[in] j1 - job to compare.
 * @param[in] j2 - job to compare.
 *
 * @return	int
 * @retval 	-1 : If j1 was preempted before j2.
 * @retval  0  : If both were preempted at the same time
 * @retval  1  : if j2 was preempted before j1.
 */
int
cmp_job_preemption_time_asc(const void *j1, const void *j2)
{
	resource_resv *r1;
	resource_resv *r2;

	r1 = *((resource_resv **) j1);
	r2 = *((resource_resv **) j2);

	if (r1 == NULL && r2 == NULL)
		return 0;

	if (r1 != NULL && r2 == NULL)
		return -1;

	if (r1 == NULL && r2 != NULL)
		return 1;

	if (r1->job == NULL || r2->job ==NULL)
		return 0;

	/* If one job is preempted and second is not then preempted job gets priority
	 * If both jobs are preempted, one which is preempted first gets priority
	 */
	if (r1->job->time_preempted == UNSPECIFIED &&
		r2->job->time_preempted ==UNSPECIFIED)
		return 0;
	else if (r1->job->time_preempted != UNSPECIFIED &&
		r2->job->time_preempted ==UNSPECIFIED)
		return -1;
	else if (r1->job->time_preempted == UNSPECIFIED &&
		r2->job->time_preempted !=UNSPECIFIED)
		return 1;

	if (r1->job->time_preempted < r2->job->time_preempted)
		return -1;
	else if (r1->job->time_preempted > r2->job->time_preempted)
		return 1;
	return 0;
}

/**
 * @brief
 * cmp_resv_state	- compare reservation state with RESV_BEING_ALTERED.
 *
 * @param[in] r1	- reservation to compare.
 * @param[in] r2	- reservation to compare.
 *
 * @return - int
 * @retval  1: If r2's state is RESV_BEING_ALTERED and r1's state is not.
 *          0: If both the reservation's states are not RESV_BEING_ALTERED.
 *         -1: If r1's state is RESV_BEING_ALTERED and r2's state is not.
 */
int
cmp_resv_state(const void *r1, const void *r2)
{
	enum resv_states resv1_state;
	enum resv_states resv2_state;

	resv1_state = (*(resource_resv **)r1)->resv->resv_state;
	resv2_state = (*(resource_resv **)r2)->resv->resv_state;

	if (resv1_state != RESV_BEING_ALTERED && resv2_state == RESV_BEING_ALTERED)
		return 1;
	if (resv2_state != RESV_BEING_ALTERED && resv1_state == RESV_BEING_ALTERED)
		return -1;
	else
		return 0;
}

/**
 * @brief
 * 		sort_jobs - This function sorts all jobs according to their preemption
 *      priority, preempted time and fairshare.
 *		sort_jobs is called whenever we need to sort jobs on the basis of
 *		various policies set in scheduler.
 * @param[in]		policy	-	Policy structure to decide whether to sort for fairshare or
 *                     			not. If yes, then according should it be according to
 *                     			by_queue or round_robin.
 * @param[in,out]	sinfo 	- 	Server info struct which contains all the jobs that needs
 *                        		sorting.
 * @return	void
 */
void
sort_jobs(status *policy, server_info *sinfo)
{
	/** sort jobs in such a way that Higher Priority jobs come on top
	 * followed by preempted jobs and then normal jobs
	 */
	if (policy->fair_share) {
		/** sort per queue basis and then use these jobs (combined from all the queues)
		 * to select the next job.
		 */
		if (policy->by_queue || policy->round_robin) {
			int job_index = 0;
			/* cycle through queues and sort them on the basis of preemption priority,
			 * preempted jobs, and fairshare usage
			 */
			for (auto qinfo: sinfo->queues) {
				if (qinfo->sc.total > 0) {
					qsort(qinfo->jobs, qinfo->sc.total,
						sizeof(resource_resv*), cmp_sort);
				}
			}
			for (auto qinfo: sinfo->queues) {
				for (int index = 0; index < qinfo->sc.total; index++) {
					sinfo->jobs[job_index] = qinfo->jobs[index];
					job_index++;
				}
			}
			sinfo->jobs[job_index] = NULL;
		}
		/** Sort on entire complex **/
		else if (!policy->by_queue && !policy->round_robin) {
			qsort(sinfo->jobs, count_array(sinfo->jobs), sizeof(resource_resv *), cmp_sort);
		}
	}
	else if (policy->by_queue) {
		for (auto qinfo: sinfo->queues) {
			qsort(qinfo->jobs, count_array(qinfo->jobs), sizeof(resource_resv *), cmp_sort);
		}
		qsort(sinfo->jobs, count_array(sinfo->jobs), sizeof(resource_resv*), cmp_sort);
	}
	else if (policy->round_robin) {
		if (sinfo -> queue_list != NULL) {
			int queue_list_size = count_array(sinfo->queue_list);
			for (int i = 0; i < queue_list_size; i++)
			{
				int queue_index_size = count_array(sinfo->queue_list[i]);
				for (int j = 0; j < queue_index_size; j++)
				{
					qsort(sinfo->queue_list[i][j]->jobs, count_array(sinfo->queue_list[i][j]->jobs),
						sizeof(resource_resv *), cmp_sort);
				}
			}

		}
	}
	else
		qsort(sinfo->jobs, count_array(sinfo->jobs), sizeof(resource_resv*), cmp_sort);
}
