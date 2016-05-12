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
 * @file    simulate.c
 *
 * @brief
 * 		simulate.c - This file contains functions related to simulation of pbs event.
 *
 * Functions included are:
 * 	simulate_events()
 * 	is_timed()
 * 	get_next_event()
 * 	next_event()
 * 	find_init_timed_event()
 * 	find_first_timed_event_backwards()
 * 	find_next_timed_event()
 * 	find_prev_timed_event()
 * 	set_timed_event_disabled()
 * 	find_timed_event()
 * 	perform_event()
 * 	exists_run_event()
 * 	calc_run_time()
 * 	check_events_overlap()
 * 	create_event_list()
 * 	create_events()
 * 	new_event_list()
 * 	dup_event_list()
 * 	free_event_list()
 * 	new_timed_event()
 * 	dup_timed_event()
 * 	find_event_ptr()
 * 	dup_timed_event_list()
 * 	free_timed_event()
 * 	free_timed_event_list()
 * 	add_event()
 * 	add_timed_event()
 * 	delete_event()
 * 	create_event()
 * 	determine_event_name()
 * 	dedtime_change()
 * 	add_dedtime_events()
 * 	simulate_resmin()
 * 	policy_change_to_str()
 * 	policy_change_info()
 * 	check_node_issues()
 * 	calendar_test()
 * 	describe_simret()
 * 	add_prov_event()
 * 	new_sim_info()
 * 	dup_sim_info_list()
 * 	dup_sim_info()
 * 	free_sim_info_list()
 * 	free_sim_info()
 * 	find_simobj_ptr()
 * 	create_add_sim_info()
 * 	sim_id_to_str()
 * 	generic_sim()
 *
 */
#include <pbs_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <log.h>

#include "simulate.h"
#include "data_types.h"
#include "resource_resv.h"
#include "resv_info.h"
#include "node_info.h"
#include "server_info.h"
#include "queue_info.h"
#include "fifo.h"
#include "constant.h"
#include "sort.h"
#include "check.h"
#include "log.h"
#include "misc.h"
#include "prime.h"
#include "globals.h"
#ifdef NAS /* localmod 030 */
#include "site_code.h"
#endif /* localmod 030 */

/** @struct	policy_change_func_name
 *
 * @brief
 * 		structure to map a function pointer to string name
 * 		for printing of policy change events
 */
struct policy_change_func_name
{
	event_func_t func;
	char *str;
};

static const struct policy_change_func_name policy_change_func_name[] =
	{
	{(event_func_t)init_prime_time, "prime time"},
	{(event_func_t)init_non_prime_time, "non-prime time"},
	{NULL, NULL}
};


/**
 * @brief
 * 		simulate the future of a PBS universe
 *
 * @param[in] 	policy   - policy info
 * @param[in] 	sinfo    - PBS universe to simulate
 * @param[in] 	cmd      - simulation command
 * @param[in] 	arg      - optional argument
 * @param[out] 	sim_time - the time in the simulated universe
 *
 * @return	bitfield of what type of event(s) were simulated
 */
unsigned int
simulate_events(status *policy, server_info *sinfo,
	enum schd_simulate_cmd cmd, void *arg, time_t *sim_time)
{
	time_t event_time = 0;	/* time of the event being simulated */
	time_t cur_sim_time = 0;	/* current time in simulation */
	unsigned int ret = 0;
	event_list *calendar;

	timed_event *event;		/* the timed event to take action on */

	if (sinfo == NULL || sim_time == NULL)
		return TIMED_ERROR;

	if (cmd == SIM_TIME && arg == NULL)
		return TIMED_ERROR;

	if (cmd == SIM_NONE)
		return TIMED_NOEVENT;

	if (sinfo->calendar == NULL)
		return TIMED_NOEVENT;

	if (sinfo->calendar->current_time ==NULL)
		return TIMED_ERROR;

	calendar = sinfo->calendar;

	event = next_event(sinfo, DONT_ADVANCE);

	if (event == NULL)
		return TIMED_NOEVENT;

	if (event->disabled)
		event = next_event(sinfo, ADVANCE);

	if (event == NULL)
		return TIMED_NOEVENT;

	cur_sim_time = (*calendar->current_time);

	if (cmd == SIM_NEXT_EVENT) {
		long t = 0;
		if(arg != NULL)
			t = *((long *) arg);
		event_time = event->event_time + t;
	}
	else if (cmd == SIM_TIME)
		event_time = *((time_t *) arg);

	while (event != NULL && event->event_time <= event_time) {
		cur_sim_time = event->event_time;

		(*calendar->current_time) = cur_sim_time;
		if (perform_event(policy, event) == 0) {
			ret = TIMED_ERROR;
			break;
		}

		ret |= event->event_type;

		event = next_event(sinfo, ADVANCE);
	}

	(*sim_time) = cur_sim_time;

	if (cmd == SIM_TIME) {
		(*sim_time) = event_time;
		(*calendar->current_time) = event_time;
	}

	return ret;
}

/**
 * @brief
 *		is_timed - check if an event_ptr has timed elements
 * 			 (i.e. has a start and end time)
 *
 * @param[in]	event_ptr	-	the event to check
 *
 * @return	int
 * @retval	1	: if its timed
 * @retval	0	: if it is not
 *
 */
int
is_timed(event_ptr_t *event_ptr)
{
	if (event_ptr == NULL)
		return 0;

	if (((resource_resv *) event_ptr)->start == UNSPECIFIED)
		return 0;

	if (((resource_resv *) event_ptr)->end  == UNSPECIFIED)
		return 0;

	return 1;
}

/**
 * @brief
 * 		get the next_event from an event list
 *
 * @param[in]	elist	-	the event list
 *
 * @par NOTE:
 * 			If prime status events matter, consider using
 *			next_event(sinfo, DONT_ADVANCE).  This function only
 *			returns the next_event pointer of the event list.
 *
 * @return	the current event from the event list
 * @retval	NULL	: elist is null
 *
 */
timed_event *
get_next_event(event_list *elist)
{
	if (elist == NULL)
		return NULL;

	return elist->next_event;
}

/**
 * @brief
 * 		move sinfo -> calendar to the next event and return it.
 *	     If the next event is a prime status event,  created
 *	     on the fly and returned.
 *
 * @param[in] 	sinfo 	- server containing the calendar
 * @param[in] 	advance - advance to the next event or not.  Prime status
 *			   				event creation happens if we advance or not.
 *
 * @return	the next event
 * @retval	NULL	: if there are no more events
 *
 */
timed_event *
next_event(server_info *sinfo, int advance)
{
	timed_event *te;
	timed_event *pe;
	event_list *calendar;
	event_func_t func;

	if (sinfo == NULL || sinfo->calendar == NULL)
		return NULL;

	calendar = sinfo->calendar;

	if (advance)
		te = find_next_timed_event(calendar->next_event,
			IGNORE_DISABLED_EVENTS, ALL_MASK);
	else
		te = calendar->next_event;

	/* should we add a periodic prime event
	 * i.e. does a prime status event fit between now and the next event
	 * ( now -- Prime Event -- next event )
	 *
	 * or if we're out of events (te == NULL), we need to return
	 * one last prime event.  There may be things waiting on a specific prime
	 * status.
	 */
	if (!calendar->eol) {
		if (sinfo->policy->prime_status_end !=SCHD_INFINITY) {
			if (te == NULL ||
				(*calendar->current_time <= sinfo->policy->prime_status_end &&
				sinfo->policy->prime_status_end < te->event_time)) {
				if (sinfo->policy->is_prime)
					func = (event_func_t) init_non_prime_time;
				else
					func = (event_func_t) init_prime_time;

				pe = create_event(TIMED_POLICY_EVENT, sinfo->policy->prime_status_end,
					(event_ptr_t*) sinfo->policy, func, NULL);

				if (pe == NULL)
					return NULL;

				add_event(sinfo->calendar, pe);
				/* important to set calendar -> eol after calling add_event(),
				 * because add_event() can clear calendar -> eol
				 */
				if (te == NULL)
					calendar->eol = 1;
				te = pe;
			}
		}
	}

	calendar->next_event = te;

	return te;
}

/**
 * @brief
 * 		find the initial event based on a timed_event
 *
 * @param[in]	event            - the current event
 * @param[in] 	ignore_disabled  - ignore disabled events
 * @param[in] 	search_type_mask - bitmask of types of events to search
 *
 * @return	the initial event of the correct type/disabled or not
 * @retval	NULL	: event is NULL.
 *
 * @par NOTE:
 * 			IGNORE_DISABLED_EVENTS exists to be passed in as the
 *		   	ignore_disabled parameter.  It is non-zero.
 *
 * @par NOTE:
 * 			ALL_MASK can be passed in for search_type_mask to search
 *		    for all events types
 */
timed_event *
find_init_timed_event(timed_event *event, int ignore_disabled, unsigned int search_type_mask)
{
	timed_event *e;

	if (event == NULL)
		return NULL;

	for (e = event; e != NULL; e = e->next) {
		if (ignore_disabled && e->disabled)
			continue;
		else if ((e->event_type & search_type_mask) ==0)
			continue;
		else
			break;
	}

	return e;
}

/**
 * @brief
 * 		find the first event based on a timed_event while iterating backwards
 *
 * @param[in] event            - the current event
 * @param[in] ignore_disabled  - ignore disabled events
 * @param[in] search_type_mask - bitmask of types of events to search
 *
 * @return the previous event of the correct type/disabled or not
 *
 * @par NOTE:
 * 			IGNORE_DISABLED_EVENTS exists to be passed in as the
 *		   	ignore_disabled parameter.  It is non-zero.
 *
 * @par NOTE:
 * 			ALL_MASK can be passed in for search_type_mask to search
 *		   	for all events types
 */
timed_event *
find_first_timed_event_backwards(timed_event *event, int ignore_disabled, unsigned int search_type_mask)
{
	timed_event *e;

	if (event == NULL)
		return NULL;

	for (e = event; e != NULL; e = e->prev) {
		if (ignore_disabled && e->disabled)
			continue;
		else if ((e->event_type & search_type_mask) ==0)
			continue;
		else
			break;
	}

	return e;
}
/**
 * @brief
 * 		find the next event based on a timed_event
 *
 * @param[in] event            - the current event
 * @param[in] ignore_disabled  - ignore disabled events
 * @param[in] search_type_mask - bitmask of types of events to search
 *
 * @return	the next timed event of the correct type and disabled or not
 * @retval	NULL	: event is NULL.
 */
timed_event *
find_next_timed_event(timed_event *event, int ignore_disabled, unsigned int search_type_mask)
{
	if (event == NULL)
		return NULL;
	return find_init_timed_event(event->next, ignore_disabled, search_type_mask);
}

/**
 * @brief
 * 		find the previous event based on a timed_event
 *
 * @param[in] event            - the current event
 * @param[in] ignore_disabled  - ignore disabled events
 * @param[in] search_type_mask - bitmask of types of events to search
 *
 * @return	the previous timed event of the correct type and disabled or not
 * @retval	NULL	: event is NULL.
 */
timed_event *
find_prev_timed_event(timed_event *event, int ignore_disabled, unsigned int search_type_mask)
{
	if (event == NULL)
		return NULL;
	return find_first_timed_event_backwards(event->prev, ignore_disabled, search_type_mask);
}
/**
 * @brief
 * 		set the timed_event disabled bit
 *
 * @param[in]	te       - timed event to set
 * @param[in] 	disabled - used to set the disabled bit
 *
 * @return	nothing
 */
void
set_timed_event_disabled(timed_event *te, int disabled)
{
	if (te == NULL)
		return;

	te->disabled = disabled ? 1 : 0;
}

/**
 * @brief
 * 		find a timed_event by any or all of the following:
 *		event name, time of event, or event type.  At times
 *		multiple search parameters are needed to
 *		differentiate between similar events.
 *
 * @param[in]	te_list 	- timed_event list to search in
 * @param[in] 	name    	- name of timed_event to search or NULL to ignore
 * @param[in] 	event_type 	- event_type or TIMED_NOEVENT to ignore
 * @param[in] 	event_time 	- time or 0 to ignore
 *
 * @par NOTE:
 *			If all three search parameters are ignored,  the first event
 *			of te_list will be returned
 *
 * @return	found timed_event
 * @retval	NULL	: on error
 *
 */
timed_event *
find_timed_event(timed_event *te_list, char *name,
	enum timed_event_types event_type, time_t event_time)
{
	timed_event *te;
	int found_name = 0;
	int found_type = 0;
	int found_time = 0;

	if (te_list == NULL)
		return NULL;

	for (te = te_list; te != NULL ; te = find_next_timed_event(te, 0, ALL_MASK)) {
		found_name = found_type = found_time = 0;
		if (name == NULL || strcmp(te->name, name) == 0)
			found_name = 1;

		if (event_type == te->event_type || event_type == TIMED_NOEVENT)
			found_type = 1;

		if (event_time == te->event_time || event_time == 0)
			found_time = 1;

		if (found_name + found_type + found_time == 3)
			break;
	}

	return te;
}
/**
 * @brief
 * 		takes a timed_event and performs any actions
 *		required by the event to be completed.
 *
 * @param[in] policy	-	status
 * @param[in] event 	- 	the event to perform
 *
 * @return int
 * @retval 1	: success
 * @retval 0	: failure
 */
int
perform_event(status *policy, timed_event *event)
{
	char logbuf[MAX_LOG_SIZE];
	char logbuf2[MAX_LOG_SIZE];
	char timebuf[128];
	resource_resv *resresv;
	int ret = 1;

	if (event == NULL || event->event_ptr == NULL)
		return 0;

	sprintf(timebuf, "%s", ctime(&event->event_time));
	/* ctime() puts a \n at the end of the line, nuke it*/
	timebuf[strlen(timebuf) - 1] = '\0';

	switch (event->event_type) {
		case TIMED_END_EVENT:	/* event_ptr type: (resource_resv *) */
			resresv = (resource_resv *) event->event_ptr;
			update_universe_on_end(policy, resresv, "X");

			sprintf(logbuf, "%s end point", resresv->is_job ? "job":"reservation");
			break;
		case TIMED_RUN_EVENT:	/* event_ptr type: (resource_resv *) */
			resresv = (resource_resv *) event->event_ptr;
			if (sim_run_update_resresv(policy, resresv, NULL, RURR_NO_FLAGS) <= 0) {
				schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO,
					event->name, "Simulation: Event failed to be run");
				ret = 0;
			}
			else {
				sprintf(logbuf, "%s start point",
					resresv->is_job ? "job": "reservation");
			}
			break;
		case TIMED_POLICY_EVENT:
			strcpy(logbuf, "Policy change");
			break;
		case TIMED_DED_START_EVENT:
			strcpy(logbuf, "Dedtime Start");
			break;
		case TIMED_DED_END_EVENT:
			strcpy(logbuf, "Dedtime End");
			break;
		case TIMED_NODE_UP_EVENT:
			strcpy(logbuf, "Node Up");
			break;
		case TIMED_NODE_DOWN_EVENT:
			strcpy(logbuf, "Node Down");
			break;
		default:
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO,
				event->name, "Simulation: Unknown event type");
			ret = 0;
	}
	if (event->event_func != NULL)
		event->event_func(event->event_ptr, event->event_func_arg);

	if (ret) {
		snprintf(logbuf2, MAX_LOG_SIZE, "Simulation: %s [%s]", logbuf, timebuf);
		schdlog(PBSEVENT_DEBUG3, PBS_EVENTCLASS_JOB, LOG_DEBUG,
			event->name, logbuf2);
	}
	return ret;
}

/**
 * @brief
 * 		returns 1 if there exists a timed run event in
 *		the event list between the current event
 *		and the last event, or the end time if it is set
 *
 * @param[in] calendar 	- event list
 * @param[in] end 		- optional end time (0 means search all events)
 *
 * @return	int
 * @retval	1	: there exists a run event
 * @retval	0	: there doesn't exist a run event
 *
 */
int
exists_run_event(event_list *calendar, time_t end)
{
	timed_event *te;

	if (calendar == NULL)
		return 0;

	te = get_next_event(calendar);

	if (te == NULL) /* no events in our calendar */
		return 0;

	te = find_init_timed_event(te, IGNORE_DISABLED_EVENTS, TIMED_RUN_EVENT);

	if (te == NULL  ) /* no run event */
		return 0;

	/* there is a run event, but it's after end */
	if (end != 0 && te->event_time > end)
		return 0;

	/* if we got here, we have a happy run event */
	return 1;
}

/**
 * @brief
 * 		calculate the run time of a resresv through simulation of
 *		future calendar events
 *
 * @param[in] name 	- the name of the resresv to find the start time of
 * @param[in] sinfo - the pbs environment
 * 					  NOTE: sinfo will be modified, it should be a copy
 * @param[in] flags - some flags to control the function
 *						SIM_RUN_JOB - simulate running the resresv
 *
 * @return	int
 * @retval	time_t of when the job will run
 *	@retval	0	: can not determine when job will run
 *	@retval	1	: on error
 *
 */
time_t
calc_run_time(char *name, server_info *sinfo, int flags)
{
	time_t event_time = (time_t) 0;	/* time of the simulated event */
	event_list *calendar;		/* calendar we are simulating in */
	resource_resv *resresv;	/* the resource resv to find star time for */
	/* the value returned from simulate_events().  Init to TIMED_END_EVENT to
	 * force the initial check to see if the job can run
	 */
	unsigned int ret = TIMED_END_EVENT;
	schd_error *err = NULL;
	timed_event *te_start;
	timed_event *te_end;
	int desc;
	nspec **ns = NULL;

	if (name == NULL || sinfo == NULL)
		return (time_t) -1;

	event_time = sinfo->server_time;
	calendar = sinfo->calendar;

	resresv = find_resource_resv(sinfo->all_resresv, name);

	if (!is_resource_resv_valid(resresv, NULL))
		return (time_t) -1;
	
	err = new_schd_error();
	if(err == NULL)
		return (time_t) 0;

	do {
		/* policy is used from sinfo instead of being passed into calc_run_time()
		 * because it's being simulated/updated in simulate_events()
		 */

		desc = describe_simret(ret);
		if (desc > 0 || (desc == 0 && policy_change_info(sinfo, resresv))) {
			clear_schd_error(err);
			if (resresv->is_job)
				ns = is_ok_to_run(sinfo->policy, -1, sinfo, resresv->job->queue, resresv, NO_FLAGS, err);
			else
				ns = is_ok_to_run(sinfo->policy, -1, sinfo, NULL, resresv, NO_FLAGS, err);
		}

		if (ns == NULL) /* event can not run */
			ret = simulate_events(sinfo->policy, sinfo, SIM_NEXT_EVENT, &(sinfo->opt_backfill_fuzzy_time), &event_time);

#ifdef NAS /* localmod 030 */
		if (check_for_cycle_interrupt(0)) {
			break;
		}
#endif /* localmod 030 */
	} while (ns == NULL && !(ret & (TIMED_NOEVENT|TIMED_ERROR)));

#ifdef NAS /* localmod 030 */
	if (check_for_cycle_interrupt(0) || (ret & TIMED_ERROR)) {
#else
	if ((ret & TIMED_ERROR)) {
#endif /* localmod 030 */
		free_schd_error(err);
		if (ns != NULL)
			free_nspecs(ns);
		return -1;
	}

	/* we can't run the job, but there are no timed events left to process */
	if (ns == NULL && (ret & TIMED_NOEVENT)) {
		schdlogerr(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING, resresv->name,
				"Can't find start time estimate", err);
		free_schd_error(err);
		return 0;
	}
	
	/* err is no longer needed, we've reported it. */
	free_schd_error(err);
	err = NULL;

	if (resresv->is_job)
		resresv->job->est_start_time = event_time;

	resresv->start = event_time;
	resresv->end = event_time + resresv->duration;

	te_start = create_event(TIMED_RUN_EVENT, resresv->start,
		(event_ptr_t *) resresv, NULL, NULL);
	if (te_start == NULL) {
		if (ns != NULL)
			free_nspecs(ns);
		return -1;
	}

	te_end = create_event(TIMED_END_EVENT, resresv->end,
		(event_ptr_t *) resresv, NULL, NULL);
	if (te_end == NULL) {
		if (ns != NULL)
			free_nspecs(ns);
		free_timed_event(te_start);
		return -1;
	}

	add_event(calendar, te_start);
	add_event(calendar, te_end);

	if (flags & SIM_RUN_JOB)
		sim_run_update_resresv(sinfo->policy, resresv, ns, RURR_NO_FLAGS);
	else
		free_nspecs(ns);

	return event_time;
}

/**
 * @brief
 *		Checks if two resource reservations overlap in time.
 *
 * @see
 *		check_resources_for_node
 *
 * @param[in]	e1_start - start time of event 1
 * @param[in]	e1_end   - end time of event 1
 * @param[in]	e2_start - start time of event 2
 * @param[in]	e2_end   - end time of event 2
 *
 * @return	int
 * @retval	0	: events don't overlap
 * @retval	1	: events overlap
 *
 * @par Side Effects:	Unknown
 *
 * @par Reentrancy:	MT-safe
 *
 */
int
check_events_overlap(time_t e1_start, time_t e1_end, time_t e2_start, time_t e2_end)
{
	/*
	 * check: [e2_start <= e1_start < e2_end]
	 * e2 starts at or before e1 and ends after e1 started,
	 * hence overlap each other.
	 */
	if ((e1_start >= e2_start) && (e1_start < e2_end))
		return 1;

	/*
	 * check: [e1_start <= e2_start < e1_end]
	 * e2 starts at or after e1 but before e1 ends, hence
	 * overlap each other.
	 */
	if ((e2_start >= e1_start) && (e2_start < e1_end))
		return 1;

	return 0;
}
/**
 * @brief
 * 		create an event_list from running jobs and confirmed resvs
 *
 * @param[in]	sinfo	-	server universe to act upon
 *
 * @return	event_list
 */
event_list *
create_event_list(server_info *sinfo)
{
	event_list *elist;

	elist = new_event_list();

	if (elist == NULL)
		return NULL;

	elist->events = create_events(sinfo);

	elist->next_event = elist->events;
	elist->current_time = &sinfo->server_time;
	add_dedtime_events(elist, sinfo->policy);

	return elist;
}

/**
 * @brief
 *		create_events - creates an timed_event list from running jobs
 *			    and confirmed reservations
 *
 * @param[in] sinfo - server universe to act upon
 *
 * @return	timed_event list
 *
 */
timed_event *
create_events(server_info *sinfo)
{
	timed_event *events = NULL;
	timed_event *te;
	resource_resv **all;
	int errflag = 0;
	int i;

	/* all_resresv is sorted such that the timed events are in the front of
	 * the array.  Once the first non-timed event is reached, we're done
	 */
	all = sinfo->all_resresv;

	/* sort the all resersv list so all the timed events are in the front */
	qsort(all, count_array((void **)all), sizeof(resource_resv *), cmp_events);

	for (i = 0; all[i] != NULL && is_timed(all[i]); i++) {
		/* only add a run event for a job or reservation if they're
		 * in a runnable state (i.e. don't add it if they're running)
		 */
		if (in_runnable_state(all[i])) {
			te = create_event(TIMED_RUN_EVENT, all[i]->start, all[i], NULL, NULL);
			if (te == NULL) {
				errflag++;
				break;
			}
			events = add_timed_event(events, te);
		}

		te = create_event(TIMED_END_EVENT, all[i]->end, all[i], NULL, NULL);
		if (te == NULL) {
			errflag++;
			break;
		}
		events = add_timed_event(events, te);
	}

	/* A malloc error was encountered, free all allocated memory and return */
	if (errflag > 0) {
		free_timed_event_list(events);
		return 0;
	}

	return events;
}

/**
 * @brief
 * 		new_event_list() - event_list constructor
 *
 * @return	event_list *
 * @retval	NULL	: malloc failed
 */
event_list *
new_event_list()
{
	event_list *elist;

	if ((elist = malloc(sizeof(event_list))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	elist->eol = 0;
	elist->events = NULL;
	elist->next_event = NULL;
	elist->current_time = NULL;

	return elist;
}

/**
 * @brief
 * 		dup_event_list() - evevnt_list copy constructor
 *
 * @param[in] oelist - event list to copy
 * @param[in] nsinfo - new universe
 *
 * @return	duplicated event_list
 *
 */
event_list *
dup_event_list(event_list *oelist, server_info *nsinfo)
{
	event_list *nelist;

	if (oelist == NULL || nsinfo == NULL)
		return NULL;

	nelist = new_event_list();

	if (nelist == NULL)
		return NULL;

	nelist->eol = oelist->eol;
	nelist->current_time = &nsinfo->server_time;

	if (oelist->events != NULL) {
		nelist->events = dup_timed_event_list(oelist->events, nsinfo);
		if (nelist->events == NULL) {
			free_event_list(nelist);
			return NULL;
		}
	}

	if (oelist->next_event != NULL) {
		nelist->next_event = find_timed_event(nelist->events,
			oelist->next_event->name,
			oelist->next_event->event_type,
			oelist->next_event->event_time);
		if (nelist->next_event == NULL) {
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED,
				LOG_WARNING, oelist->next_event->name,
				"can't find next event in duplicated list");
			free_event_list(nelist);
			return NULL;
		}
	}

	return nelist;
}

/**
 * @brief
 * 		free_event_list - event_list destructor
 *
 * @param[in] elist - event list to freed
 */
void
free_event_list(event_list *elist)
{
	if (elist == NULL)
		return;

	free_timed_event_list(elist->events);
	free(elist);
}

/**
 * @brief
 * 		new_timed_event() - timed_event constructor
 *
 * @return	timed_event *
 * @retval	NULL	: malloc error
 *
 */
timed_event *
new_timed_event()
{
	timed_event *te;

	if ((te = malloc(sizeof(timed_event))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	te->disabled = 0;
	te->name = NULL;
	te->event_type = TIMED_NOEVENT;
	te->event_time = 0;
	te->event_ptr = NULL;
	te->event_func = NULL;
	te->event_func_arg = NULL;
	te->next = NULL;
	te->prev = NULL;

	return te;
}

/**
 * @brief
 * 		dup_timed_event() - timed_event copy constructor
 *
 * @param[in]	ote 	- timed_event to copy
 * @param[in] 	nsinfo 	- "new" universe where to find the event_ptr
 *
 * @return	timed_event *
 * @retval	NULL	: something wrong
 */
timed_event *
dup_timed_event(timed_event *ote, server_info *nsinfo)
{
	timed_event *nte;

	if (ote == NULL || nsinfo == NULL)
		return NULL;

	nte = new_timed_event();

	if (nte == NULL)
		return NULL;

	nte->disabled = ote->disabled;
	nte->event_type = ote->event_type;
	nte->event_time = ote->event_time;
	nte->event_func = ote->event_func;
	nte->event_func_arg = ote->event_func_arg;
	nte->event_ptr = find_event_ptr(ote, nsinfo);

	if (nte->event_ptr == NULL) {
		free_timed_event(nte);
		return NULL;
	}

	if (determine_event_name(nte) == 0) {
		free_timed_event(nte);
		return NULL;
	}

	return nte;
}

/**
 * @brief
 *		find_event_ptr - find the correct event pointer for the duplicated
 *			 event based on event type
 *
 * @param[in]	ote		- old event
 * @param[in] 	nsinfo 	- "new" universe
 *
 * @return event_ptr in new universe
 * @retval	NULL	: on error
 */
event_ptr_t *
find_event_ptr(timed_event *ote, server_info *nsinfo)
{
	resource_resv *oep;	/* old event_ptr in resresv form */
	event_ptr_t *event_ptr = NULL;
	char logbuf[MAX_LOG_SIZE];

	if (ote == NULL || nsinfo == NULL)
		return NULL;

	switch (ote->event_type) {
		case TIMED_RUN_EVENT:
		case TIMED_END_EVENT:
			oep = (resource_resv *) ote->event_ptr;
			event_ptr =
				find_resource_resv_by_time(nsinfo->all_resresv,
				oep->name, oep->start);

			if (event_ptr == NULL) {
				schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING, ote->name,
					"Event can't be found in new server to be duplicated.");
				event_ptr = NULL;
			}
			break;
		case TIMED_POLICY_EVENT:
		case TIMED_DED_START_EVENT:
		case TIMED_DED_END_EVENT:
			event_ptr = nsinfo->policy;
			break;
		case TIMED_NODE_DOWN_EVENT:
		case TIMED_NODE_UP_EVENT:
			event_ptr = find_node_info(nsinfo->nodes,
				((node_info*)(ote->event_ptr))->name);
			break;
		default:
			snprintf(logbuf, MAX_LOG_SIZE, "Unknown event type: %d",
#ifdef NAS /* localmod 005 */
				(int)ote->event_type);
#else
				ote->event_type);
#endif /* localmod 005 */
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING, __func__, logbuf);
	}

	return event_ptr;
}

/**
 * @brief
 *		dup_timed_event_list() - timed_event copy constructor for a list
 *
 * @param[in]	ote_list 	- list of timed_events to copy
 * @param[in]	nsinfo		- "new" universe where to find the event_ptr
 *
 * @return	timed_event *
 * @retval	NULL	: one of the input is null
 */
timed_event *
dup_timed_event_list(timed_event *ote_list, server_info *nsinfo)
{
	timed_event *ote;
	timed_event *nte = NULL;
	timed_event *nte_prev = NULL;
	timed_event *nte_head;

	if (ote_list == NULL || nsinfo == NULL)
		return NULL;

#ifdef NAS /* localmod 005 */
	nte_head = NULL;		/* quiet compiler warning */
#endif /* localmod 005 */
	for (ote = ote_list; ote != NULL; ote = ote->next) {
		nte = dup_timed_event(ote, nsinfo);
		if (nte_prev != NULL)
			nte_prev->next = nte;
		else
			nte_head = nte;

		nte_prev = nte;
	}

	return nte_head;
}

/**
 * @brief
 * 		free_timed_event - timed_event destructor
 *
 * @param[in]	te	-	timed event.
 */
void
free_timed_event(timed_event *te)
{
	if (te == NULL)
		return;

	free(te);
}

/**
 * @brief
 * 		free_timed_event_list - destructor for a list of timed_event structures
 *
 * @param[in]	te_list	-	timed event list
 */
void
free_timed_event_list(timed_event *te_list)
{
	timed_event *te;
	timed_event *te_next;

	if (te_list == NULL)
		return;

	te = te_list;

	while (te != NULL) {
		te_next = te->next;
		free_timed_event(te);
		te = te_next;
	}
}

/**
 * @brief
 * 		add a timed_event to an event list
 *
 * @param[in] calendar - event list
 * @param[in] te       - timed event
 *
 * @retval 1 : success
 * @retval 0 : failure
 *
 */
int
add_event(event_list *calendar, timed_event *te)
{
	time_t current_time;
	int events_is_null = 0;

	if (calendar == NULL || calendar->current_time == NULL || te == NULL)
		return 0;

	current_time = *calendar->current_time;

	if (calendar->events == NULL)
		events_is_null = 1;

	calendar->events = add_timed_event(calendar->events, te);

	/* empty event list - the new event is the only event */
	if (events_is_null)
		calendar->next_event = te;
	else if (calendar->next_event != NULL) {
		/* check if we're adding an event between now and our current event.
		 * If so, it becomes our new current event
		 */
		if (te->event_time > current_time) {
			if (te->event_time < calendar->next_event->event_time)
				calendar->next_event = te;
			else if (te->event_time == calendar->next_event->event_time) {
				calendar->next_event =
					find_timed_event(calendar->events, NULL,
					TIMED_NOEVENT, te->event_time);
			}
		}
	}
	/* if next_event == NULL, then we've simulated to the end. */
	else if (te->event_time >= current_time)
		calendar->next_event = te;

	/* if we had previously run to the end of the list
	 * and now we have more work to do, clear the eol bit
	 */
	if (calendar->eol && calendar->next_event !=NULL)
		calendar->eol = 0;

	return 1;
}

/**
 * @brief
 * 		add_timed_event - add an event to a sorted list of events
 *
 * @note
 *		ASSUMPTION: if multiple events are at the same time, all
 *		    end events will come first
 *
 * @param	events - event list to add event to
 * @param 	te     - timed_event to add to list
 *
 * @return	head of timed_event list
 */
timed_event *
add_timed_event(timed_event *events, timed_event *te)
{
	timed_event *eloop;
	timed_event *eloop_prev = NULL;

	if (te == NULL)
		return events;

	if (events == NULL)
		return te;

	for (eloop = events; eloop != NULL; eloop = eloop->next) {
		if (eloop->event_time > te->event_time)
			break;
		if (eloop->event_time == te->event_time &&
			te->event_type == TIMED_END_EVENT) {
			break;
		}

		eloop_prev = eloop;
	}

	if (eloop_prev == NULL) {
		te->next = events;
		te->prev = NULL;
		return te;
	}

	te->next = eloop;
	eloop_prev->next = te;
	te->prev = eloop_prev;

	return events;
}

/**
 * @brief
 * 		delete a timed event from an event_list
 *
 * @param[in] sinfo    - sinfo which contains calendar to delete from
 * @param[in] e        - event to delete
 * @param[in] flags    - flag bitfield
 *						 DE_UNLINK - unlink event and don't free
 *
 * @return int
 * @retval 1 event was successfully deleted
 * @retval 0 failure
 */

int
delete_event(server_info *sinfo, timed_event *e, unsigned int flags)
{
	timed_event *cur_e;
	timed_event *prev_e = NULL;
	event_list *calendar;

	if (sinfo == NULL || e == NULL)
		return 0;

	calendar = sinfo->calendar;


	for (cur_e = calendar->events; cur_e != e && cur_e != NULL;
		prev_e = cur_e, cur_e = cur_e->next)
			;

	/* found our event to delete */
	if (cur_e != NULL) {
		if (calendar->next_event == cur_e)
			calendar->next_event = cur_e->next;

		if (prev_e == NULL)
			calendar->events = cur_e->next;
		else
			prev_e->next = cur_e->next;

		if ((flags & DE_UNLINK) == 0)
			free_timed_event(cur_e);

		return 1;
	}

	return 0;
}


/**
 * @brief
 *		create_event - create a timed_event with the passed in arguemtns
 *
 * @param[in]	event_type - event_type member
 * @param[in] 	event_time - event_time member
 * @param[in] 	event_ptr  - event_ptr member
 * @param[in] 	event_func - event_func function pointer member
 *
 * @return	newly created timed_event
 * @retval	NULL	: on error
 */
timed_event *
create_event(enum timed_event_types event_type,
	time_t event_time, event_ptr_t *event_ptr,
	event_func_t event_func, void *event_func_arg)
{
	timed_event *te;

	if (event_ptr == NULL)
		return NULL;

	te = new_timed_event();
	if (te == NULL)
		return NULL;

	te->event_type = event_type;
	te->event_time = event_time;
	te->event_ptr = event_ptr;
	te->event_func = event_func;
	te->event_func_arg = event_func_arg;

	if (determine_event_name(te) == 0) {
		free_timed_event(te);
		return NULL;
	}

	return te;
}

/**
 * @brief
 *		determine_event_name - determine a timed events name based off of
 *				event type and sets it
 *
 * @param[in]	te	-	the event
 *
 * @par Side Effects
 *		te -> name is set to static data or data owned by other entities.
 *		It should not be freed.
 *
 * @return	int
 * @retval	1	: if the name was successfully set
 * @retval	0	: if not
 */
int
determine_event_name(timed_event *te)
{
	char *name;
	char logbuf[MAX_LOG_SIZE];

	if (te == NULL)
		return 0;

	switch (te->event_type) {
		case TIMED_RUN_EVENT:
		case TIMED_END_EVENT:
			te->name = ((resource_resv*) te->event_ptr)->name;
			break;
		case TIMED_POLICY_EVENT:
			name = policy_change_to_str(te);
			if (name != NULL)
				te->name = name;
			else
				te->name = "policy change";
			break;
		case TIMED_DED_START_EVENT:
			te->name = "dedtime_start";
			break;
		case TIMED_DED_END_EVENT:
			te->name = "dedtime_end";
			break;
		case TIMED_NODE_UP_EVENT:
		case TIMED_NODE_DOWN_EVENT:
			te->name = ((node_info*) te->event_ptr)->name;
			break;
		default:
#ifdef NAS /* localmod 005 */
			snprintf(logbuf, MAX_LOG_SIZE, "Unknown event type: %d", (int)te->event_type);
#else
			snprintf(logbuf, MAX_LOG_SIZE, "Unknown event type: %d", te->event_type);
#endif /* localmod 005 */
			schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING, __func__, logbuf);
			return 0;
	}

	return 1;
}

/**
 * @brief
 * 		update dedicated time policy
 *
 * @param[in] policy - policy info (contains dedicated time policy)
 * @param[in] arg    - "START" or "END"
 *
 * @return int
 * @retval 1 : success
 * @retval 0 : failure/error
 *
 */

int
dedtime_change(status *policy, void  *arg)
{
	char *event_arg;

	if (policy == NULL || arg == NULL)
		return 0;

	event_arg = (char *)arg;

	if (strcmp(event_arg, DEDTIME_START) == 0)
		policy->is_ded_time = 1;
	else if (strcmp(event_arg, DEDTIME_END) == 0)
		policy->is_ded_time = 0;
	else {
		schdlog(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING, __func__, "unknown dedicated time change");
		return 0;
	}

	return 1;
}

/**
 * @brief
 * 		add the dedicated time events from conf
 *
 * @param[in] elist 	- the event list to add the dedicated time events to
 * @param[in] policy 	- status structure for the dedicated time events
 *
 *	@retval 1 : success
 *	@retval 0 : failure
 */
int
add_dedtime_events(event_list *elist, status *policy)
{
	int i;
	timed_event *te_start;
	timed_event *te_end;

	if (elist == NULL)
		return 0;


	for (i = 0; i < MAX_DEDTIME_SIZE && conf.ded_time[i].from != 0; i++) {
		te_start = create_event(TIMED_DED_START_EVENT, conf.ded_time[i].from, policy, (event_func_t) dedtime_change, (void *) DEDTIME_START);
		if (te_start == NULL)
			return 0;

		te_end = create_event(TIMED_DED_END_EVENT, conf.ded_time[i].to, policy, (event_func_t) dedtime_change, (void *) DEDTIME_END);
		if (te_end == NULL) {
			free_timed_event(te_start);
			return 0;
		}

		add_event(elist, te_start);
		add_event(elist, te_end);
	}
	return 1;
}

/**
 * @brief
 * 		simulate the minimum amount of a resource list
 *		for an event list until a point in time.  The
 *		comparison we are simulating the minimum for is
 *		(resources_available.foo - resources_assigned.foo)
 *		The minimum is simulated by holding resources_available
 *		constant and maximizing the resources_assigned value
 *
 * @note
 * 		This function only simulates START and END events.  If at some
 *		point in the future we start simulating events such as
 *		qmgr -c 's s resources_available.ncpus + =5' this function will
 *		will have to be revisited.
 *
 * @param[in] reslist  	- resource list to simulate
 * @param[in] end	  	- end time
 * @param[in] calendar 	- calendar to simulate
 * @param[in] incl_arr 	- only use events for resresvs in this array (can be NULL)
 * @param[in] exclude	- job/resv to ignore (possibly NULL)
 *
 * @return static pointer to amount of resources available during
 * @retval the entire length from now to end
 * @retval	NULL	: on error
 *
 * @par MT-safe: No
 */
resource *
simulate_resmin(resource *reslist, time_t end, event_list *calendar,
	resource_resv **incl_arr, resource_resv *exclude)
{
	static resource *retres = NULL;	/* return pointer */

	resource *cur_res;
	resource *cur_resmin;
	resource_req *req;
	resource *res;
	resource *resmin = NULL;
	timed_event *te;
	resource_resv *resresv;
	unsigned int event_mask = (TIMED_RUN_EVENT | TIMED_END_EVENT);

	if (reslist == NULL)
		return NULL;

	/* if there is no calendar, then there is nothing to do */
	if (calendar == NULL)
		return reslist;

	/* If there are no run events in the calendar between now and the end time
	 * then there is nothing to do. Nothing will reduce resources (only increase)
	 */
	if (exists_run_event(calendar, end) == 0)
		return reslist;

	if (retres != NULL) {
		free_resource_list(retres);
		retres = NULL;
	}

	if ((res = dup_resource_list(reslist)) == NULL)
		return NULL;
	if ((resmin = dup_resource_list(reslist)) == NULL) {
		free_resource_list(res);
		return NULL;
	}

	te = get_next_event(calendar);
	for (te = find_init_timed_event(te, IGNORE_DISABLED_EVENTS, event_mask);
		te != NULL && (end == 0 || te->event_time < end);
		te = find_next_timed_event(te, IGNORE_DISABLED_EVENTS, event_mask)) {
		resresv = (resource_resv*) te->event_ptr;
		if (incl_arr == NULL || find_resource_resv_by_rank(incl_arr, resresv->rank) !=NULL) {
			if (resresv != exclude) {
				req = resresv->resreq;

				for (; req != NULL; req = req->next) {
					if (req->type.is_consumable) {
						cur_res = find_alloc_resource(res, req->def);

						if (cur_res == NULL) {
							free_resource_list(res);
							free_resource_list(resmin);
							return NULL;
						}

						if (te->event_type == TIMED_RUN_EVENT)
							cur_res->assigned += req->amount;
						else
							cur_res->assigned -= req->amount;

						cur_resmin = find_alloc_resource(resmin, req->def);
						if (cur_resmin == NULL) {
							free_resource_list(res);
							free_resource_list(resmin);
							return NULL;
						}
						if (cur_res->assigned > cur_resmin->assigned)
							cur_resmin->assigned = cur_res->assigned;
					}
				}
			}
		}
	}
	free_resource_list(res);
	retres = resmin;
	return retres;
}

/**
 * @brief
 * 		return a printable name for a policy change event
 *
 * @param[in]	te	-	policy change timed event
 *
 * @return	printable string name of policy change event
 * @retval	NULL	: if not found or error
 */
char *
policy_change_to_str(timed_event *te)
{
	int i;
	if (te == NULL)
		return NULL;

	for (i = 0; policy_change_func_name[i].func != NULL; i++) {
		if (te->event_func == policy_change_func_name[i].func)
			return policy_change_func_name[i].str;
	}

	return NULL;
}

/**
 * @brief
 * 		should we do anything on policy change events
 *
 * @param[in] sinfo 	- server
 * @param[in] resresv 	- a resresv to check
 *
 * @return	int
 * @retval	1	: there is something to do
 * @retval 	0 	: nothing to do
 * @retval 	-1 	: error
 *
 */
int
policy_change_info(server_info *sinfo, resource_resv *resresv)
{
	int i;
	status *policy;

	if (sinfo == NULL || sinfo->policy == NULL)
		return -1;

	policy = sinfo->policy;

	/* check to see if we may be holding resoures by backfilling during one
	 * prime status, just to turn it off in the next, thus increasing the
	 * resource pool
	 */
	if (conf.prime_bf != conf.non_prime_bf)
		return 1;

	/* check to see if we're backfilling around prime status changes
	 * if we are, we may have been holding up running jobs until the next
	 * prime status change.  In this case, we have something to do at a status
	 * change.
	 * We only have to worry if prime_exempt_anytime_queues is false.  If it is
	 * True, backfill_prime only affects prime or non-prime queues which we
	 * handle below.
	 */
	if (!conf.prime_exempt_anytime_queues &&
		(conf.prime_bp + conf.non_prime_bp >= 1))
		return 1;

	if (resresv != NULL) {
		if (resresv->is_job && resresv->job !=NULL) {
			if (policy->is_ded_time && resresv->job->queue->is_ded_queue)
				return 1;
			if (policy->is_prime == PRIME &&
				resresv->job->queue->is_prime_queue)
				return 1;
			if (policy->is_prime == NON_PRIME &&
				resresv->job->queue->is_nonprime_queue)
				return 1;
		}

		return 0;
	}

	if (sinfo->queues != NULL) {
		if (policy->is_ded_time && sinfo->has_ded_queue) {
			for (i = 0; sinfo->queues[i] != NULL; i++) {
				if (sinfo->queues[i]->is_ded_queue &&
					sinfo->queues[i]->jobs !=NULL)
					return 1;
			}
		}
		if (policy->is_prime == PRIME && sinfo->has_prime_queue) {
			for (i = 0; sinfo->queues[i] != NULL; i++) {
				if (sinfo->queues[i]->is_prime_queue &&
					sinfo->queues[i]->jobs !=NULL)
					return 1;
			}
		}
		if (policy->is_prime == NON_PRIME && sinfo->has_nonprime_queue) {
			for (i = 0; sinfo->queues[i] != NULL; i++) {
				if (sinfo->queues[i]->is_nonprime_queue &&
					sinfo->queues[i]->jobs !=NULL)
					return 1;
			}
		}
	}
	return 0;
}

/**
 * @brief
 * 		debug function to check a list of nodes if any
 *		consumable resources are over subscribed
 *
 * @param[in] nodes - the nodes to check
 * @param[in] quiet - if true, don't print to stderr
 *
 * @return	int
 * @retval	1	: no issues
 * @retval 	0 	: node issues (print issues to stderr)
 * @retval -1 	: error
 */
int
check_node_issues(node_info **nodes, int quiet)
{
	int i;
	resource *res;
	sch_resource_t dyn_avail;
	int rc = 1;

	if (nodes == NULL)
		return -1;

	for (i = 0; nodes[i] != NULL; i++) {
		for (res = nodes[i]->res; res != NULL; res = res->next) {
			if (res->type.is_consumable) {
				dyn_avail = res->avail == SCHD_INFINITY ? 0 :
					(res->avail - res->assigned);
				if (dyn_avail < 0) {
					fprintf(stderr, "Node %s: resource %s: %.0lf\n", nodes[i]->name,
						res->name, dyn_avail);
					rc = 0;
				}
			}
		}
	}
	return rc;
}

/**
 * @brief
 * 		debug function to test a calendar.  It will run a
 *		calendar from the current point to the end and report
 *
 * @param[in]	sinfo - server which contains the calendar
 * @param[in]	quiet - true to not print anything to stderr
 *
 * @retval	1 : no errors
 * @retval 	0 : calendar errors (with text printed to stderr)
 * @retval -1 : error
 */
int
calendar_test(server_info *sinfo, int quiet)
{

	server_info *nsinfo;
	unsigned int ret = 0;
	time_t event_time;
	int rc = 1;
	int i;


	nsinfo = dup_server_info(sinfo);

	if (nsinfo == NULL)
		return -1;

	while (!(ret & (TIMED_ERROR | TIMED_NOEVENT)) && rc) {
		ret = simulate_events(nsinfo->policy, nsinfo, SIM_NEXT_EVENT, NULL, &event_time);

		rc = check_node_issues(nsinfo->nodes, quiet);
		if (rc == 0) {
			if (!quiet)
				fprintf(stderr, "time: %s", ctime(&sinfo->server_time));
		}
	}

	if (nsinfo->running_jobs[0] != NULL) {
		if (!quiet) {
			fprintf(stderr, "Running Jobs: ");
			for (i = 0; nsinfo->running_jobs[i] != NULL; i++)
				fprintf(stderr, "%s ", nsinfo->running_jobs[i]->name);
			fprintf(stderr, "\n");
		}
		rc = 0;
	}

	if (ret & TIMED_ERROR) {
		if (!quiet)
			fprintf(stderr, "Simulation Error.\n");
		rc = 0;
	}

	free_server(nsinfo, 0);

	return rc;
}

/**
 * @brief
 * 		takes a bitfield returned by simulate_events and will determine if
 *      the amount resources have gone up down, or are unchanged.  If events
 *	  	caused resources to be both freed and used, we err on the side of
 *	  	caution and say there are more resources.
 *
 * @param[in]	simret	-	return bitfield from simulate_events
 *
 * @retval	1 : more resources are available for use
 * @retval  0 : resources have not changed
 * @retval -1 : less resources are available for use
 */
int
describe_simret(unsigned int simret)
{
	unsigned int more =
		(TIMED_END_EVENT | TIMED_DED_END_EVENT | TIMED_NODE_UP_EVENT);
	unsigned int less =
		(TIMED_RUN_EVENT | TIMED_DED_START_EVENT | TIMED_NODE_DOWN_EVENT);

	if (simret & more)
		return 1;
	if (simret & less)
		return -1;

	return 0;
}

/**
 * @brief
 * 		adds event(s) for bringing the node back up after we provision a node
 *
 * @param[in] calnedar 		- event list to add event(s) to
 * @param[in] event_time 	- time of the event
 * @param[in] node 			- node in question
 *
 * @return	success/failure
 * @retval 	1 : on sucess
 * @retval 	0 : in failure/error
 */
int
add_prov_event(event_list *calendar, time_t event_time, node_info *node)
{
	timed_event *te;

	if (calendar == NULL || node == NULL)
		return 0;

	te = create_event(TIMED_NODE_UP_EVENT, event_time, (event_ptr_t *) node,
		(event_func_t) node_up_event, NULL);
	if (te == NULL)
		return 0;
	add_event(calendar, te);
	/* if the node is resv node, we need to add an event to bring the
	 * server version of the resv node back up
	 */
	if (node->svr_node != NULL) {
		te = create_event(TIMED_NODE_UP_EVENT, event_time,
			(event_ptr_t *) node->svr_node, (event_func_t) node_up_event, NULL);
		if (te == NULL)
			return 0;
		add_event(calendar, te);
	}
	return 1;
}

/**
 * @brief
 * 		constructor for sim_info
 *
 * @return	newly created sim_info
 */
sim_info *
new_sim_info()
{
	sim_info *sim;

	if ((sim = malloc(sizeof(sim_info))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	sim->id = SIMID_NONE;
	sim->info[0] = '\0';
	sim->simobj = NULL;
	sim->next = NULL;

	return sim;
}

/**
 * @brief
 * 		copy constructor for a list of sim_info
 *
 * @param[in]	sim 	- sim object whose obj needs to be copied
 * @param[in] 	nsinfo 	- the server info
 *
 * @return	new sim_info which contains copied values.
 */
sim_info *
dup_sim_info_list(sim_info *osim_list, server_info *nsinfo)
{
	sim_info *nsim_list = NULL;
	sim_info *osim;
	sim_info *nsim;
	sim_info *prev_nsim = NULL;

	for (osim = osim_list; osim != NULL; osim = osim->next, prev_nsim = nsim) {
		nsim = dup_sim_info(osim, nsinfo);
		if (nsim == NULL) {
			free_sim_info_list(nsim_list);
			return NULL;
		}
		if (nsim_list == NULL)
			nsim_list = nsim;

		if (prev_nsim != NULL)
			prev_nsim->next = nsim;
	}
	return nsim_list;
}
/**
 * @brief
 * 		copy constructor for sim_info
 *
 * @param[in]	osim 	- sim object whose obj needs to be copied
 * @param[in] 	nsinfo 	- the server info
 *
 * @return	new sim_info which contains copied values.
 */
sim_info *
dup_sim_info(sim_info *osim, server_info *nsinfo)
{
	sim_info *nsim;

	if (osim == NULL)
		return NULL;

	nsim = new_sim_info();
	if (nsim == NULL)
		return NULL;

	nsim->id = osim->id;
	strcpy(nsim->info, osim->info);

	nsim->simobj = find_simobj_ptr(osim, nsinfo);

	return nsim;
}

/**
 * @brief
 * 		destructor for sim_info list
 *
 * @param[in]	sim_list	-	sim list which needs to be destroyed.
 */
void
free_sim_info_list(sim_info *sim_list)
{
	sim_info *sim;
	sim_info *next_sim;

	for (sim = sim_list; sim != NULL; sim = next_sim) {
		next_sim = sim->next;
		free_sim_info(sim);
	}
}

/**
 * @brief
 * 		destructor for sim_info
 *
 * @param[in]	sim	-	sim object which needs to be destroyed.
 */
void
free_sim_info(sim_info *sim)
{
	free(sim);
}

/**
 * @brief
 * 		find an simobj ptr in a server_info
 *
 * @param[in] sim 		- sim object whose obj ptr we need to find
 * @param[in] nsinfo 	- the server to find it from
 *
 * @return	new simobj ptr
 */
void *
find_simobj_ptr(sim_info *sim, server_info *nsinfo)
{
	if (sim == NULL || nsinfo == NULL)
		return NULL;

	switch (sim->id) {
			/* job events */
		case SIMID_RUN_JOB:
		case SIMID_MOVE_JOB:
		case SIMID_MODIFY_JOB:
		case SIMID_SUSPEND_JOB:
		case SIMID_CHKP_JOB:
		case SIMID_REQUEUE_JOB:
			return find_resource_resv(nsinfo->jobs, ((resource_resv*)(sim->simobj))->name);
		case SIMID_NONE:
			return NULL;
		case SIMID_HIGH:
		default:
			schdlog(PBSEVENT_DEBUG, PBS_EVENTCLASS_SCHED, LOG_WARNING, __func__, "Unknown SIMID event type");
	}
	return NULL;
}


/**
 * @brief
 * 		create and add sim_info to list
 *
 * @param[in] sim_list 	- list to add sim_info to
 * @param[in] id 		- id of sim event
 * @param[in] info     	- inf of sim event
 *
 * @return	new sim_info
 * @retval	newly created sim_info
 * @retval	NULL	: on error
 */
sim_info *
create_add_sim_info(sim_info *sim_list,
	enum sim_info_id simid, char *info, void *simobj)
{
	sim_info *sim;
	sim_info *nsim;

	nsim = new_sim_info();
	if (nsim == NULL)
		return NULL;

	nsim->id = simid;
	if (info != NULL)
		strcpy(nsim->info, info);
	nsim->simobj = simobj;

	for (sim = sim_list; sim != NULL && sim->next != NULL; sim = sim->next)
		;

	if (sim != NULL)
		sim->next = nsim;

	return nsim;
}
/**
 * @brief
 * 		generic simulation function which will call a function pointer over
 *      events of a calendar from now up to (but not including) the end time.
 * @par
 *	  	The simulation works by looping searching for a success or failure.
 *	  	The loop will stop if the function returns 1 for success or -1 for
 *	  	failure.  We continue looping if the function returns 0.  If we run
 *	  	out of events, we return the default passed in.
 *
 * @par Function:
 * 		The function can return three return values
 *	 	>0 success - stop looping and return success
 *	  	0 failure - keep looping
 *	 	<0 failure - stop looping and return failure
 *
 * @param[in] calendar 		- calendar of timed events
 * @param[in] event_mask 	- mask of timed_events which we want to simulate
 * @param[in] end 			- end of simulation (0 means search all events)
 * @param[in] default_ret 	- default return value if we reach the end of the simulation
 * @param[in] func 			- the function to call on each timed event
 * @param[in] arg1 			- generic arg1 to function
 * @param[in] arg2 			- generic arg2 to function
 *
 * @return success of simulate
 * @retval 1 : if simulation is success
 * @retval 0 : if func returns failure or there is an error
 */
int
generic_sim(event_list *calendar, unsigned int event_mask, time_t end, int default_ret,
	int (*func)(timed_event*, void*, void*), void *arg1, void *arg2)
{
	timed_event *te;
	int rc = 0;
	if (calendar == NULL || func == NULL)
		return 0;

	/* We need to handle the calendar's initial event special because
	 * get_next_event() only returns the calendar's next_event member.
	 * We need to make sure the initial event is of the correct type.
	 */
	te = get_next_event(calendar);

	for (te = find_init_timed_event(te, IGNORE_DISABLED_EVENTS, event_mask);
		te != NULL && rc == 0 && (end == 0 || te->event_time < end);
		te = find_next_timed_event(te, IGNORE_DISABLED_EVENTS, event_mask)) {
		rc = func(te, arg1, arg2);
	}

	if (rc > 0)
		return 1;
	else if (rc < 0)
		return 0;

	return default_ret;
}
