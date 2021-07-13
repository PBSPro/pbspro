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

#ifndef	_JOB_INFO_H
#define	_JOB_INFO_H

#include <pbs_ifl.h>
#include "data_types.h"

/*
 *	query_job - takes info from a batch_status about a job and puts
 */
resource_resv *query_job(int pbs_sd, struct batch_status *job, server_info *sinfo, queue_info *qinfo, schd_error *err);

/*
 * pthread routine for querying a chunk of jobs
 */
void query_jobs_chunk(th_data_query_jinfo *data);

/* create an array of jobs for a particular queue */
resource_resv **query_jobs(status *policy, int pbs_sd, queue_info *qinfo, resource_resv **pjobs, const std::string& queue_name);


/*
 *	new_job_info  - allocate and initialize new job_info structure
 */
#ifdef NAS /* localmod 005 */
job_info *new_job_info(void);
#else
job_info *new_job_info();
#endif /* localmod 005 */

/*
 *	free_job_info - free all the memory used by a job_info structure
 */

void free_job_info(job_info *jinfo);

/*
 *      set_job_state - set the state flag in a job_info structure
 *                      i.e. the is_* bit
 */
int set_job_state(const char *state, job_info *jinfo);

/* update_job_attr - update job attributes on the server */
int
update_job_attr(int pbs_sd, resource_resv *resresv, const char *attr_name,
	const char *attr_resc, const char *attr_value, struct attrl *extra, unsigned int flags );

/* send delayed job attribute updates for job using send_attr_updates() */
int send_job_updates(int pbs_sd, resource_resv *job);

/* send delayed attributes to the server for a job */
int send_attr_updates(int virtual_fd, resource_resv *resresv, struct attrl *pattr);

preempt_job_info *send_preempt_jobs(int virtual_sd, char **preempt_jobs_list);

int send_sigjob(int virtual_sd, resource_resv *resresv, const char *signal, char *extend);

struct batch_status *send_selstat(int virtual_fd, struct attropl *attrib, struct attrl *rattrib, char *extend);


/*
 *
 *      unset_job_attr - unset job attributes on the server
 *
 *	  pbs_sd     - connection to the pbs_server
 *	  resresv    - job to update
 *	  attr_name  - the name of the attribute to unset
 *	  flags - UPDATE_NOW - call send_attr_updates() to update the attribute now
 *		  UPDATE_LATER - attach attribute change to job to be sent all at once
 *				for the job.  NOTE: Only the jobs that are part
 *				of the server in main_sched_loop() will be updated in this way.
 *
 *      returns
 *              1: attribute was unset
 *              0: no attribute was unset
 *
 */
int
unset_job_attr(int pbs_sd, resource_resv *resresv, const char *attr_name, unsigned int flags);

/*
 *      update_jobs_cant_run - update an array of jobs which can not run
 */
void
update_jobs_cant_run(int pbs_sd, resource_resv **jinfo_arr,
	resource_resv *start, struct schd_error *err, int start_where);

/*
 *	update_job_comment - update a job's comment attribute.  If the job's
 *			     comment attr is identical, don't update
 *
 *	  pbs_sd - pbs connection descriptor
 *	  resresv - the job to update
 *	  comment - the comment string
 *
 *	returns 1 if the comment was updated
 *		0 if not
 */
int update_job_comment(int pbs_sd, resource_resv *resresv, char *comment);

/*
 *	translate_fail_code - translate Scheduler failure code into
 *				a comment and log message
 */
int translate_fail_code(schd_error *err, char *comment_msg, char *log_msg);

/*
 *      preempt_job - preempt a job to allow another job to run.  First the
 *                    job will try to be suspended, then checkpointed and
 *                    finally forcablly requeued
 */
int preempt_job(status *policy, int pbs_sd, resource_resv *jinfo, server_info *sinfo);

/*
 *      find_and_preempt_jobs - find the jobs to preempt and then preempt them
 */
int find_and_preempt_jobs(status *policy, int pbs_sd, resource_resv *hjinfo, server_info *sinfo, schd_error *err);

/*
 *      find_jobs_to_preempt - find jobs to preempt in order to run a high
 *                             priority job
 */
int *
find_jobs_to_preempt(status *policy, resource_resv *jinfo,
	server_info *sinfo, int *fail_list, int *count);

/*
 *      select_job_to_preempt - select the best candidite out of the running
 *                              jobs to preempt
 */
long
select_index_to_preempt(status *policy, resource_resv *hjob,
	resource_resv **rjobs, long skipto, schd_error *err,
	int *fail_list);

/*
 *      preempt_level - take a preemption priority and return a preemption
 *                      level
 */
int preempt_level(unsigned int prio);

/*
 *      set_preempt_prio - set a job's preempt field to the correct value
 */
void set_preempt_prio(resource_resv *job, queue_info *qinfo, server_info *sinfo);

/*
 * create subjob name from subjob id and array name
 *
 *	array_id - the parent array name
 *	index    - subjob index
 *
 * return created subjob name
 */
std::string create_subjob_name(const std::string& array_id, int index);

/*
 *	create_subjob_from_array - create a resource_resv structure for a subjob
 *				   from a job array structure.  The subjob
 *				   will be in state 'Q'
 */
resource_resv *
create_subjob_from_array(resource_resv *array, int index, const std::string& subjob_name);

/*
 *	update_array_on_run - update a job array object when a subjob is run
 */
int update_array_on_run(job_info *array, job_info *subjob);
/*
 *	dup_job_info - duplicate the information in a job_info structure
 */
job_info *dup_job_info(job_info *ojinfo, queue_info *nqinfo, server_info * nsinfo);

/*
 *	is_job_array - is a job name a job array range
 *			  valid_form: 1234[]
 *			  valid_form: 1234[N]
 *			  valid_form: 1234[N-M]
 *
 *	returns 1 if jobname is a job array
 *		2 if jobname is a subjob
 *		3 if jobname is a range
 *		0 if it is not a job array
 */
int is_job_array(char *jobid);

/*
 *
 *	modify_job_array_for_qrun - modify a job array for qrun -
 * 				    set queued_subjobs to just the
 *				    range which is being run
 *				    set qrun_job on server
 */
int modify_job_array_for_qrun(server_info *sinfo, char *jobid);

/*
 *	queue_subjob - create a subjob from a job array and queue it
 *
 *	  array - job array to create the next subjob from
 *	  sinfo - the server the job array is in
 *	  qinfo - the queue the job array is in
 *
 *	returns new subjob or NULL on error
 *	NOTE: subjob will be attached to the server/queue job lists
 */
resource_resv *
queue_subjob(resource_resv *array, server_info *sinfo,
	queue_info *qinfo);
/*
 *	formula_evaluate - evaluate a math formula for jobs based on their resources
 *		NOTE: currently done through embedded python interpreter
 */

sch_resource_t formula_evaluate(const char *formula, resource_resv *resresv, resource_req *resreq);

/*
 *
 *      update_accruetype - Updates accrue_type of job on server.
 *                          The accrue_type is determined from the values
 *                          of err_code and mode. if resresv is a job
 *                          array, special action is taken.
 *
 *        pbs_sd   - connection to pbs_server
 *        sinfo    - pointer to server
 *        mode     - mode of operation
 *        err_code - error code to evaluate
 *        resresv  - pointer to job
 *
 */

void update_accruetype(int pbs_sd, server_info *sinfo, enum update_accruetype_mode mode, enum sched_error_code err_code, resource_resv *resresv);

/**
 * @brief
 *	return aoe from select spec
 *
 * @param[in]	select - select spec of job/reservation
 *
 * @return	char*
 * @retval	NULL - no aoe found or failure encountered
 * @retval	aoe name string
 */
char * getaoename(selspec *select);

/**
 *  * @brief
 *	return eoe from select spec
 *
 * @param[in]	select - select spec of job/reservation
 *
 * @return	char*
 * @retval	NULL - no eoe found or failure encountered
 * @retval	eoe name string
 */
char * geteoename(selspec *select);

/*
 *
 *	update_estimated_attrs - updated the estimated.start_time and
 *				 estimated.exec_vnode attributes on a job
 *
 *	  \param pbs_sd     - connection descriptor to pbs server
 *	  \param job        - job to update
 *	  \param start_time - start time of job
 *	  \param exec_vnode - exec_vnode of job or NULL to create it from
 *			      job -> nspec_arr
 *
 *	\return 1 if attributes were successfully updated 0 if not
 */
int
update_estimated_attrs(int pbs_sd, resource_resv *job,
	time_t start_time, char *exec_vnode, int force);

/*
 *
 *	check_preempt_targets_for_none - This function checks if preemption set has been configured as "NONE"
 *					If its found that resources_default.preempt_targets = NONE
 *					then this function returns PREEMPT_NONE.
 *	res_list - list of resources created from comma seperated resource list.
 *
 *	return - int
 *	retval - PREEMPT_NONE :If preemption set is set to "NONE"
 *	retval - 0 :If preemption set is not set as "NONE"
 */
int check_preempt_targets_for_none(char ** res_list);

/*
 *
 *  @brief checks whether the IFL interface failed because it was a finished job
 *
 *  @param[in] error - pbs_errno set by server
 *
 *  @retval 1 if job is a finished job
 *  @retval 0 if job is not a finished job
 */
int is_finished_job(int error);

/*
 * compare two jobs to see if they overlap using a complete err list as
 * criteria similarity criteria.
 */
int preemption_similarity(resource_resv *hjob, resource_resv *pjob, schd_error *full_err);

/* Equivalence class functions*/
resresv_set *new_resresv_set(void);
void free_resresv_set(resresv_set *rset);
void free_resresv_set_array(resresv_set **rsets);
resresv_set *dup_resresv_set(resresv_set *oset, server_info *nsinfo);
resresv_set **dup_resresv_set_array(resresv_set **osets, server_info *nsinfo);

/* create a resresv_set with a resresv as a template */
resresv_set *create_resresv_set_by_resresv(status *policy, server_info *sinfo, resource_resv *resresv);

/* find a resresv_set by its internal components */
int find_resresv_set(status *policy, resresv_set **rsets, char *user, char *group, char *project, selspec *sel, place *pl, resource_req *req, queue_info *qinfo);

/* find a resresv_set with a resresv as a template */
int find_resresv_set_by_resresv(status *policy, resresv_set **rsets, resource_resv *resresv);

/* create the array of resdef's to use to create resresv->req*/
std::unordered_set<resdef *> create_resresv_sets_resdef(status *policy);

/* Create an array of resresv_sets based on sinfo*/
resresv_set **create_resresv_sets(status *policy, server_info *sinfo);
/*
 * This function creates a string and update resources_released job
 *  attribute.
 *  The string created will be similar to how exec_vnode is presented
 *  example: (node1:ncpus=8)+(node2:ncpus=8)
 */
void create_res_released(status *policy, resource_resv *pjob);

/*
 *This function populates resreleased job structure for a particular job.
 */
nspec **create_res_released_array(status *policy, resource_resv *resresv);

/*
 * @brief create a resource_rel array for a job by accumulating all of the RASSN
 *	    resources in a resources_released nspec array.
 */
resource_req *create_resreq_rel_list(status *policy, resource_resv *pjob);

/* Returns the extended duration of a job that has exceeded its soft_walltime */
long extend_soft_walltime(resource_resv *resresv, time_t server_time);

/* Returns a list of preemptable candidates */
resource_resv **filter_preemptable_jobs(resource_resv **arr, resource_resv *job, schd_error *err);

/*
 * This function processes every job's depend attribute and
 * associate the jobs with runone dependency to its dependent_jobs list.
 */
void associate_dependent_jobs(server_info *sinfo);

/* This function associated the job passed in to its parent job */
int associate_array_parent(resource_resv *pjob, server_info *sinfo);

/* Set start, end, duration, and possibly STF parts of the job */
void set_job_times(int pbs_sd, resource_resv *reseresv, time_t server_time);

#endif	/* _JOB_INFO_H */
