/*
 * Copyright (C) 1994-2016 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *  
 * This file is part of the PBS Professional ("PBS Pro") software.
 * 
 * Open Source License Information:
 *  
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free 
 * Software Foundation, either version 3 of the License, or (at your option) any 
 * later version.
 *  
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *  
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 * Commercial License Information: 
 * 
 * The PBS Pro software is licensed under the terms of the GNU Affero General 
 * Public License agreement ("AGPL"), except where a separate commercial license 
 * agreement for PBS Pro version 14 or later has been executed in writing with Altair.
 *  
 * Altair’s dual-license business model allows companies, individuals, and 
 * organizations to create proprietary derivative works of PBS Pro and distribute 
 * them - whether embedded or bundled with other software - under a commercial 
 * license agreement.
 * 
 * Use of Altair’s trademarks, including but not limited to "PBS™", 
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's 
 * trademark licensing policies.
 *
 */

/**
 * @file    fifo.c
 *
 * @brief
 * 		fifo.c - This file contains functions related to FIFO scheduling.
 *
 * Functions included are:
 * 	schedinit()
 * 	update_cycle_status()
 * 	init_scheduling_cycle()
 * 	schedule()
 * 	intermediate_schedule()
 * 	scheduling_cycle()
 * 	main_sched_loop()
 * 	end_cycle_tasks()
 * 	update_last_running()
 * 	update_job_can_not_run()
 * 	run_job()
 * 	run_update_resresv()
 * 	sim_run_update_resresv()
 * 	should_backfill_with_job()
 * 	add_job_to_calendar()
 * 	find_ready_resv_job()
 * 	find_runnable_resresv()
 * 	find_non_normal_job()
 * 	find_susp_job()
 * 	scheduler_simulation_task()
 * 	next_job()
 * 	my_malloc()
 * 	my_realloc()
 */
#include <pbs_config.h>

#ifdef PYTHON
#include <Python.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <libutil.h>
#include <pbs_error.h>
#include <pbs_ifl.h>
#include <sched_cmds.h>
#include <time.h>
#include <log.h>
#include "data_types.h"
#include "fifo.h"
#include "queue_info.h"
#include "server_info.h"
#include "node_info.h"
#include "check.h"
#include "constant.h"
#include "job_info.h"
#include "misc.h"
#include "config.h"
#include "sort.h"
#include "parse.h"
#include "globals.h"
#include "prev_job_info.h"
#include "fairshare.h"
#include "prime.h"
#include "dedtime.h"
#include "resv_info.h"
#include "range.h"
#include "resource_resv.h"
#include "simulate.h"
#include "node_partition.h"
#include "resource.h"
#include "resource_resv.h"
#include "pbs_share.h"
#include "pbs_internal.h"
#include "limits_if.h"


#ifdef NAS
#include "site_code.h"
#endif

/* a list of running jobs from the last scheduling cycle */
static prev_job_info *last_running = NULL;
static int last_running_size = 0;

#ifdef WIN32
extern void win_toolong(void);
#endif

extern int	second_connection;
extern int	get_sched_cmd_noblk(int sock, int *val, char **jobid);

/**
 * @brief
 * 		initialize conf struct and parse conf files
 *
 * @param[in]	argc	-	passed in from main (may be 0)
 * @param[in]	argv	-	passed in from main (may be NULL)
 *
 * @return	Success/Failure
 * @retval	0	: success
 * @retval	!= 0	: failure
 */
int
schedinit(int argc, char *argv[])
{
	char zone_dir[MAXPATHLEN];
	struct tm *tmptr;

#ifdef PYTHON
	char errMsg[MAX_BUF_SIZE];
	char buf[MAXPATHLEN];
	char *errstr;
	
	PyObject *module;
	PyObject *obj;
	PyObject *dict;
	PyObject *path;
#endif 

	init_config();
	parse_config(CONFIG_FILE);

	parse_holidays(HOLIDAYS_FILE);
	time(&(cstat.current_time));

	if (is_prime_time(cstat.current_time))
		init_prime_time(&cstat, NULL);
	else
		init_non_prime_time(&cstat, NULL);

	tmptr = localtime(&cstat.current_time);
	if ((tmptr != NULL) && ((tmptr->tm_year + 1900) > conf.holiday_year))
		schdlog(PBSEVENT_ADMIN, PBS_EVENTCLASS_FILE, LOG_NOTICE,
			HOLIDAYS_FILE, "The holiday file is out of date; please update it.");

	parse_ded_file(DEDTIME_FILE);

	/* preload the static members to the fairshare tree */
	conf.fairshare = preload_tree();
	if (conf.fairshare != NULL) {
		parse_group(RESGROUP_FILE, conf.fairshare->root);
		calc_fair_share_perc(conf.fairshare->root->child, UNSPECIFIED);
		if (conf.prime_fs || conf.non_prime_fs) {
			read_usage(USAGE_FILE, 0, conf.fairshare);

			if (conf.fairshare->last_decay == 0)
				conf.fairshare->last_decay = cstat.current_time;
		}
	}
#ifdef NAS /* localmod 034 */
	site_parse_shares(SHARE_FILE);
#endif /* localmod 034 */

	/* initialize the iteration count */
	cstat.iteration = 0;

	/* set the zoneinfo directory to $PBS_EXEC/zoneinfo.
	 * This is used for standing reservations user of libical */
	sprintf(zone_dir, "%s%s", pbs_conf.pbs_exec_path, ICAL_ZONEINFO_DIR);
	set_ical_zoneinfo(zone_dir);

#ifdef PYTHON
	Py_NoSiteFlag = 1;
	Py_FrozenFlag = 1;
	Py_Initialize();

	path = PySys_GetObject("path");

	snprintf(buf, sizeof(buf), "%s/python/lib/python2.7", pbs_conf.pbs_exec_path);
	PyList_Append(path, PyString_FromString(buf));

	snprintf(buf, sizeof(buf), "%s/python/lib/python2.7/lib-dynload", pbs_conf.pbs_exec_path);
	PyList_Append(path, PyString_FromString(buf));

	PySys_SetObject("path", path);

	PyRun_SimpleString(
		"_err =\"\"\n"
		"ex = None\n"
		"try:\n"
			"\tfrom math import *\n"
		"except ImportError, ex:\n"
			"\t_err = str(ex)");
	
	module = PyImport_AddModule("__main__");
	dict = PyModule_GetDict(module);
	
	errstr = NULL;
	obj = PyMapping_GetItemString(dict, "_err");
	if (obj != NULL) {
		errstr = PyString_AsString(obj);
		if (errstr != NULL) {
			if (strlen(errstr) > 0) {
				snprintf(errMsg, sizeof(errMsg), " %s. Python is unlikely to work properly.", errstr);
				schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING,
					"PythonError", errMsg);
			}
		}
		Py_XDECREF(obj);
	}

#endif

	return 0;
}

/**
 * @brief
 *		update global status structure which holds
 *		      status information used by the scheduler which
 *		      can change from cycle to cycle
 *
 * @param[in]	policy	-	status structure to update
 * @param[in]	current_time	-	current time or 0 to call time()
 *
 * @return nothing
 *
 */
void
update_cycle_status(struct status *policy, time_t current_time)
{
	char dedtime;				/* boolean: is it dedtime? */
	enum prime_time prime;		/* current prime time status */
	struct tm *ptm;
	char logbuf[MAX_LOG_SIZE];
	char logbuf2[MAX_LOG_SIZE];
	struct tm *tmptr;

	if (policy == NULL)
		return;

	if (current_time == 0)
		time(&policy->current_time);
	else
		policy->current_time = current_time;

	/* cycle start in real time -- can be used for time deltas */
	policy->cycle_start = time(NULL);

	dedtime = is_ded_time(policy->current_time);

	/* it was dedtime last scheduling cycle, and it is not dedtime now */
	if (policy->is_ded_time && !dedtime) {
		/* since the current dedtime is now passed, set it to zero and sort it to
		 * the end of the dedtime array so the next dedtime is at the front
		 */
		conf.ded_time[0].from = 0;
		conf.ded_time[0].to = 0;
		qsort(conf.ded_time, MAX_DEDTIME_SIZE, sizeof(struct timegap), cmp_ded_time);
	}
	policy->is_ded_time = dedtime;

	/* if we have changed from prime to nonprime or nonprime to prime
	 * init the status respectively
	 */
	prime = is_prime_time(policy->current_time);
	if (prime == PRIME && !policy->is_prime)
		init_prime_time(policy, NULL);
	else if (prime == NON_PRIME && policy->is_prime)
		init_non_prime_time(policy, NULL);

	tmptr = localtime(&policy->current_time);
	if ((tmptr != NULL ) && ((tmptr->tm_year + 1900) > conf.holiday_year))
		schdlog(PBSEVENT_ADMIN, PBS_EVENTCLASS_FILE, LOG_NOTICE,
		       HOLIDAYS_FILE, "The holiday file is out of date; please update it.");
	policy->prime_status_end = end_prime_status(policy->current_time);

	if (policy->prime_status_end == SCHD_INFINITY)
		strcpy(logbuf2, "It will never end");
	else {
		ptm = localtime(&(policy->prime_status_end));
		if (ptm != NULL) {
			snprintf(logbuf2, sizeof(logbuf2),
				"It will end in %ld seconds at %02d/%02d/%04d %02d:%02d:%02d",
				policy->prime_status_end - policy->current_time,
				ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_year + 1900,
				ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
		}
		else
			strcpy(logbuf2, "It will end at <UNKNOWN>");
	}
	snprintf(logbuf, sizeof(logbuf), "It is %s.  %s",
		prime == PRIME ? "primetime" : "non-primetime", logbuf2);
	schdlog(PBSEVENT_DEBUG2, PBS_EVENTCLASS_SERVER, LOG_DEBUG, "", logbuf);

	policy->order = 0;
	policy->preempt_attempts = 0;
	policy->iteration++;
}

/**
 * @brief
 * 		prep the scheduling cycle.  Do tasks that have to happen prior
 *		 to the consideration of the first job.  This includes any
 *		 periodic upkeep (like fairshare), or any prep to the queried
 *		 data that needed to happen post query_server()
 *		 (like preemption)
 *
 * @param[in]	policy	-	policy info
 * @param[in]	sinfo	-	the server
 *
 * @return	int
 * @retval	1	: success
 * @retval	0	: failure
 *
 * @note
 * 		failure of this function will cause schedule() to exit
 */

int
init_scheduling_cycle(status *policy, server_info *sinfo)
{
	group_info *user = NULL; /* the user for the running jobs of the last cycle */
	char decayed = 0;	/* boolean: have we decayed usage? */
	time_t t;		/* used in decaying fair share */
	usage_t delta;	/* the usage between last sch cycle and now */
	struct group_path *gpath;	/* used to update usage with delta */
	int i, j;

	if (policy->fair_share && sinfo->fairshare !=NULL) {
		if (last_running != NULL) {
			/* add the usage which was accumulated between the last cycle and this
			 * one and calculate a new value
			 */

			for (i = 0; i < last_running_size ; i++) {
				user = find_alloc_ginfo(last_running[i].entity_name,
					sinfo->fairshare->root);

				for (j = 0; sinfo->running_jobs[j] != NULL &&
					strcmp(last_running[i].name, sinfo->running_jobs[j]->name); j++)
					;

				if (sinfo->running_jobs[j] != NULL &&
					sinfo->running_jobs[j]->job !=NULL) {
					/* just incase the delta is negative just add 0 */
					delta = formula_evaluate(conf.fairshare_res, sinfo->running_jobs[j], sinfo->running_jobs[j]->job->resused) -
						formula_evaluate(conf.fairshare_res, sinfo->running_jobs[j], last_running[i].resused);

					delta = IF_NEG_THEN_ZERO(delta);

					gpath = user->gpath;
					while (gpath != NULL) {
						gpath->ginfo->usage += delta;
						gpath = gpath->next;
					}
				}
			}
		}

		/* The half life for the fair share tree might have passed since the last
		 * scheduling cycle.  For that matter, several half lives could have
		 * passed.  If this is the case, perform as many decays as necessary
		 */

		t = policy->current_time;
		while (conf.decay_time != SCHD_INFINITY &&
			(t - sinfo->fairshare->last_decay) > conf.decay_time) {
			schdlog(PBSEVENT_DEBUG2, PBS_EVENTCLASS_SERVER, LOG_DEBUG,
				"Fairshare", "Decaying Fairshare Tree");
			if (conf.fairshare != NULL)
				decay_fairshare_tree(sinfo->fairshare->root);
			t -= conf.decay_time;
			decayed = 1;
		}

		if (decayed) {
			/* set the time to the actual time the half-life should have occurred */
			conf.fairshare->last_decay =
				policy->current_time - (policy->current_time -
				sinfo->fairshare->last_decay) % conf.decay_time;
		}

		if (policy->sync_fairshare_files && (decayed || last_running != NULL)) {
			write_usage(USAGE_FILE, sinfo->fairshare);
			schdlog(PBSEVENT_DEBUG2, PBS_EVENTCLASS_SERVER, LOG_DEBUG,
				"Fairshare", "Usage Sync");
		}
		reset_temp_usage(sinfo->fairshare->root);
	}

	/* set all the job's preempt priorities.  It is done here instead of when
	 * the jobs were created for several reasons.
	 * 1. fairshare usage is not updated
	 * 2. we need all the jobs to be created and up to date for soft run limits
	 */

	if (policy->preempting) {
		for (i = 0; sinfo->jobs[i] != NULL; i++) {
			if (sinfo->jobs[i]->job !=NULL)
				set_preempt_prio(sinfo->jobs[i],
					sinfo->jobs[i]->job->queue, sinfo);
		}
		/* now that we've set all the preempt levels, we need to count them */
		for (i = 0; sinfo->running_jobs[i] != NULL; i++) {
			if (sinfo->running_jobs[i]->job != NULL) {
				if (!sinfo->running_jobs[i]->job->can_not_preempt) {
					sinfo->preempt_count[preempt_level(sinfo->running_jobs[i]->job->preempt)]++;
				}
			}
		}

	}
	next_job(policy, sinfo, INITIALIZE);
#ifdef NAS /* localmod 034 */
	(void)site_pick_next_job(NULL);
	(void)site_is_share_king(policy);
#endif /* localmod 034 */

	return 1;		/* SUCCESS */
}

/**
 * @brief
 *		schedule - this function gets called to start each scheduling cycle
 *		   It will handle the difference cases that caused a
 *		   scheduling cycle
 *
 * @param[in]	cmd	-	reason for scheduling cycle
 * 						SCH_ERROR	: error
 *						SCH_SCHEDULE_NULL	: NULL command
 *						SCH_SCHEDULE_NEW	: A new job was submited
 *						SCH_SCHEDULE_TERM	: A job terminated
 *						SCH_SCHEDULE_TIME	: The scheduling interval expired
 *						SCH_SCHEDULE_RECYC	: A scheduling recycle(see admin guide)
 *						SCH_SCHEDULE_CMD	: The server scheduling variabe was set
 *												or reset to true
 *						SCH_SCHEDULE_FIRST	: the first cycle after the server starts
 *						SCH_CONFIGURE	: perform scheduling configuration
 *						SCH_QUIT	: have the scheduler quit
 *						SCH_RULESET	: reread the scheduler ruleset
 *						SCH_SCHEDULE_RESV_RECONFIRM	: reconfirm a reservation
 * @param[in]	sd	-	connection desctiptor to the pbs server
 * @param[in]	runjobid	-	job to run for a qrun request
 *
 * @return	int
 * @retval	0	: continue calling scheduling cycles
 * @retval	1	: exit scheduler
 */
int
schedule(int cmd, int sd, char *runjobid)
{
	switch (cmd) {
		case SCH_ERROR:
		case SCH_SCHEDULE_NULL:
		case SCH_RULESET:
			/* ignore and end cycle */
			break;


		case SCH_SCHEDULE_FIRST:
			/*
			 * on the first cycle after the server restarts custom resources
			 * may have been added.  Dump what we have so we'll requery them.
			 */
			reset_global_resource_ptrs();

		case SCH_SCHEDULE_NEW:
		case SCH_SCHEDULE_TERM:
		case SCH_SCHEDULE_CMD:
		case SCH_SCHEDULE_TIME:
		case SCH_SCHEDULE_JOBRESV:
		case SCH_SCHEDULE_STARTQ:
		case SCH_SCHEDULE_MVLOCAL:
		case SCH_SCHEDULE_ETE_ON:
		case SCH_SCHEDULE_RESV_RECONFIRM:
			return intermediate_schedule(sd, NULL);
		case SCH_SCHEDULE_AJOB:
			return intermediate_schedule(sd, runjobid);
		case SCH_CONFIGURE:
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_INFO,
				"reconfigure", "Scheduler is reconfiguring");
			if (conf.prime_fs || conf.non_prime_fs)
				write_usage(USAGE_FILE, conf.fairshare);
			free_fairshare_head(conf.fairshare);
			reset_global_resource_ptrs();
			free(conf.prime_sort);
			free(conf.non_prime_sort);
			if(schedinit(0, NULL) != 0)
				return 0;
			break;
		case SCH_QUIT:
			if (conf.prime_fs || conf.non_prime_fs)
				write_usage(USAGE_FILE, conf.fairshare);
#ifdef PYTHON
			Py_Finalize();
#endif
			return 1;		/* have the scheduler exit nicely */
		default:
			return 0;
	}
	return 0;
}

/**
 * @brief
 *		intermediate_schedule - responsible for starting/restarting scheduling
 *		cycle.
 *
 * @param[in]	sd	-	connection descriptor to the pbs server
 * @param[in]	jobid	-	job to run for a qrun request
 *
 * returns 0
 *
 */
int
intermediate_schedule(int sd, char *jobid)
{
	int ret; /* to re schedule or not */
	int cycle_cnt = 0; /* count of cycles run */

	do {
		ret = scheduling_cycle(sd, jobid);

		/* don't restart cycle if :- */

		/* 1) qrun request, we don't want to keep trying same job */
		if (jobid != NULL)
			break;

		/* Note that a qrun request receiving batch protocol error or any other
		 error will not restart scheduling cycle.
		 */

		/* 2) broken pipe, server connection lost */
		if (got_sigpipe)
			break;

		/* 3) max allowed number of cycles have already been run,
		 *    there can be total of 1 + MAX_RESTART_CYCLECNT cycles
		 */
		if (cycle_cnt > (MAX_RESTART_CYCLECNT - 1))
			break;

		cycle_cnt++;
	}
	while (ret == -1);

	return 0;
}

/**
 * @brief
 *		scheduling_cycle - the controling function of the scheduling cycle
 *
 * @param[in]	sd	-	connection descriptor to the pbs server
 * @param[in]	jobid	-	job to run for a qrun request
 *
 * @return	int
 * @retval	0	: success/normal return
 * @retval	-1	: failure
 *
 */

int
scheduling_cycle(int sd, char *jobid)
{
	server_info *sinfo;		/* ptr to the server/queue/job/node info */
	int rc = SUCCESS;		/* return code from main_sched_loop() */
	char log_msg[MAX_LOG_SIZE];	/* used to log the message why a job can't run*/
	int error = 0;                /* error happened, don't run main loop */
	status *policy;		/* policy structure used for cycle */
	schd_error *err = NULL;

	schdlog(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, LOG_DEBUG,
		"", "Starting Scheduling Cycle");

	update_cycle_status(&cstat, 0);

#ifdef NAS /* localmod 030 */
	do_soft_cycle_interrupt = 0;
	do_hard_cycle_interrupt = 0;
#endif /* localmod 030 */
	/* create the server / queue / job / node structures */
	if ((sinfo = query_server(&cstat, sd)) == NULL) {
		schdlog(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER, LOG_NOTICE,
			"", "Problem with creating server data structure");
		end_cycle_tasks(sinfo);
		return 0;
	}
	policy = sinfo->policy;
	

	/* don't confirm reservations if we're handling a qrun request */
	if (jobid == NULL) {
		if (check_new_reservations(policy, sd, sinfo->resvs, sinfo)) {
			/* Check if there are new reservations.  If there are, we can't go any
			 * further in the scheduling cycle since we don't have the up to date
			 * information about the newly confirmed reservations
			 */
			end_cycle_tasks(sinfo);
			return 0;
		}
	}

	/* jobid will not be NULL if we received a qrun request */
	if (jobid != NULL) {
		schdlog(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, LOG_INFO,
			jobid, "Received qrun request");
		if (is_job_array(jobid) > 1) /* is a single subjob or a range */
			modify_job_array_for_qrun(sinfo, jobid);
		else
			sinfo->qrun_job = find_resource_resv(sinfo->jobs, jobid);

		if (sinfo->qrun_job != NULL) {
			sinfo->qrun_job->can_not_run = 0;
			if (sinfo->qrun_job->job !=NULL) {
				if (sinfo->qrun_job->job->is_waiting ||
					sinfo->qrun_job->job->is_held) {
					set_job_state("Q", sinfo->qrun_job->job);
				}
			}
		}
		else   { /* something went wrong */
			schdlog(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, LOG_INFO, jobid,
				"Could not find job to qrun.");
			error = 1;
			rc = SCHD_ERROR;
			sprintf(log_msg, "PBS Error: Scheduler can not find job");
		}
	}

	if (init_scheduling_cycle(policy, sinfo) == 0) {
		schdlog(PBSEVENT_DEBUG, PBS_EVENTCLASS_SERVER, LOG_DEBUG,
			sinfo->name, "init_scheduling_cycle failed.");
		end_cycle_tasks(sinfo);
		return 0;
	}

	/* run loop run */
	if (error == 0)
		rc = main_sched_loop(policy, sd, sinfo, &err);

	if (jobid != NULL) {
		int def_rc = -1;
		int i;

		for (i = 0; i < MAX_DEF_REPLY && def_rc != 0; i++) {
			/* smooth sailing, the job ran */
			if (rc == SUCCESS)
				def_rc = pbs_defschreply(sd, SCH_SCHEDULE_AJOB, jobid, 0, NULL, NULL);

			/* we thought the job should run, but the server had other ideas */
			else {
				if (err != NULL) {
					translate_fail_code(err, NULL, log_msg);
					if (err->error_code < RET_BASE) {
						error = err->error_code;
					} else {
						/* everything else... unfortunately our ret codes don't nicely match up to
						 * the rest of PBS's PBSE codes, so we return resources unavailable.  This
						 * doesn't really matter, because we're returning a message
						 */
						error = PBSE_RESCUNAV;
					}
				} else
					error = PBSE_RESCUNAV;
				def_rc = pbs_defschreply(sd, SCH_SCHEDULE_AJOB, jobid, error, log_msg, NULL);
			}
			if (def_rc != 0) {
				char *pbs_errmsg;
				pbs_errmsg = pbs_geterrmsg(sd);

				snprintf(log_msg, sizeof(log_msg), "Error in deferred reply: %s",
					pbs_errmsg == NULL ? "" : pbs_errmsg);
				schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING,
					jobid, log_msg);
			}
		}
		if (i == MAX_DEF_REPLY && def_rc != 0) {
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING,
				jobid, "Max deferred reply count reached; giving up.");
		}
	}

#ifdef NAS
	/* localmod 064 */
	site_list_jobs(sinfo, sinfo->jobs);
	/* localmod 034 */
	site_list_shares(stdout, sinfo, "eoc_", 1);
#endif
	end_cycle_tasks(sinfo);
	
	free_schd_error(err);
	if (rc < 0)
		return -1;

	return 0;
}

/**
 * @brief
 * 		the main scheduler loop
 *		Loop until njob = next_job() returns NULL
 *		if njob can run now, run it
 *		if not, attempt preemption
 *		if successful, run njob
 *		njob can't run:
 *		if we can backfill
 *		add job to calendar
 *		deal with normal job can't run stuff
 *
 * @param[in]	policy	-	policy info
 * @param[in]	sd	-	connection descriptor to server or
 *		   	  			SIMULATE_SD if we're simulating
 * @param[in]	sinfo	-	pbs universe we're going to loop over
 * @param[out]	rerr	-	error bits from the last job considered
 *
 *	@return return code of last job scheduled
 *	@retval -1	: on error
 */
int
main_sched_loop(status *policy, int sd, server_info *sinfo, schd_error **rerr)
{
	queue_info *qinfo;		/* ptr to queue that job is in */
	resource_resv *njob;		/* ptr to the next job to see if it can run */
	int rc = 0;			/* return code to the function */
	int num_topjobs = 0;		/* number of jobs we've added to the calendar */
	int end_cycle = 0;		/* boolean  - end main cycle loop */
	char log_msg[MAX_LOG_SIZE];	/* used to log an message about job */
	char comment[MAX_LOG_SIZE];	/* used to update comment of job */
	char buf[MAX_LOG_SIZE];		/* used for misc printing */
	time_t cycle_start_time;	/* the time the cycle starts */
	time_t cycle_end_time;		/* the time when the current cycle should end */
	time_t cur_time;		/* the current time via time() */
	nspec **ns_arr = NULL;		/* node solution for job */
	int i;
	int cmd;
	int sort_again = DONT_SORT_JOBS;
	schd_error *err;
	schd_error *chk_lim_err;


	if (policy == NULL || sinfo == NULL || rerr == NULL)
		return -1;

	time(&cycle_start_time);
	/* calculate the time which we've been in the cycle too long */
	cycle_end_time = cycle_start_time + sinfo->sched_cycle_len;
	
	chk_lim_err = new_schd_error();
	if(chk_lim_err == NULL)
		return -1;
	err = new_schd_error();
	if(err == NULL) {
		free_schd_error(chk_lim_err);
		return -1;
	}

	/* main scheduling loop */
#ifdef NAS
	/* localmod 030 */
	interrupted_cycle_start_time = cycle_start_time;
	/* localmod 038 */
	num_topjobs_per_queues = 0;
	/* localmod 064 */
	site_list_jobs(sinfo, sinfo->jobs);
#endif
	for (i = 0; !end_cycle &&
		(njob = next_job(policy, sinfo, sort_again)) != NULL ; i++) {
#ifdef NAS /* localmod 030 */
		if (check_for_cycle_interrupt(1)) {
			break;
		}
#endif /* localmod 030 */

		rc = 0;
		comment[0] = '\0';
		log_msg[0] = '\0';
		qinfo = njob->job->queue;
		
		clear_schd_error(err);
		err->status_code = NOT_RUN;

		schdlog(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG,
			njob->name, "Considering job to run");

		if (njob->is_shrink_to_fit) {
			/* Pass the suitable heuristic for shrinking */
			ns_arr = is_ok_to_run_STF(policy, sd, sinfo, qinfo, njob, err, shrink_job_algorithm);
		}
		else
			ns_arr = is_ok_to_run(policy, sd, sinfo, qinfo, njob, NO_FLAGS, err);
		
		if (err->status_code == NEVER_RUN)
			njob->can_never_run = 1;

		if (ns_arr != NULL) { /* success! */
			resource_resv *tj;
			if (njob->job->is_array) {
				tj = queue_subjob(njob, sinfo, qinfo);
				if (tj == NULL) {
					rc = SCHD_ERROR;
					njob->can_not_run = 1;
				}
			} else
				tj = njob;

			if (rc != SCHD_ERROR) {
				if(run_update_resresv(policy, sd, sinfo, qinfo, tj, ns_arr, RURR_ADD_END_EVENT, err) > 0 ) {
					rc = SUCCESS;
					sort_again = MAY_RESORT_JOBS;
				} else {
					/* if run_update_resresv() returns 0 and pbs_errno == PBSE_HOOKERROR,
					 * then this job is required to be ignored in this scheduling cycle
					 */
					rc = err->error_code;
					sort_again = SORTED;
				}
			} else
				free_nspecs(ns_arr);
		}
		else if (policy->preempting && in_runnable_state(njob) && (!njob -> can_never_run)) {
			if (find_and_preempt_jobs(policy, sd, njob, sinfo, err) > 0) {
				rc = SUCCESS;
				sort_again = MUST_RESORT_JOBS;
			}
			else
				sort_again = SORTED;
		}

#ifdef NAS /* localmod 034 */
		if (rc == SUCCESS && !site_is_queue_topjob_set_aside(njob)) {
			site_bump_topjobs(njob);
		}
		if (rc == SUCCESS) {
			site_resort_jobs(njob);
		}
#endif /* localmod 034 */

		/* if run_update_resresv() returns an error, it's generally pretty serious.
		 * lets bail out of the cycle now
		 */
		if (rc == SCHD_ERROR || rc == PBSE_PROTOCOL || got_sigpipe) {
			end_cycle = 1;
			schdlog(PBSEVENT_ERROR, PBS_EVENTCLASS_JOB, LOG_WARNING, njob->name, "Leaving scheduling cycle because of an internal error.");
		}
		else if (rc != SUCCESS && rc != RUN_FAILURE) {
			int cal_rc;
			sort_again = SORTED;
#ifdef NAS
			/* localmod 034 */
			/* localmod 038 */
			if (should_backfill_with_job(policy, sinfo, njob, num_topjobs, err)  &&
				((site_is_queue_topjob_set_aside(njob) &&
				++num_topjobs_per_queues <= conf.per_queues_topjobs) ||
				(!site_is_queue_topjob_set_aside(njob) &&
				(site_bump_topjobs(njob) <= conf.per_share_topjobs ||
				num_topjobs < policy->backfill_depth)))) {
#else
			if (should_backfill_with_job(policy, sinfo, njob, num_topjobs)) {
#endif
				cal_rc = add_job_to_calendar(sd, policy, sinfo, njob);

				if (cal_rc > 0) { /* Success! */
#ifdef NAS /* localmod 038 */
					if (!site_is_queue_topjob_set_aside(njob))
#endif /* localmod 038 */
					if (njob->job->is_preempted == 0 || sinfo->enforce_prmptd_job_resumption == 0) { /* preempted jobs don't increase top jobs count */
						if (qinfo->backfill_depth == UNSPECIFIED)
							num_topjobs++;
						else
							qinfo->num_topjobs++;
					}
				}
				else if (cal_rc == -1) { /* recycle */
					end_cycle = 1;
					rc = -1;
					schdlog(PBSEVENT_DEBUG, PBS_EVENTCLASS_SERVER, LOG_DEBUG,
						njob->name, "Error in add_job_to_calendar");
				}
				/* else cal_rc == 0: failed to add to calendar - continue on */
			}

			/* Need to set preemption status so that soft limits can be checked
			 * before updating accrue_type.
			 */
			if (sinfo->eligible_time_enable == 1) {
				struct schd_error *update_accrue_err = err;
				set_preempt_prio(njob, qinfo, sinfo);
				/*
				 * A temporary schd_error location where errors from check_limits
				 * will be stored (when called to check CUMULATIVE limits)
				 */
				clear_schd_error(chk_lim_err);
				if ((sinfo->qrun_job == NULL)) {
					chk_lim_err->error_code = (enum sched_error)check_limits(sinfo,
						qinfo, njob, chk_lim_err, CHECK_CUMULATIVE_LIMIT);
					if (chk_lim_err->error_code != 0) {
						update_accrue_err = chk_lim_err;
					}
					/* Update total_*_counts in server_info and queue_info
					 * based on number of jobs that are either running  or
					 * are considered to run.
					 */
					update_total_counts(sinfo, qinfo, njob, ALL);
				}
				update_accruetype(sd, sinfo, ACCRUE_CHECK_ERR, update_accrue_err->error_code, njob);
			}

			njob->can_not_run = 1;
		}

		if ((rc != SUCCESS) && (err->error_code != 0)) {
			translate_fail_code(err, comment, log_msg);
			if (comment[0] != '\0' &&
				(!njob->job->is_array || !njob->job->is_begin))
				update_job_comment(sd, njob, comment);
			if (log_msg[0] != '\0')
				schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB,
					LOG_INFO, njob->name, log_msg);
		}

		if (njob->can_never_run) {
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_WARNING,
				njob->name, "Job will never run with the resources currently configured in the complex");
		}
		if ((rc != SUCCESS) && njob->job->resv ==NULL) {
			/* jobs in reservations are outside of the law... they don't cause
			 * the rest of the system to idle waiting for them
			 */
			if (policy->strict_fifo) {
				set_schd_error_codes(err, NOT_RUN, STRICT_ORDERING);
				update_jobs_cant_run(sd, qinfo->jobs, NULL, err, START_WITH_JOB);
			}
			else if (!policy->backfill && policy->strict_ordering) {
				set_schd_error_codes(err, NOT_RUN, STRICT_ORDERING);
				update_jobs_cant_run(sd, sinfo->jobs, NULL, err, START_WITH_JOB);
			}
			else if (!policy->backfill && policy->help_starving_jobs &&
				njob->job->is_starving) {
				set_schd_error_codes(err, NOT_RUN, ERR_SPECIAL);
				set_schd_error_arg(err, SPECMSG, "Job would conflict with starving job");
				update_jobs_cant_run(sd, sinfo->jobs, NULL, err, START_WITH_JOB);
			}
		}

		time(&cur_time);
		if (cur_time >= cycle_end_time) {
			end_cycle = 1;
			snprintf(buf, MAX_LOG_SIZE, "Leaving the scheduling cycle: Cycle duration of %ld seconds has exceeded %s of %ld seconds", (long)(cur_time - cycle_start_time), ATTR_sched_cycle_len, sinfo->sched_cycle_len);
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_NOTICE, "toolong", buf);
		}
		if (conf.max_jobs_to_check != SCHD_INFINITY && (i + 1) >= conf.max_jobs_to_check) {
			/* i begins with 0, hence i + 1 */
			end_cycle = 1;
			snprintf(buf, MAX_LOG_SIZE, "Bailed out of main job loop after checking to see if %d jobs could run.", (i + 1));
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO, "", buf);
		}

		if (!end_cycle) {
			if (second_connection != -1) {
				char *jid = NULL;

				/* get_sched_cmd_noblk() located in file get_4byte.c */
				if ((get_sched_cmd_noblk(second_connection, &cmd, &jid) == 1) &&
					(cmd == SCH_SCHEDULE_RESTART_CYCLE)) {
					schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_WARNING,
						njob->name, "Leaving scheduling cycle as requested by server.");
					end_cycle = 1;
				}
				if (jid != NULL)
					free(jid);
			}
		}

#ifdef NAS /* localmod 030 */
		if (check_for_cycle_interrupt(0)) {
			consecutive_interrupted_cycles++;
		}
		else {
			consecutive_interrupted_cycles = 0;
		}
#endif /* localmod 030 */

		/* send any attribute updates to server that we've collected */
		send_job_updates(sd, njob);
	}

	*rerr = err;
		
	free_schd_error(chk_lim_err);
	return rc;
}

/**
 * @brief
 *		end_cycle_tasks - stuff which needs to happen at the end of a cycle
 *
 * @param[in]	sinfo	-	the server structure
 *
 * @return	nothing
 *
 */
void
end_cycle_tasks(server_info *sinfo)
{
	int i;

	/* keep track of update used resources for fairshare */
	if (sinfo != NULL && sinfo->policy->fair_share)
		update_last_running(sinfo);

	/* we copied in conf.fairshare into sinfo at the start of the cycle,
	 * we don't want to free it now, or we'd lose all fairshare data
	 */
	if (sinfo != NULL) {
		sinfo->fairshare = NULL;
		free_server(sinfo, 1);	/* free server and queues and jobs */
	}

	/* close any open connections to peers */
	for (i = 0; (i < NUM_PEERS) &&
		(conf.peer_queues[i].local_queue != NULL); i++) {
		if (conf.peer_queues[i].peer_sd >= 0) {
			/* When peering "local", do not disconnect server */
			if (conf.peer_queues[i].remote_server != NULL)
				pbs_disconnect(conf.peer_queues[i].peer_sd);
			conf.peer_queues[i].peer_sd = -1;
		}
	}

	/* free cmp_aoename */
	if (cmp_aoename != NULL) {
		free(cmp_aoename);
		cmp_aoename = NULL;
	}

	got_sigpipe = 0;
	schdlog(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, LOG_DEBUG,
		"", "Leaving Scheduling Cycle");
}

/**
 * @brief
 *		update_last_running - update the last_running job array
 *			      keep the currently running jobs till next
 *			      scheduling cycle
 *
 * @param[in]	sinfo	-	the server of current jobs
 *
 * @return	success/failure
 *
 */
int
update_last_running(server_info *sinfo)
{
	free_pjobs(last_running, last_running_size);

	last_running = create_prev_job_info(sinfo->running_jobs,
		sinfo->sc.running);
	last_running_size = sinfo->sc.running;

	if (last_running == NULL)
		return 0;

	return 1;
}

/**
 * @brief
 *		update_job_can_not_run - do post job 'can't run' processing
 *				 mark it 'can_not_run'
 *				 update the job comment and log the reason why
 *				 take care of deleting a 'can_never_run job
 *
 * @param[in]	pbs_sd	-	the connection descriptor to the server
 * @param[in,out]	job	-	the job to update
 * @param[in]	err	-	the error structure for why the job can't run
 *
 * @return	int
 * @retval	1	: success
 * @retval	0	: failure.
 *
 */
int
update_job_can_not_run(int pbs_sd, resource_resv *job, schd_error *err)
{
	char comment_buf[MAX_LOG_SIZE];	/* buffer for comment message */
	char log_buf[MAX_LOG_SIZE];		/* buffer for log message */
	char del_buf[MAX_LOG_SIZE];		/* log buffer for deletion message */
	int ret = 1;				/* return code for function */


	job->can_not_run = 1;

	if ((job == NULL) || (err == NULL) || (job->job == NULL))
		return ret;

	if (translate_fail_code(err, comment_buf, log_buf)) {
		/* don't attempt to update the comment on a remote job and on an array job */
		if (!job->is_peer_ob && (!job->job->is_array || !job->job->is_begin))
			update_job_comment(pbs_sd, job, comment_buf);

		/* not attempting to update accrue type on a remote job */
		if (!job->is_peer_ob)
			update_accruetype(pbs_sd, job->server, ACCRUE_CHECK_ERR, err->error_code, job);

		if (log_buf[0] != '\0')
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO,
				job->name, log_buf);

		if (job->can_never_run && !job->is_peer_ob) {
			snprintf(del_buf, MAX_LOG_SIZE, "Job Deleted because it would never run: %s", log_buf);
			schdlog(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, LOG_INFO,
				job->name, del_buf);
			pbs_deljob(pbs_sd, job->name, del_buf);
		}
		
		/* We won't be looking at this job in main_sched_loop()  
		 * and we just updated some attributes just above.  Send Now.
		 */
		send_job_updates(pbs_sd, job);
	}
	else
		ret = 0;

	return ret;
}

/**
 * @brief
 * 		run_job - handle the running of a pbs job.  If it's a peer job
 *	       first move it to the local server and then run it.
 *	       if it's a local job, just run it.
 *
 * @param[in]	pbs_sd	-	pbs connection descriptor to the LOCAL server
 * @param[in]	rjob	-	the job to run
 * @param[in]	execvnode	-	the execvnode to run a multi-node job on
 * @param[in]	throughput	-	thoughput mode enabled?
 * @param[out]	err	-	error struct to return errors
 *
 * @retval	0	: success
 * @retval	1	: failure
 * @retval -1	: error
 */
int
run_job(int pbs_sd, resource_resv *rjob, char *execvnode, int throughput, schd_error *err)
{
	char buf[100];	/* used to assemble queue@localserver */
	char *errbuf;		/* comes from pbs_geterrmsg() */
	int rc = 0;

	if (rjob == NULL || rjob->job == NULL || err == NULL)
		return -1;

	/* Server most likely crashed */
	if (got_sigpipe) {
		set_schd_error_codes(err, NEVER_RUN, SCHD_ERROR);
		return -1;
	}

	if (rjob->is_peer_ob) {
		if (strchr(rjob->server->name, (int) ':') == NULL) {
#ifdef NAS /* localmod 005 */
			sprintf(buf, "%s@%s:%u", rjob->job->queue->name,
#else
			sprintf(buf, "%s@%s:%d", rjob->job->queue->name,
#endif /* localmod 005 */
				rjob->server->name, pbs_conf.batch_service_port);
		}
		else {
			sprintf(buf, "%s@%s", rjob->job->queue->name,
				rjob->server->name);
		}

		if (pbs_sd != SIMULATE_SD)
			rc = pbs_movejob(rjob->job->peer_sd, rjob->name, buf, NULL);

		/*
		 * After successful transfer of the peer job to local server,
		 * reset the peer job flag i.e. is_peer_ob to 0, as it became
		 * a local job.
		 */
		if (rc == 0)
			rjob->is_peer_ob = 0;
	}

	if (!rc) {
		if (pbs_sd != SIMULATE_SD) {
			if (rjob->is_shrink_to_fit) {
				char timebuf[TIMEBUF_SIZE] = {0};
				rc = 1;
				/* The job is set to run, update it's walltime only if it is not a foerever job */
				if (rjob->duration != JOB_INFINITY) {
					convert_duration_to_str(rjob->duration, timebuf, TIMEBUF_SIZE);
					rc = update_job_attr(pbs_sd, rjob, ATTR_l, "walltime", timebuf, NULL, UPDATE_NOW);
				}
				if (rc > 0) {
					if (strlen(timebuf) > 0) {
						char logbuf[MAX_LOG_SIZE];
						snprintf(logbuf, MAX_LOG_SIZE, "Job will run for duration=%s", timebuf);
						schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_NOTICE, rjob->name, logbuf);
					}
					if (throughput)
						rc = pbs_asyrunjob(pbs_sd, rjob->name, execvnode, NULL);
					else
						rc = pbs_runjob(pbs_sd, rjob->name, execvnode, NULL);
				}
			} else {
				if (throughput)
					rc = pbs_asyrunjob(pbs_sd, rjob->name, execvnode, NULL);
				else
					rc = pbs_runjob(pbs_sd, rjob->name, execvnode, NULL);
			}
		} 
	}

	if (rc) {
		char buf[MAX_LOG_SIZE];
		set_schd_error_codes(err, NOT_RUN, RUN_FAILURE);
		errbuf = pbs_geterrmsg(pbs_sd);
		if (errbuf == NULL)
			errbuf = "";
		set_schd_error_arg(err, ARG1, errbuf);
		snprintf(buf, sizeof(buf), "%d", pbs_errno);
		set_schd_error_arg(err, ARG2, buf);
#ifdef NAS /* localmod 031 */
		set_schd_error_arg(err, ARG3, rjob->name);
#endif /* localmod 031 */
	}

	return rc;
}

#ifdef NAS_CLUSTER /* localmod 125 */
/**
 * @brief
 * 		check the run_job return code and decide whether to
 *        consider this job as running or not.
 *
 * @param[in]	pbsrc	-	return code of run_job
 * @param[in]	bjob	-	job structure
 *
 * @return	int
 * @retval	1	-	Job ran successfully
 * @retval	2	-	Job may not be running, ignoring error.
 * @retval	0	-	Job did not run
 * @retval	-1	-	Invalid function parameter
 */
static int translate_runjob_return_code (int pbsrc, resource_resv *bjob)
{
    char log_buf[MAX_LOG_SIZE] = {'\0'};		/* generic buffer - comments & logging */
    if ((bjob == NULL) || (pbsrc == PBSE_PROTOCOL))
        return -1;
    if (pbsrc == 0)
        return 1;
    switch (pbsrc)
    {
        case PBSE_HOOKERROR:
            return 0;
        default:
            sprintf(log_buf, "Transient job warning.  Job may get held if issue persists:%d",pbsrc);
            schdlog(PBSEVENT_ERROR, PBS_EVENTCLASS_JOB, LOG_WARNING, bjob -> name, log_buf);
            return 2;
    }
}
#endif /* localmod 125 */

/**
 * @brief
 * 		run a resource_resv (job or reservation) and
 *		update the local cache if it was successfully
 *		run.  Currently we only simulate the running
 *		of reservations.
 *
 * @param[in]	policy	-	policy info
 * @param[in]	pbs_sd	-	connection descriptor to pbs_server or
 *			  				SIMULATE_SD if we're simulating
 * @param[in]	sinfo	-	server job is on
 * @param[in]	qinfo	-	queue job resides in or NULL if reservation
 * @param[in]	resresv	-	the job/reservation to run
 * @param[in]	ns_arr	-	node solution of where job/resv should run
 *				  			needs to be attached to the job/resv or freed
 * @param[in]	flags	-	flags to modify procedure
 *							RURR_ADD_END_EVENT - add an end event to calendar for this job
 * @param[out]	err	-	error struct to return errors
 *
 * @retval	1	: success
 * @retval	0	: failure (see err for more info)
 * @retval -1	: error
 *
 */
int
run_update_resresv(status *policy, int pbs_sd, server_info *sinfo,
	queue_info *qinfo, resource_resv *resresv, nspec **ns_arr,
	unsigned int flags, schd_error *err)
{
	int ret = 0;				/* return code */
	int pbsrc;				/* return codes from pbs IFL calls */
	node_info **ninfo_arr = NULL;		/* the entire node array to search */
	node_partition **nodepart = NULL;	/* node partitions to serach */
	node_partition *np = NULL;		/* node partition to run resresv on */
	char buf[COMMENT_BUF_SIZE] = {'\0'};		/* generic buffer - comments & logging*/
	char timebuf[128];			/* buffer to hold the time and date */
	int num_nspec;			/* number of nspecs in node solution */

	/* used for jobs with nodes resource */
	nspec **ns = NULL;			/* the nodes to run the job on */
	char *execvnode = NULL;		/* the execvnode to pass to the server*/
	int i;

	/* used for resresv array */
	resource_resv *array = NULL;		/* used to hold array ptr */

	struct batch_status *bs;		/* used for rescspec res assignment */
	struct attrl *attrp;			/* used for rescspec res assignment */
	resource_req *req;
	unsigned int eval_flags = NO_FLAGS;	/* flags to pass to eval_selspec() */
	timed_event *te;			/* used to create timed events */
	sim_info *sim;			/* simulation event trace */
	resource_resv *rr;

	if (resresv == NULL || sinfo == NULL)
		ret = -1;

	if (resresv->is_job && qinfo == NULL)
		ret = -1;

	if (!is_resource_resv_valid(resresv, err)) {
		schdlogerr(PBSEVENT_DEBUG2, PBS_EVENTCLASS_SCHED, LOG_DEBUG, (char *) __func__, "Request not valid:", err);
		ret = -1;
	}

	if (ret == -1) {
		clear_schd_error(err);
		set_schd_error_codes(err, NOT_RUN, SCHD_ERROR);
		free_nspecs(ns_arr);
		return ret;
	}

	pbs_errno = PBSE_NONE;
	if (resresv->is_job && resresv->job->is_suspended) {
		if (pbs_sd != SIMULATE_SD) {
			pbsrc = pbs_sigjob(pbs_sd, resresv->name, "resume", NULL);
			if (!pbsrc)
				ret = 1;
		}
		else 
			ret = 1;
		
		rr = resresv;
		ns = resresv->nspec_arr;
		/* we didn't use nspec_arr, we need to free it */
		free_nspecs(ns_arr);
		ns_arr = NULL;
	}
	else {
		if (resresv->is_job && resresv->job->is_subjob) {
			array = resresv->job->parent_job;
			rr = resresv;
		}
		
		else if(resresv->is_job && resresv->job->is_array) {
			array = resresv;
			rr = queue_subjob(resresv, sinfo, qinfo);
			if(rr == NULL) {
				set_schd_error_codes(err, NOT_RUN, SCHD_ERROR);
				return -1;
			}
		} else
			rr = resresv;

		/* Where should we run our resresv? */

		/* 1) if the resresv knows where it should be run, run it there */
		if (rr->nspec_arr != NULL) {
			ns = rr->nspec_arr;
			/* we didn't use nspec_arr, we need to free it */
			free_nspecs(ns_arr);
			ns_arr = NULL;
		}
		/* 2) if we were told by our caller through ns_arr, run the resresv there */
		else if (ns_arr != NULL) 
			ns = ns_arr;
		/* 3) calculate where to run the resresv ourselves */
		else {
			if (!find_correct_nodes(policy, sinfo, qinfo, rr, &ninfo_arr, &nodepart)) {
				set_schd_error_codes(err, NOT_RUN, SCHD_ERROR);
				return -1;
			}

			/* cached node partition -- use the nodes from that */
			if (rr->nodepart_name != NULL) {
				np = find_node_partition(nodepart, rr->nodepart_name);
				if (np != NULL) {
					ninfo_arr = np->ninfo_arr;
					nodepart = NULL;
					if (np->excl)
						eval_flags |= EVAL_EXCLSET;
				}
				/* np should never be NULL -- if it is, something strange happened
				 * but we can recover and just search for the solution again.
				 * if we can't find one, that error condition is handled further down
				 */
			}

			eval_selspec(policy, rr->select, rr->place_spec,
				ninfo_arr, nodepart, rr, eval_flags, &ns, err);
		}

		if (ns != NULL) {
#ifdef RESC_SPEC /* Hack to make rescspec work with new select code */
			if (rr->is_job && rr->job->rspec != NULL && ns[0] != NULL) {
				bs = rescspec_get_assignments(rr->job->rspec);
				if (bs != NULL) {
					attrp = bs->attribs;
					while (attrp != NULL) {
						req = find_alloc_resource_req_by_str(ns[0]->resreq, attrp->resource);
						if (req != NULL)
							set_resource_req(req, attrp->value);

						if (rr->resreq == NULL)
							rr->resreq = req;
						attrp = attrp->next;
					}
					pbs_statfree(bs);
				}
			}
#endif

			num_nspec = count_array((void **) ns);
			if (num_nspec > 1)
				qsort(ns, num_nspec, sizeof(nspec *), cmp_nspec);

			if (pbs_sd != SIMULATE_SD) {
				if (rr->is_job) { /* don't bother if we're a reservation */
					execvnode = create_execvnode(ns);
					if (execvnode != NULL) {
						/* The nspec array coming out of the node selection code could
						 * have a node appear multiple times.  This is how we need to
						 * send the execvnode to the server.  We now need to combine
						 * nodes into 1 entry for updating our local data structures
						 */
						combine_nspec_array(ns);
					}

					if (rr->nodepart_name != NULL) {
						if (array != NULL)
							update_job_attr(pbs_sd, array, ATTR_pset, NULL,
								array->nodepart_name, NULL, UPDATE_NOW);
						else
							update_job_attr(pbs_sd, rr, ATTR_pset, NULL,
								rr->nodepart_name, NULL, UPDATE_NOW);
					}

#ifdef NAS /* localmod 031 */
					/* debug dpr - Log vnodes assigned to job */
					time_t tm = time(NULL);
					struct tm *ptm = localtime(&tm);
					printf("%04d-%02d-%02d %02d:%02d:%02d %s %s %s\n",
						ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday,
						ptm->tm_hour, ptm->tm_min, ptm->tm_sec,
						"Running", resresv->name,
						execvnode != NULL ? execvnode : "(NULL)");
					fflush(stdout);
#endif /* localmod 031 */

					pbsrc = run_job(pbs_sd, rr, execvnode, sinfo->throughput_mode, err);

#ifdef NAS_CLUSTER /* localmod 125 */
					ret = translate_runjob_return_code(pbsrc, resresv);
#else
					if (!pbsrc)
						ret = 1;
#endif /* localmod 125 */
				}
				else
					ret = 1;
			}
			else {
				/* if we're simulating, resresvs can't fail to run */
				ret = 1;
				execvnode = "";
			}
		}
		else  { /* should never happen */
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_NOTICE, rr->name,
				"Could not find node solution in run_update_resresv()");
			set_schd_error_codes(err, NOT_RUN, SCHD_ERROR);
			ret = 0;
		}

	}

#ifdef NAS_CLUSTER /* localmod 125 */
	if (ret > 0) { /* resresv has successfully been started */
#else
	if (ret != 0) { /* resresv has successfully been started */
#endif /* localmod 125 */
		strftime(timebuf, 128, "%a %b %d at %H:%M",
			localtime(&sinfo->server_time));
		/* any resresv marked can_not_run will be ignored by the scheduler
		 * so just incase we run by this resresv again, we want to ignore it
		 * since it is already running
		 */
		rr->can_not_run = 1;

		/* The nspec array coming out of the node selection code could
		 * have a node appear multiple times.  This is how we need to
		 * send the execvnode to the server.  We now need to combine
		 * nodes into 1 entry for updating our local data structures
		 */
		combine_nspec_array(ns);
		rr->nspec_arr = ns;

		if (array != NULL && array->job->is_queued) {
			sprintf(buf, "Job Array Began at %s", timebuf);
			update_job_comment(pbs_sd, array, buf);
		}

		if (rr->is_job) {
			sprintf(buf, "Job run at %s", timebuf);
			if (execvnode != NULL) {
				/* Job run at <timespec> is about 35 characters, 80 is about all we get
				 * on a qstat -s line.
				 * With qstat -ws, the line can take upto 120 chars. Hence, taking
				 * all of the comment string and handling it in qstat.c
				 * Remark: the buffer is zero'd at initialization time, so we don't need
				 * to worry about the strncat not null terminiating the string
				 */
				strcat(buf, " on ");
				if (strlen(execvnode) > MAXCOMMENTSCOPE) {
					strncat(buf, execvnode, MAXCOMMENTLEN);
					strcat(buf, "...");;
					buf[strlen(buf)] = '\0';
				}
				else
					strcat(buf, execvnode);
			}

			if (!(flags & RURR_NOPRINT)) {
				schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB,
					LOG_INFO, rr->name, "Job run");
			}

			/* array subresresvs can not be altered */
			if (array == NULL)
				update_job_comment(pbs_sd, rr, buf);
		}


		update_resresv_on_run(rr, ns);

		if (array != NULL) {
			update_array_on_run(array->job, rr->job);

			/* Subjobs inherit all attributes from their parent job array. This means
			 * we need to make sure the parent resresv array has its accrue_type set
			 * before running the subresresv.  If all subresresvs have run,
			 * then resresv array's accrue_type becomes ineligible.
			 */
			if (array->is_job &&
				range_next_value(array->job->queued_subjobs, -1) < 0)
				update_accruetype(pbs_sd, sinfo, ACCRUE_MAKE_INELIGIBLE, SUCCESS, array);
			else
				update_accruetype(pbs_sd, sinfo, ACCRUE_MAKE_ELIGIBLE, SUCCESS, array);
		}

		if (ns != NULL) {
			for (i = 0; ns[i] != NULL; i++) {
				update_node_on_run(ns[i], rr);
				/* if the node is being provisioned, it's brought down in
				 * update_node_on_run().  We need to add an event in the calendar to
				 * bring it back up.
				 */
				if (ns[i]->go_provision) {
					if (add_prov_event(sinfo->calendar, sinfo->server_time + PROVISION_DURATION, ns[i]->ninfo)== 0) {
						set_schd_error_codes(err, NOT_RUN, SCHD_ERROR);
						return -1;
					}
				}
			}
		}

		update_queue_on_run(qinfo, rr);
		update_all_nodepart(policy, sinfo, rr);
		update_server_on_run(policy, sinfo, qinfo, rr);

		/* update_preemption_on_run() must be called post queue/server update */
		update_preemption_on_run(sinfo, rr);

		if (sinfo->policy->fair_share)
			update_usage_on_run(rr);
#ifdef NAS /* localmod 057 */
		site_update_on_run(sinfo, qinfo, resresv, ns);
#endif /* localmod 057 */

		if ((flags & RURR_ADD_END_EVENT)) {
			te = create_event(TIMED_END_EVENT, rr->end, rr, NULL, NULL);
			if (te == NULL) {
				set_schd_error_codes(err, NOT_RUN, SCHD_ERROR);
				return -1;
			}
			add_event(sinfo->calendar, te);
		}
	}
	else	 { /* resresv failed to start (server rejected) -- cleanup */
		/*
		 * nspec freeage:
		 * 1) ns_arr is passed in and ours to free.  We need to free it.
		 * 2) ns can be one of three things.
		 *    a) ns_arr - handled by #1 above
		 *    b) resresv -> nspec_arr - not ours, we can't free it
		 *    c) allocated in this function - ours to free
		 */
		if (ns_arr != NULL)
			free_nspecs(ns_arr);
		else if (ns != rr->nspec_arr)
			free_nspecs(ns);

		rr->can_not_run = 1;
		if (array != NULL)
			array->can_not_run = 1;

		/* received 'batch protocol error' */
		if (pbs_errno == PBSE_PROTOCOL) {
			set_schd_error_codes(err, NOT_RUN, PBSE_PROTOCOL);
			return -1;
		}
	}
	
	if (rr->is_job && rr->job->is_preempted) {
		unset_job_attr(pbs_sd, rr, ATTR_sched_preempted, UPDATE_LATER);
		rr->job->is_preempted = 0;
		rr->job->time_preempted = UNSPECIFIED;
		sinfo->num_preempted--;
	}
	return ret;
}

/**
 * @brief
 * 		simulate the running of a resource resv
 *
 * @param[in]	policy	-	policy info
 * @param[in]	resresv	-	the resource resv to simulate running
 * @param[in]	ns_arr  -	node solution of where a job/resv should run
 * @param[in]	flags	-	flags to modify procedure
 *							RURR_ADD_END_EVENT - add an end event to calendar for this job
 *
 * @retval	1	: success
 * @retval	0	: failure
 * @retval	-1	: error
 *
 */
int
sim_run_update_resresv(status *policy, resource_resv *resresv, nspec **ns_arr, unsigned int flags)
{
	server_info *sinfo = NULL;
	queue_info *qinfo = NULL;
	static schd_error *err = NULL;
	
	if(err == NULL)
		err = new_schd_error();

	if (resresv == NULL)
		return -1;

	if (!is_resource_resv_valid(resresv, NULL))
		return -1;

	sinfo = resresv->server;
	if (resresv->is_job)
		qinfo = resresv->job->queue;
	
	clear_schd_error(err);

	return run_update_resresv(policy, SIMULATE_SD, sinfo, qinfo,
		resresv, ns_arr, flags | RURR_NOPRINT, err);

}

/**
 * @brief
 * 		should we call add_job_to_calendar() with job
 *
 * @param[in]	policy	-	policy structure
 * @param[in]	sinfo   -	server where job resides
 * @param[in]	resresv -	the job to check
 * @param[in]	num_topjobs	-	number of topjobs added to the calendar
 *
 * @return	int
 * @retval	1	: we should backfill
 * @retval	0	: we should not
 *
 */
#ifdef NAS /* localmod 065 */
int
should_backfill_with_job(status *policy, server_info *sinfo, resource_resv *resresv, int num_topjobs, schd_error *err)
#else
int
should_backfill_with_job(status *policy, server_info *sinfo, resource_resv *resresv, int num_topjobs)
#endif /* localmod 065 */
{

	if (policy == NULL || sinfo == NULL || resresv == NULL)
		return 0;

	if (resresv->job == NULL)
		return 0;

	if (!policy->backfill)
		return 0;

	/* jobs in reservations are not eligible for backfill */
	if (resresv->job->resv !=NULL)
		return 0;

	if (!resresv->job->is_preempted) {
		queue_info *qinfo = resresv->job->queue;
		int bf_depth;
		int num_tj;

		/* If job is in a queue with a backfill_depth, use it*/
		if (qinfo->backfill_depth != UNSPECIFIED) {
			bf_depth = qinfo->backfill_depth;
			num_tj = qinfo->num_topjobs;
		} /* else check for a server bf depth */
		else if (policy->backfill_depth != UNSPECIFIED) {
			bf_depth = policy->backfill_depth;
			num_tj = num_topjobs;
		} else { /* lastly use the server's default of 1*/
			bf_depth = 1;
			num_tj = num_topjobs;
		}

		if ((num_tj >= bf_depth))
			return 0;
	}

	/* jobs with AOE are not eligible for backfill unless specifically allowed */
	if (!conf.allow_aoe_calendar && resresv->aoename != NULL)
		return 0;

	/* If we know we can never run the job, we shouldn't try and backfill*/
	if (resresv->can_never_run)
		return 0;

	/* Job is preempted and we're helping preempted jobs resume -- add to the calendar*/
	if (resresv->job->is_preempted && sinfo->enforce_prmptd_job_resumption
	    && (resresv->job->preempt >= conf.preempt_normal))
		return 1;

	/* Admin settable flag - don't add to calendar */
	if(resresv->job->topjob_ineligible)
		return 0;

#ifdef NAS /* localmod 065 */
	/* Jobs blocked by other jobs from the same user are not eligible
	 * for starving/backfill help.
	 */
	switch (err->error_code) {
		case SERVER_USER_LIMIT_REACHED:
		case QUEUE_USER_LIMIT_REACHED:
		case SERVER_USER_RES_LIMIT_REACHED:
		case QUEUE_USER_RES_LIMIT_REACHED:
			return 0;
			/* localmod 066 */
			/*
			 * No point to backfill for jobs blocked by dedicated time.  All
			 * resources will become available at the end of the dedicated time.
			 */
		case DED_TIME:
		case CROSS_DED_TIME_BOUNDRY:
			return 0;
			/*
			 * If job exceeds total mission allocation, it can never run.
			 */
		case GROUP_CPU_INSUFFICIENT:
			return 0;
		default:
			;
	}
#endif /* localmod 065 */

	if (policy->strict_ordering)
		return 1;

	if (policy->help_starving_jobs && resresv->job->is_starving)
		return 1;

	return 0;
}

/**
 * @brief
 * 		Find the start time of the top job and init
 *       all the necessary variables in sinfo to correctly backfill
 *       around it.  If no start time can be found, the job is not added
 *	     to the calendar.
 *
 * @param[in]	policy	-	policy info
 * @param[in]	pbs_sd	-	connection descriptor to pbs server
 * @param[in]	policy	-	policy structure
 * @param[in]	sinfo	-	the server to find the topjob in
 * @param[in]	topjob	-	the job we want to backfill around
 *
 * @retval	1	: success
 * @retval	0	: failure
 * @retval	-1	: error
 *
 * @par Side-Effect:
 * 			Use caution when returning failure from this function.
 *		    It will have the effect of exiting the cycle and possibily
 *		    stalling scheduling.  It should only be done for important
 *		    reasons like jobs can't be added to the calendar.
 */
int
add_job_to_calendar(int pbs_sd, status *policy, server_info *sinfo,
	resource_resv *topjob)
{
	server_info *nsinfo;		/* dup'd universe to simulate in */
	resource_resv *njob;		/* the topjob in the dup'd universe */
	resource_resv *bjob;		/* job pointer which becomes the topjob*/
	resource_resv *tjob;		/* temporary job pointer for job arrays */
	time_t start_time;		/* calculated start time of topjob */
	char *exec;			/* used to hold execvnode for topjob */
	timed_event *te_start;	/* start event for topjob */
	timed_event *te_end;		/* end event for topjob */
	timed_event *nexte;
	char log_buf[MAX_LOG_SIZE];

	if (policy == NULL || sinfo == NULL ||
		topjob == NULL || topjob->job == NULL)
		return 0;

	if (sinfo->calendar != NULL) {
		/* if the job is in the calendar, then there is nothing to do
		 * Note: We only ever look from now into the future
		 */
		nexte = get_next_event(sinfo->calendar);
		if (find_timed_event(nexte, topjob->name, TIMED_NOEVENT, 0) != NULL)
			return 1;
	}

	if ((nsinfo = dup_server_info(sinfo)) == NULL)
		return 0;

	if ((njob = find_resource_resv_by_rank(nsinfo->jobs, topjob->rank)) == NULL) {
		free_server(nsinfo, 1);
		return 0;
	}

#ifdef NAS /* localmod 031 */
	snprintf(log_buf, sizeof(log_buf), "Estimating the start time for a top job (q=%s schedselect=%.1000s).", topjob->job->queue->name, topjob->job->schedsel);
	schdlog(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG,
		topjob->name, log_buf);
#else
	schdlog(PBSEVENT_DEBUG2, PBS_EVENTCLASS_JOB, LOG_DEBUG,
		topjob->name, "Estimating the start time for a top job.");
#endif /* localmod 031 */

	start_time = calc_run_time(njob->name, nsinfo, SIM_RUN_JOB);

	if (start_time > 0) {
		/* If our top job is a job array, we don't backfill around the
		 * parent array... rather a subjob.  Normally subjobs don't actually
		 * exist until they are started.  In our case here, we need to create
		 * the subjob so we can backfill around it.
		 */
		if (topjob->job->is_array) {
			tjob = queue_subjob(topjob, sinfo, topjob->job->queue);
			if (tjob == NULL) {
				free_server(nsinfo, 1);
				return 0;
			}
			/* Can't search by rank, we just created tjob and it has a new rank*/
			njob = find_resource_resv(nsinfo->jobs, tjob->name);
			if (njob == NULL) {
				schdlog(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG, __func__,
					"Can't find new subjob in simulated universe");
				free_server(nsinfo, 1);
				return 0;
			}
			/* The subjob is just for the calendar, not for running */
			tjob->can_not_run = 1;
			bjob = tjob;
		}
		else
			bjob = topjob;

		exec = create_execvnode(njob->nspec_arr);
		if (exec != NULL) {
#ifdef NAS /* localmod 068 */
			/* debug dpr - Log vnodes reserved for job */
			time_t tm = time(NULL);
			struct tm *ptm = localtime(&tm);
			printf("%04d-%02d-%02d %02d:%02d:%02d %s %s %s\n",
				ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday,
				ptm->tm_hour, ptm->tm_min, ptm->tm_sec,
				"Backfill", njob->name, exec);
#endif /* localmod 068 */
			bjob->nspec_arr = parse_execvnode(exec, sinfo);
			if (bjob->nspec_arr != NULL) {
				char *selectspec;
				bjob->ninfo_arr =
					create_node_array_from_nspec(bjob->nspec_arr);
				selectspec = create_select_from_nspec(bjob->nspec_arr);
				if (selectspec != NULL) {
					bjob->job->execselect = parse_selspec(selectspec);
					free(selectspec);
				}
			}
			else {
				free_server(nsinfo, 1);
				return 0;
			}
		}
		else {
			free_server(nsinfo, 1);
			return 0;
		}


		if (bjob->job->est_execvnode !=NULL)
			free(bjob->job->est_execvnode);
		bjob->job->est_execvnode = string_dup(exec);
		bjob->job->est_start_time = start_time;
		bjob->start = start_time;
		bjob->end = start_time + bjob->duration;

		te_start = create_event(TIMED_RUN_EVENT, bjob->start, bjob, NULL, NULL);
		if (te_start == NULL) {
			free_server(nsinfo, 1);
			return 0;
		}
		add_event(sinfo->calendar, te_start);

		te_end = create_event(TIMED_END_EVENT, bjob->end, bjob, NULL, NULL);
		if (te_start == NULL) {
			free_server(nsinfo, 1);
			return 0;
		}
		add_event(sinfo->calendar, te_end);

		if (update_estimated_attrs(pbs_sd, bjob, bjob->job->est_start_time,
			bjob->job->est_execvnode, 0) <0) {
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING,
				bjob->name, "Failed to update estimated attrs.");
		}

		if (policy->fair_share) {
			/* update the fairshare usage of this job.  This only modifies the
			 * temporary usage used for this cycle.  Updating this will help the
			 * problem of backfilling other jobs which will affect the fairshare
			 * priority of the top job.  If the priority changes too much
			 * before it is run, the current top job may change in subsequent
			 * cycles
			 */
			update_usage_on_run(bjob);
			sprintf(log_buf, "Fairshare usage of entity %s increased due to job becoming a top job.", bjob->job->ginfo->name);
			schdlog(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG,
				bjob->name, log_buf);
		}

		sprintf(log_buf, "Job is a top job and will run at %s",
			ctime(&bjob->start));

		log_buf[strlen(log_buf)-1] = '\0';	/* ctime adds a \n */
		schdlog(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_DEBUG,
			bjob->name, log_buf);
	} else if (start_time == 0) {
		/* In the case where start_time = 0, we don't want mark the job as
		 * can_never_run because there are transient cases (like node state)
		 * that we don't handle in our simulation that can fix themselves in
		 * real life.  Reconsider this decision once the simulation is more
		 * robust
		 */
		schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_WARNING, topjob->name,
			"Error in calculation of start time of top job");
	}

	free_server(nsinfo, 1);
	return 1;
}


/**
 * @brief
 *		find_ready_resv_job - find a job in a reservation which can run
 *
 * @param[in]	resvs	-	running resvs
 *
 * @return	the first job whose reservation is in RESV_RUNNING
 * @retval	: NULL if there are not any
 *
 */
resource_resv *
find_ready_resv_job(resource_resv **resvs)
{
	int i;
	resource_resv *rjob = NULL;

	if (resvs == NULL)
		return NULL;

	for (i = 0; resvs[i] != NULL && rjob == NULL; i++) {
		if (resvs[i]->resv != NULL) {
			if (resvs[i]->resv->resv_state ==RESV_RUNNING) {
				if (resvs[i]->resv->resv_queue !=NULL)
					rjob = find_runnable_resresv(resvs[i]->resv->resv_queue->jobs);
			}
		}
	}

	return rjob;
}

/**
 * @brief
 *		find_runnable_resresv - find the next runnable resouce resv in an array
 *
 * @param[in]	resresv_arr	-	array of resource resvs to search
 *
 * @return	the next resource resv to run
 * @retval	NULL	: if there are not any
 *
 */
resource_resv *
find_runnable_resresv(resource_resv **resresv_arr)
{
	int i;

	if (resresv_arr == NULL)
		return NULL;

	for (i = 0; resresv_arr[i] != NULL; i++) {
		if (!resresv_arr[i]->can_not_run && in_runnable_state(resresv_arr[i]))
			return resresv_arr[i];
	}
	return NULL;
}

/**
 * @brief
 *		find_non_normal_job - find a runable sched job which is not a normal job in the list of jobs.
 * @par
 * 		ASSUMPTION: express jobs will be sorted to the front of the list, followed by preempted job, followed by starving job
 *
 * @param[in]	jobs	-	the array of jobs
 *
 * @return	first runnable job
 * @retval	NULL	: if there aren't any
 *
 */
resource_resv *
find_non_normal_job(resource_resv **jobs) {
	int i;

	if (jobs == NULL)
		return NULL;

	for (i = 0; jobs[i] != NULL; i++) {
		if (jobs[i]->job != NULL) {
			if ((jobs[i]->job->preempt_status & PREEMPT_TO_BIT(PREEMPT_EXPRESS)) ||
			(jobs[i]->job->is_preempted) || (jobs[i]->job->is_starving)) {
				if (!jobs[i]->can_not_run)
					return jobs[i];
			} else if (jobs[i]->job->preempt_status & PREEMPT_TO_BIT(PREEMPT_NORMAL))
				return NULL;
		}
	}
	return NULL;
}

#ifdef NAS
/**
 * @return
 *		find_susp_job - find a suspended job
 *
 * @param[in]	jobs	-	the array of jobs
 *
 * @return	first suspended job or NULL if there aren't any.
 *
 */
resource_resv *
find_susp_job(resource_resv **jobs) {
	int i;

	if (jobs == NULL)
		return NULL;

	for(i = 0; jobs[i] != NULL; i++) {
		if (jobs[i]->job != NULL) {
			if (jobs[i]->job->is_suspended) {
				return jobs[i];
			}
		}
	}
	return NULL;
}
#endif



/**
 * @brief
 * 		find the next job to be considered to run by the scheduler
 *
 * @param[in]	policy	-	policy info
 * @param[in]	sinfo	-	the server the jobs are on
 * @param[in]	flag	-	whether or not to initialize, sort/re-sort jobs.
 *
 * @return	resource_resv *
 * @retval	the next job to consider
 * @retval  NULL	: on error or if there are no more jobs to run
 *
 * @par MT-safe: No
 *
 */
resource_resv *
next_job(status *policy, server_info *sinfo, int flag)
{
	/* last_queue is the index into a queue array of the last time
	 * the function was called
	 */
	static int last_queue;
	static int last_index;

	/* skip is used to mark that we're done looking for qrun, reservation jobs and
	 * preempted jobs (while using by_queue policy).
	 * In each scheduling cycle skip is reset to 0. Jobs are selected in following
	 * order.
	 * 1. qrun job
	 * 2. jobs in reservation
	 * 3. High priority preempting jobs
	 * 4. Preempted jobs
	 * 5. Starving jobs
	 * 6. Normal jobs
	 */
	static int skip = SKIP_NOTHING;
	static int sort_status = MAY_RESORT_JOBS; /* to decide whether to sort jobs or not */
	static int queue_list_size; /* Count of number of priority levels in queue_list */
	resource_resv *rjob = NULL;		/* the job to return */
	int i = 0;
#ifdef NAS
	resource_resv *(*func)(resource_resv **);
#else
	int queues_finished = 0;
	int queue_index_size = 0;
	int j = 0;
#endif

	if ((policy == NULL) || (sinfo == NULL))
		return NULL;

	if (flag == INITIALIZE) {
		if (policy->round_robin) {
			last_queue = 0;
			last_index = 0;
			queue_list_size = count_array((void**)sinfo->queue_list);

		}
		else if (policy->by_queue)
			last_queue = 0;
		skip = SKIP_NOTHING;
		sort_jobs(policy, sinfo);
		sort_status = SORTED;
		return NULL;
	}

#ifdef NAS /* localmod 034 */
	if (sinfo->qrun_job != NULL) {
		skip = 5;
		if (!sinfo->qrun_job->can_not_run &&
			in_runnable_state(sinfo->qrun_job))
			rjob = sinfo->qrun_job;
		else
			rjob = NULL;
	}

	/* Please note, there are no breaks in this switch statement
	 * Order matters: Each case is higher priority than following cases
	 */
	switch (skip) {
		case 0: /* find jobs in running reservations */
			if (rjob == NULL)
				rjob = find_ready_resv_job(sinfo->resvs);

		case 1: /* find high priority preempting jobs */
			if (rjob == NULL) {
				if (skip < 1)
					skip = 1;
				if (policy->preempting)
					rjob = find_runnable_resresv(sinfo->preempting_jobs);
			}

		case 2: /* find starving jobs */
			if (rjob == NULL) {
				if (skip < 2)
					skip = 2;
#ifdef NAS_FAIR_STARVE /* localmod 069 */
				if (policy->help_starving_jobs) {
					if (policy->fair_share)
						func = &extract_fairshare;
					else
						func = &find_runnable_resresv;
					rjob = (*func)(sinfo->starving_jobs);
				}
#else
				if (policy->help_starving_jobs)
					rjob = find_runnable_resresv(sinfo->starving_jobs);
#endif /* localmod 069 */
			}

		case 3: /* find suspended jobs */
			if (rjob == NULL) {
				if (skip < 3)
					skip = 3;
				rjob = find_susp_job(sinfo->jobs);
			}

		case 4: /* normal jobs */
			if (rjob == NULL) {
				/* use formula (built into job_sort_key)*/
				if (sinfo->job_formula != NULL)
					func = &find_runnable_resresv;
				else if (site_is_share_king(NULL))
					func = &site_pick_next_job;
				/* next: Use fairshare */
				else if (policy->fair_share)
					func = &extract_fairshare;
				else /* lastly use job_sort_key */
					func = &find_runnable_resresv;

				if (skip < 4)
					skip = 4;
				if (policy->round_robin) {
					/* cycle through queues looking for a runnable job */
					for (i = 0; i < sinfo->num_queues && rjob == NULL; i++) {
						/* we have reached the end of the array, cycle back through */
						if (last_queue == (sinfo->num_queues - 1)) {
							last_queue = 0;
						}
						else /* still more queues to go */
							last_queue++;

						/* If we're fairsharing, find the most deserving fairshare job
						 * if we're not, find the first job which is in a runable state
						 * and not marked can_not_run
						 */
						rjob = (*func)(sinfo->queues[last_queue]->jobs);
					}
				}
				else if (policy->by_queue) {
					while (last_queue < sinfo->num_queues &&
						(rjob = (*func)(sinfo->queues[last_queue]->jobs)) == NULL) {
						last_queue++;
					}
				}
				else	 { /* treat the entire system as one large queue */
					rjob = (*func)(sinfo->jobs);
				}
			}
		default:
			/* skip everything */ ;
	}
	return rjob;
#else
	if (sinfo->qrun_job != NULL) {
		if (!sinfo->qrun_job->can_not_run &&
			in_runnable_state(sinfo->qrun_job)) {
			rjob = sinfo->qrun_job;
		}
		return rjob;
	}
	if (skip != SKIP_RESERVATIONS) {
		rjob = find_ready_resv_job(sinfo->resvs);
		if (rjob != NULL)
			return rjob;
		else
			skip = SKIP_RESERVATIONS;
	}

	if ((sort_status != SORTED) || ((flag == MAY_RESORT_JOBS) && policy->fair_share)
		|| (flag == MUST_RESORT_JOBS)) {
		sort_jobs(policy, sinfo);
		sort_status = SORTED;
	}
	if (policy->round_robin) {
		/* Below is a pictorial representation of how queue_list
		 * looks like when policy is set to round_robin.
		 * each column represent the queues which are at a given priority level
		 * Priorities are also sorted in descending order i.e.
		 * Priority 1 > Priority 2 > Priority 3 ...
		 * We make use of each column, traverse through each queue at the same priority
		 * level and run jobs from these queues in round_robin fashion. For example:
		 * If queue 1 has J1,J3 : queue 2 has J2, J5 : queue 4 has J4, J6 then the order
		 * these jobs will be picked would be J1 -> J2 -> J4 -> J3 -> J5 -> J6
		 * When we are finished running all jobs from one priority column, we move onto
		 * next column and repeat the procedure there.
		 */
		/****************************************************
		 *    --------------------------------------------
		 *    | Priority 1 | Priority 2 | .............. |
		 *    --------------------------------------------
		 *    | queue 1    | queue 3    | ........ | NULL|
		 *    --------------------------------------------
		 *    | queue 2    | queue 5    | ........ | NULL|
		 *    --------------------------------------------
		 *    | queue 4    | NULL       | ........ | NULL|
		 *    --------------------------------------------
		 *    | NULL       |            | ........ | NULL|
		 *    --------------------------------------------
		 ****************************************************/
		/* last_index refers to a priority level as shown in diagram
		 * above.
		 */
		i = last_index;
		while((rjob == NULL) && (i < queue_list_size)) {
			/* Calculating number of queues at this priority level */
			queue_index_size = count_array((void **) sinfo->queue_list[i]);
			for (j = last_queue; j < queue_index_size; j++) {
				rjob = find_runnable_resresv(sinfo->queue_list[i][j]->jobs);
				last_queue++;
				/*If all queues are traversed, move back to first queue */
				if (last_queue == queue_index_size)
					last_queue = 0;
				/* Count how many times we've reached the end of a queue.
				 * If we've reached the end of all the queues, we're done.
				 * If we find a job, reset our counter.
				 */
				if (rjob == NULL) {
					queues_finished++;
					if (queues_finished == queue_index_size)
						break;
				} else {
					/* If we are able to get one job from any of the queues,
					 * set queues_finished to 0
					 */
					queues_finished = 0;
					break;
				}
			}
			/* If all queues at the given priority level are traversed
			 * then move onto next index and set last_queue as 0, so as to
			 * start from the first queue of the next index
			 */
			if (queues_finished == queue_index_size) {
							last_queue = 0;
				last_index++;
				i++;
						}
			queues_finished = 0;
					}
				}
				else if (policy->by_queue) {
		if (skip != SKIP_NON_NORMAL_JOBS) {
			rjob = find_non_normal_job(sinfo->jobs);
			if (rjob == NULL)
				/* No more preempted jobs */
				skip = SKIP_NON_NORMAL_JOBS;
		}
		if (skip == SKIP_NON_NORMAL_JOBS) {
					while (last_queue < sinfo->num_queues &&
			    (rjob = find_runnable_resresv(sinfo->queues[last_queue]->jobs)) == NULL) {
						last_queue++;
					}
				}
	}
				else	 { /* treat the entire system as one large queue */
		rjob = find_runnable_resresv(sinfo->jobs);
	}
	return rjob;
#endif /* localmod 034 */
}


#ifdef DEBUG

/**
 * @brief
 * 		to test debug malloc messages -
 * 		set '#define malloc my_malloc' in a place which is included in all the
 * 		files you want to test.  The target tree's pbs_config. is a fine place
 * @par
 * 		run scheduler in debugger and set break points near the mallocs you want
 * 		to fail.  Set the debug_malloc_null to 1 and continue stepping through
 * 		the code.
 *
 * @param[in]	bytes	-	No. of bytes want to allocate.
 *
 * @return	return value from calloc
 * @retval	NULL	: if debug_malloc_null is set.
 */

int debug_malloc_null = 0;
void *
my_malloc(size_t bytes)
{
	if (debug_malloc_null)
		return NULL;

	return calloc(1, bytes);
}

/**
 * @brief
 *  	does not work - when debug_malloc_null is 0, it crashes
 *
 * @param[in]	ptr	-	  pointer to the memory which needs to be reallocated.
 * @param[in]	size	-	size of memory which needs to be reallocated.
 *
 * @return	pointer to the reallocated memory.
 * @retval	NULL	: if debug_malloc_null is set.
 */
void *
my_realloc(void *ptr, size_t size)
{
	void *p;
	if (debug_malloc_null) {
		free(ptr);
		return NULL;
	}

	p = calloc(1, size);
	if (p != NULL)
		memcpy(p, ptr, size);

	free(ptr);
	return p;
}

#endif
