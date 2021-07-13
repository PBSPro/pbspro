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

#ifndef	_SERVER_INFO_H
#define	_SERVER_INFO_H

#include <pbs_ifl.h>
#include "state_count.h"
#include "data_types.h"
#include "constant.h"

/* Modes passed to update_total_counts_on_run() */
enum counts_on_run {
	SERVER,
	QUEUE,
	ALL
};
/*
 *      query_server - creates a structure of arrays consisting of a server
 *                      and all the queues and jobs that reside in that server
 */
server_info *query_server(status *policy, int pbs_sd);

/*
 *	query_server_info - collect information out of a statserver call
 *			    into a server_info structure
 */
server_info *query_server_info(status *policy, struct batch_status *server);

/*
 * 	query_server_dyn_res - execute all configured server_dyn_res scripts
 */
int query_server_dyn_res(server_info *sinfo);

/*
 *	find_alloc_resource[_by_str] - try and find a resource, and if it is
 *                                     not there allocate space for it and
 *                                     add it to the resource list
 */

schd_resource *find_alloc_resource(schd_resource *resplist, resdef *def);
schd_resource *find_alloc_resource_by_str(schd_resource *resplist, const char *name);
schd_resource *find_alloc_resource_by_str(schd_resource *resplist, const std::string& name);

/*  finds a resource in a resource list by string resource name */

schd_resource *find_resource_by_str(schd_resource *reslist, const char *name);
schd_resource *find_resource_by_str(schd_resource *reslist, const std::string& name);

/*
 *	find resource by resource definition
 */
schd_resource *find_resource(schd_resource *reslist, resdef *def);

/*
 *      free_resource - free a resource struct
 */
void free_resource(schd_resource *res);

/*
 *      free_resource_list - free a resource list
 */
void free_resource_list(schd_resource *res_list);

/*
 *      new_resource - allocate and initialize new resoruce struct
 */
schd_resource *new_resource(void);

/*
 * Create new resource with given data
 */
schd_resource *create_resource(const char *name, const char *value, enum resource_fields field);

/*
 *	free_server - free a list of server_info structs
 */
void free_server(server_info *sinfo);

/*
 *      update_server_on_run - update server_info strucutre when a job is run
 */
void
update_server_on_run(status *policy, server_info *sinfo, queue_info *qinfo,
	resource_resv *resresv, char *job_state);

/*
 *
 *      create_server_arrays - create a large server resresv array of all the
 *                             jobs on the system by coping all the jobs
 *                             from the queue job arrays.  Also create an array
 *                             of both jobs and reservations
 */
int create_server_arrays(server_info *sinfo);

/*
 *	copy_server_arrays - copy server's jobs and all_resresv arrays
 */
int copy_server_arrays(server_info *nsinfo, const server_info *osinfo);


/*
 *      check_exit_job - function used by job_filter to filter out
 *                       jobs not in the exiting state
 */
int check_exit_job(resource_resv *job, const void *arg);

/*
 *
 *	check_susp_job - function used by job_filter to filter out jobs
 *			   which are suspended
 */
int check_susp_job(resource_resv *job, const void *arg);

/*
 *
 *	check_job_running - function used by job_filter to filter out
 *			   jobs that are running
 */
int check_job_running(resource_resv *job, const void *arg);

/*
 *
 *	check_running_job_in_reservation - function used by job_filter to filter out
 *			   jobs that are in a reservation
 */
int check_running_job_in_reservation(resource_resv *job, const void *arg);

/*
 *
 *	check_running_job_not_in_reservation - function used by job_filter to filter out
 *			   jobs that are not in a reservation
 */
int check_running_job_not_in_reservation(resource_resv *job, const void *arg);

/*
 *
 *      check_resv_running_on_node - function used by resv_filter to filter out
 *				running reservations
 */
int check_resv_running_on_node(resource_resv *resv, const void *arg);

/*
 *      dup_resource_list - dup a resource list
 */
schd_resource *dup_resource_list(schd_resource *res);

/* dup a resource list selectively only duping specific resources */

schd_resource *dup_selective_resource_list(schd_resource *res, std::unordered_set<resdef*>& deflist, unsigned flags);

/*
 *	dup_ind_resource_list - dup a resource list - if a resource is indirect
 *				dup the pointed to resource instead
 */
schd_resource *dup_ind_resource_list(schd_resource *res);

/*
 *      dup_resource - duplicate a resource struct
 */
schd_resource *dup_resource(schd_resource *res);

/*
 *      check_resv_job - finds if a job has a reservation
 *                       used with job_filter
 */
int check_resv_job(resource_resv *job, void *unused);

/*
 *      free_resource_list - frees the memory used by a resource list
 */
void free_resource_list(schd_resource *reslist);


/*
 *      free_resource - frees the memory used by a resource structure
 */
void free_resource(schd_resource *resp);

/*
 *      update_server_on_end - update a server structure when a job has
 *                             finished running
 */
void
update_server_on_end(status *policy, server_info *sinfo, queue_info *qinfo,
	resource_resv *resresv, const char *job_state);

/*
 *      check_unassoc_node - finds nodes which are not associated with queues
 *                           used with node_filter
 */
int is_unassoc_node(node_info *ninfo, void *arg);

/*
 *      new_counts - create a new counts structure and return it
 */
counts *new_counts(void);

/*
 *      free_counts_list - free a list of counts structures
 */
void free_counts_list(counts_umap &ctslist);

/*
 *	dup_counts_umap - duplicate counts_umap
 */
counts_umap dup_counts_umap (const counts_umap &omap);

/*
 *      find_counts - find a counts structure by name
 */
counts *find_counts(counts_umap &ctslist, const std::string &name);

/*
 *      find_alloc_counts - find a counts structure by name or allocate a new
 *                          counts, name it, and add it to the end of the list
 */
counts *find_alloc_counts(counts_umap &ctslist, const std::string &name);

/*
 *      update_counts_on_run - update a counts struct on the running of a job
 */
void update_counts_on_run(counts *cts, resource_req *resreq);

/*
 *      update_counts_on_end - update a counts structure on the end of a job
 */
void update_counts_on_end(counts *cts, resource_req *resreq);

/**
 *	counts_max - perform a max() the current max and a new list.  If any
 *			element from the new list is greater than the current
 *			max, we free the old, and dup the new and attach it
 *			in.
 *
 *	  \param cmax    - current max that will be updated.
 *	  \param new     - new counts lists.  If anything in this list is
 *			   greater than the cur_max, it needs to be dup'd.
 *
 *	  returns void
 */
void counts_max(counts_umap &cmax, counts_umap &ncounts);
void counts_max(counts_umap &cmax, counts *ncounts);

/*
 *      check_run_job - function used by resource_resv_filter to filter out
 *                      non-running jobs.
 */
int check_run_job(resource_resv *job, const void *arg);

/*
 *      update_universe_on_end - update a pbs universe when a job/resv ends
 */
void update_universe_on_end(status *policy, resource_resv *resresv, const char *job_state, unsigned int flags);

/*
 *
 *	set_resource - set the values of the resource structure.  This
 *		function can be called in one of two ways.  It can be called
 *		with resources_available value, or the resources_assigned
 *		value.
 *
 *	NOTE: If we have resource type information from the server, we will
 * 		use it.  If not, we will try and set the resource type from
 * 		the resources_available value first, if not then the
 *		resources_assigned
 *
 *	res - the resource to set
 *	val - the value to set upon the resource
 *	field - the type of field to set (avaialble or assigned)
 *
 *	returns 1 on success 0 on failure/error
 *
 */
int set_resource(schd_resource *res, const char *val, enum resource_fields field);

/*
 *	update_preemption_priority - update preemption status when a
 *   					resource resv runs/ends
 *	returns nothing
 */
void update_preemption_priority(server_info *sinfo, resource_resv *resresv);

/*
 *	add_resource_list - add one resource list to another
 *			i.e. r1 += r2
 *	returns 1 on success
 *		0 on failure
 */
int add_resource_list(status *policy, schd_resource *r1, schd_resource *r2, unsigned int flags);

int modify_resource_list(schd_resource *res_list, resource_req *req_list, int type);

/*
 *	add_resource_value - add a resource value to another
 *				i.e. val1 += val2
 */
void add_resource_value(sch_resource_t *val1, sch_resource_t *val2,
			sch_resource_t default_val);

/*
 *  add_resource_string_arr - add values from a string array to
 *                             a string resource.  Only add values if
 *                             they do not exist unless specified by allow_dup
 */
int add_resource_str_arr(schd_resource *res, char **str_arr, int allow_dup);

/*
 *      accumulate two boolean resources together (r1 += r2)
 *        T + T = True | F + F = False | T + F = TRUE_FALSE
 */
int add_resource_bool(schd_resource *r1, schd_resource *r2);

/*
 *	find_indirect_resource - follow the indirect resource pointers to
 *				 find the real resource at the end
 *	returns the indirect resource or NULL on error
 */
schd_resource *find_indirect_resource(schd_resource *res, node_info **nodes);

/*
 *	resolve_indirect_resources - resource indirect resources for node array
 *
 *	  nodes - the nodes to resolve
 *
 *	returns 1 if successful
 *		0 if there were any errors
 */
int resolve_indirect_resources(node_info **nodes);

/*
 *	read_formula - read the formula from a well known file
 *
 *	returns formula in malloc'd buffer or NULL on error
 */
char *read_formula(void);

/*
 * create_total_counts -  Creates total counts list for server & queue
 */
void
create_total_counts(server_info *sinfo, queue_info * qinfo,
	resource_resv *resresv, int mode);

/*
 * Updates total counts list for server & queue on run and on
 * preemption.
 */
void
update_total_counts(server_info *si, queue_info* qi,
	resource_resv *rr, int mode);
void
update_total_counts_on_end(server_info *si, queue_info* qi,
	resource_resv *rr, int mode);

/**
 * @brief - get a unique rank to uniquely identify an object
 * @return int
 * @retval unique number for this scheduling cycle
 */
int get_sched_rank();

/*
 *  add_queue_to_list - This function aligns all queues according to
 *                      their priority so that we can round robin
 *                      across those.
 */
int add_queue_to_list(queue_info **** qlhead, queue_info * qinfo);

/*
 * append_to_queue_list - function that will reallocate and append
 *                        "add" to the list provided.
 */
struct queue_info **append_to_queue_list(queue_info ***list, queue_info *add);

/*
 * find_queue_list_by_priority - function finds out the array of queues
 *                               which matches with the priority passed to this
 *                               function. It returns the base address of matching
 *                               array.
 */
struct queue_info ***find_queue_list_by_priority(queue_info ***list_head, int priority);

/*
 * free_queue_list - to free two dimensional queue_list array
 */
void free_queue_list(queue_info *** queue_list);

void add_req_list_to_assn(schd_resource *, resource_req *);

int create_resource_assn_for_node(node_info *);

int compare_resource_avail_list(schd_resource *r1, schd_resource *r2);
int compare_resource_avail(schd_resource *r1, schd_resource *r2);

node_info **dup_unordered_nodes(node_info **old_unordered_nodes, node_info **nnodes);

status *dup_status(status *ost);

struct batch_status *send_statserver(int virtual_fd, struct attrl *attrib, char *extend);

#endif	/* _SERVER_INFO_H */
