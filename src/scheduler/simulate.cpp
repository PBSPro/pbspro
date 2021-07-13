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
 * 	describe_simret()
 * 	add_prov_event()
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
#include "check.h"
#include "buckets.h"
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
	const char *str;
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
		long t = 1;
		if(arg != NULL) {
			t = *((long *) arg);
			if (t == 0)
				t = 1;
		}
		/* t is the opt_backfill_fuzzy window.  In order to create more consistent estimates
		 * shorten the first window to the next time boundary (e.g. if t=1hr and it
		 * is now 12:31, the first window is 29m).  The subsequent windows will be the same.
		 */
		event_time = (event->event_time + t) / t * t;
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

	if (calendar->first_run_event != NULL && cur_sim_time > calendar->first_run_event->event_time)
		calendar->first_run_event = find_init_timed_event(calendar->next_event, 0, TIMED_RUN_EVENT);

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

	if ((static_cast<resource_resv *>(event_ptr))->start == UNSPECIFIED)
		return 0;

	if ((static_cast<resource_resv *>(event_ptr))->end  == UNSPECIFIED)
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
		if (sinfo->policy->prime_status_end != SCHD_INFINITY) {
			if (te == NULL ||
				(*calendar->current_time <= sinfo->policy->prime_status_end &&
				sinfo->policy->prime_status_end < te->event_time)) {
				event_func_t func;

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
		else if ((e->event_type & search_type_mask) == 0)
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
 * @param[in] 	ignore_disabled - ignore disabled events
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
find_timed_event(timed_event *te_list, const std::string &name, int ignore_disabled,
		 enum timed_event_types event_type, time_t event_time)
{
	timed_event *te;
	int found_name = 0;
	int found_type = 0;
	int found_time = 0;

	if (te_list == NULL)
		return NULL;

	for (te = te_list; te != NULL; te = find_next_timed_event(te, 0, ALL_MASK)) {
		if (ignore_disabled && te->disabled)
			continue;
		found_name = found_type = found_time = 0;
		if (name.empty() || te->name == name)
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

timed_event *find_timed_event(timed_event *te_list, int ignore_disabled, enum timed_event_types event_type, time_t event_time)
{
	return find_timed_event(te_list, "", ignore_disabled, event_type, event_time);
}

timed_event *find_timed_event(timed_event *te_list, enum timed_event_types event_type)
{
	return find_timed_event(te_list, "", 0, event_type, 0);
}

timed_event *find_timed_event(timed_event *te_list, const std::string& name, enum timed_event_types event_type, time_t event_time)
{
	return find_timed_event(te_list, name, 0, event_type, event_time);
}

timed_event *find_timed_event(timed_event *te_list, time_t event_time)
{
	return find_timed_event(te_list, "", 0, TIMED_NOEVENT, event_time);
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
			resresv = static_cast<resource_resv *>(event->event_ptr);
			update_universe_on_end(policy, resresv, "X", NO_ALLPART);

			sprintf(logbuf, "%s end point", resresv->is_job ? "job":"reservation");
			break;
		case TIMED_RUN_EVENT:	/* event_ptr type: (resource_resv *) */
			resresv = static_cast<resource_resv *>(event->event_ptr);
			if (sim_run_update_resresv(policy, resresv, NULL, NO_ALLPART) <= 0) {
				log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO,
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
			log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_JOB, LOG_INFO,
				event->name, "Simulation: Unknown event type");
			ret = 0;
	}
	if (event->event_func != NULL)
		event->event_func(event->event_ptr, event->event_func_arg);

	if (ret)
		log_eventf(PBSEVENT_DEBUG3, PBS_EVENTCLASS_JOB, LOG_DEBUG,
			event->name, "Simulation: %s [%s]", logbuf, timebuf);
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
	if (calendar == NULL || calendar->first_run_event == NULL)
		return 0;

	if (calendar->first_run_event->event_time < end)
		return 1;
	return 0;
}

/**
 * @brief finds if there is a reservation run event between now and 'end'
 * @param[in] calendar - the calendar to search
 * @param[in] end - when to stop searching
 *
 * @returns int
 * @retval 1 found a reservation event
 * @retval 0 did not find a reservation event
 */
int
exists_resv_event(event_list *calendar, time_t end)
{
	timed_event *te;
	timed_event *te_list;

	if (calendar == NULL)
		return 0;

	te_list = calendar->first_run_event;
	if (te_list == NULL) /* no run events in our calendar */
		return 0;

	for (te = te_list; te != NULL && te->event_time <= end;
		te = find_next_timed_event(te, 0, TIMED_RUN_EVENT)) {
		if (te->event_type == TIMED_RUN_EVENT) {
			resource_resv *resresv = static_cast<resource_resv *>(te->event_ptr);
			if(resresv->is_resv)
				return 1;
		}
	}
	return 0;
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
calc_run_time(const std::string& name, server_info *sinfo, int flags)
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
	nspec **ns = NULL;
	unsigned int ok_flags = NO_ALLPART;
	queue_info *qinfo = NULL;

	if (name.empty() || sinfo == NULL)
		return (time_t) -1;

	event_time = sinfo->server_time;
	calendar = sinfo->calendar;

	resresv = find_resource_resv(sinfo->all_resresv, name);

	if (!is_resource_resv_valid(resresv, NULL))
		return (time_t) -1;

	if (flags & USE_BUCKETS)
		ok_flags |= USE_BUCKETS;
	if (resresv->is_job) {
		ok_flags |= IGNORE_EQUIV_CLASS;
		qinfo = resresv->job->queue;
	}

	err = new_schd_error();
	if(err == NULL)
		return (time_t) 0;

	do {
		/* policy is used from sinfo instead of being passed into calc_run_time()
		 * because it's being simulated/updated in simulate_events()
		 */

		auto desc = describe_simret(ret);
		if (desc > 0 || (desc == 0 && policy_change_info(sinfo, resresv))) {
			clear_schd_error(err);
			ns = is_ok_to_run(sinfo->policy, sinfo, qinfo, resresv, ok_flags, err);
		}

		if (ns == NULL) /* event can not run */
			ret = simulate_events(sinfo->policy, sinfo, SIM_NEXT_EVENT, &sc_attrs.opt_backfill_fuzzy, &event_time);

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
		sim_run_update_resresv(sinfo->policy, resresv, ns, NO_ALLPART);
	else
		free_nspecs(ns);

	return event_time;
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
	elist->first_run_event = find_timed_event(elist->events, TIMED_RUN_EVENT);
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
	timed_event	*events = NULL;
	timed_event	*te = NULL;
	resource_resv	**all = NULL;
	int		errflag = 0;
	int		i = 0;
	time_t 		end = 0;
	resource_resv	**all_resresv_copy;
	int		all_resresv_len;

	/* create a temporary copy of all_resresv array which is sorted such that
	 * the timed events are in the front of the array.
	 * Once the first non-timed event is reached, we're done
	 */
	all_resresv_len = count_array(sinfo->all_resresv);
	all_resresv_copy = static_cast<resource_resv **>(malloc((all_resresv_len + 1) * sizeof(resource_resv *)));
	if (all_resresv_copy == NULL)
		return 0;
	for (i = 0; sinfo->all_resresv[i] != NULL; i++)
		all_resresv_copy[i] = sinfo->all_resresv[i];
	all_resresv_copy[i] = NULL;
	all = all_resresv_copy;

	/* sort the all resersv list so all the timed events are in the front */
	qsort(all, count_array(all), sizeof(resource_resv *), cmp_events);

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

		if (sinfo->use_hard_duration)
			end = all[i]->start + all[i]->hard_duration;
		else
			end = all[i]->end;
		te = create_event(TIMED_END_EVENT, end, all[i], NULL, NULL);
		if (te == NULL) {
			errflag++;
			break;
		}
		events = add_timed_event(events, te);
	}

	/* for nodes that are in state=sleep add a timed event */
	for (i = 0; sinfo->nodes[i] != NULL; i++) {
		node_info *node = sinfo->nodes[i];
		if (node->is_sleeping) {
			te = create_event(TIMED_NODE_UP_EVENT, sinfo->server_time + PROVISION_DURATION,
					(event_ptr_t *) node, (event_func_t) node_up_event, NULL);
			if (te == NULL) {
				errflag++;
				break;
			}
			events = add_timed_event(events, te);
		}
	}

	/* A malloc error was encountered, free all allocated memory and return */
	if (errflag > 0) {
		free_timed_event_list(events);
		free(all_resresv_copy);
		return 0;
	}

	free(all_resresv_copy);
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

	if ((elist = static_cast<event_list *>(malloc(sizeof(event_list)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	elist->eol = 0;
	elist->events = NULL;
	elist->next_event = NULL;
	elist->first_run_event = NULL;
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
		nelist->next_event = find_timed_event(nelist->events, oelist->next_event->name,
						      oelist->next_event->event_type,
						      oelist->next_event->event_time);
		if (nelist->next_event == NULL) {
			log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING,
			oelist->next_event->name, "can't find next event in duplicated list");
			free_event_list(nelist);
			return NULL;
		}
	}

	if (oelist->first_run_event != NULL) {
		nelist->first_run_event =
			find_timed_event(nelist->events, oelist->first_run_event->name, TIMED_RUN_EVENT,
					 oelist->first_run_event->event_time);
		if (nelist->first_run_event == NULL) {
			log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING, oelist->first_run_event->name,
				"can't find first run event event in duplicated list");
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

	if ((te = new timed_event()) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	te->disabled = 0;
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
 * @par
 * 		dup_timed_event() modifies the run_event and end_event memebers of the resource_resv.
 * 		If dup_timed_event() is not called as part of server_info() copy constructor, the resource_resvs of
 * 		the main server_info will be modified, even if server_info->calendar is not.
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
	event_ptr_t *event_ptr;

	if (ote == NULL || nsinfo == NULL)
		return NULL;

	event_ptr = find_event_ptr(ote, nsinfo);
	if (event_ptr == NULL)
		return NULL;

	nte = create_event(ote->event_type, ote->event_time, event_ptr, ote->event_func, ote->event_func_arg);
	set_timed_event_disabled(nte, ote->disabled);

	return nte;
}

/*
 * @brief constructor for te_list
 * @return new te_list structure
 */
te_list *
new_te_list() {
	te_list *tel;
	tel = static_cast<te_list *>(malloc(sizeof(te_list)));

	if(tel == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	tel->event = NULL;
	tel->next = NULL;

	return tel;
}

/*
 * @brief te_list destructor
 * @param[in] tel - te_list to free
 *
 * @return void
 */
void
free_te_list(te_list *tel) {
	if(tel == NULL)
		return;
	free_te_list(tel->next);
	free(tel);
}

/*
 * @brief te_list copy constructor
 * @param[in] ote - te_list to copy
 * @param[in] new_timed_even_list - new timed events
 *
 * @return copied te_list
 */
te_list *
dup_te_list(te_list *ote, timed_event *new_timed_event_list)
{
	te_list *nte;

	if(ote == NULL || new_timed_event_list == NULL)
		return NULL;

	nte = new_te_list();
	if(nte == NULL)
		return NULL;

	nte->event = find_timed_event(new_timed_event_list, ote->event->name, ote->event->event_type, ote->event->event_time);

	return nte;
}

/*
 * @brief copy constructor for a list of te_list structures
 * @param[in] ote - te_list to copy
 * @param[in] new_timed_even_list - new timed events
 *
 * @return copied te_list list
 */

te_list *
dup_te_lists(te_list *ote, timed_event *new_timed_event_list) {
	te_list *nte;
	te_list *end_te = NULL;
	te_list *cur;
	te_list *nte_head = NULL;

	if (ote == NULL || new_timed_event_list == NULL)
		return NULL;

	for(cur = ote; cur != NULL; cur = cur->next) {
		nte = dup_te_list(cur, new_timed_event_list);
		if (nte == NULL) {
			free_te_list(nte_head);
			return NULL;
		}
		if(end_te != NULL)
			end_te->next = nte;
		else
			nte_head = nte;

		end_te = nte;
	}
	return nte_head;
}

/*
 * @brief add a te_list for a timed_event to a list sorted by the event's time
 * @param[in,out] tel - te_list to add to
 * @param[in] te - timed_event to add
 *
 * @return success/failure
 * @retval 1 success
 * @retbal 0 failure
 */
int
add_te_list(te_list **tel, timed_event *te) {
	te_list *cur_te;
	te_list *prev = NULL;
	te_list *ntel;

	if(tel == NULL || te == NULL)
		return 0;

	for(cur_te = *tel; cur_te != NULL && cur_te->event->event_time < te->event_time; prev = cur_te, cur_te = cur_te->next)
		;

	ntel = new_te_list();
	if(ntel == NULL)
		return 0;
	ntel->event = te;

	if(prev == NULL) {
		ntel->next = *tel;
		(*tel) = ntel;
	} else {
		prev->next = ntel;
		ntel->next = cur_te;
	}
	return 1;
}

/*
 * @brief remove a te_list from a list by timed_event
 * @param[in,out] tel - te_list to remove event from
 * @param[in] te - timed_event to remove
 *
 * @return success/failure
 * @retval 1 success
 * @retval 0 failure
 */
int
remove_te_list(te_list **tel, timed_event *e)
{
	te_list *prev_tel;
	te_list *cur_tel;

	if(tel == NULL || *tel == NULL || e == NULL)
		return 0;

	prev_tel = NULL;
	for (cur_tel = *tel; cur_tel != NULL && cur_tel->event != e; prev_tel = cur_tel, cur_tel = cur_tel->next)
		;
	if (prev_tel == NULL) {
		*tel = cur_tel->next;
		free(cur_tel);
	}
	else if (cur_tel != NULL) {
		prev_tel -> next = cur_tel -> next;
		free(cur_tel);
	}
	else
		return 0;

	return 1;
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

	if (ote == NULL || nsinfo == NULL)
		return NULL;

	switch (ote->event_type) {
		case TIMED_RUN_EVENT:
		case TIMED_END_EVENT:
			oep = static_cast<resource_resv *>(ote->event_ptr);
			if (oep->is_resv)
				event_ptr =
					find_resource_resv_by_time(nsinfo->all_resresv,
					oep->name, oep->start);
			else
				/* In case of jobs there can be only one occurance of job in
				 * all_resresv list, so no need to search using start time of job
				 */
				event_ptr = find_resource_resv_by_indrank(nsinfo->all_resresv,
					    oep->resresv_ind, oep->rank);

			if (event_ptr == NULL) {
				log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING, ote->name,
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
				static_cast<node_info*>(ote->event_ptr)->name);
			break;
		default:
			log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING, __func__,
				"Unknown event type: %d", (int)ote->event_type);
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
	timed_event *nte_head = NULL;

	if (ote_list == NULL || nsinfo == NULL)
		return NULL;

	for (ote = ote_list; ote != NULL; ote = ote->next) {
		nte = dup_timed_event(ote, nsinfo);
		if (nte == NULL) {
			free_timed_event_list(nte_head);
			return NULL;
		}
		if (nte_prev != NULL)
			nte_prev->next = nte;
		else
			nte_head = nte;
		nte->prev = nte_prev;

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
	if (te->event_ptr != NULL) {
		if (te->event_type & TIMED_RUN_EVENT)
			static_cast<resource_resv *>(te->event_ptr)->run_event = NULL;
		if (te->event_type & TIMED_END_EVENT)
			static_cast<resource_resv *>(te->event_ptr)->end_event = NULL;
	}

	delete te;
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
					find_timed_event(calendar->events, te->event_time);
			}
		}
	}
	/* if next_event == NULL, then we've simulated to the end. */
	else if (te->event_time >= current_time)
		calendar->next_event = te;

	if (te->event_type == TIMED_RUN_EVENT)
		if (calendar->first_run_event == NULL || te->event_time < calendar->first_run_event->event_time)
			calendar->first_run_event = te;

	/* if we had previously run to the end of the list
	 * and now we have more work to do, clear the eol bit
	 */
	if (calendar->eol && calendar->next_event != NULL)
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
		events->prev = te;
		te->prev = NULL;
		return te;
	}

	te->next = eloop;
	eloop_prev->next = te;
	te->prev = eloop_prev;
	if (eloop != NULL)
		eloop->prev = te;

	return events;
}

/**
 * @brief
 * 		delete a timed event from an event_list
 *
 * @param[in] sinfo    - sinfo which contains calendar to delete from
 * @param[in] e        - event to delete
 *
 * @return void
 */

void
delete_event(server_info *sinfo, timed_event *e)
{
	event_list *calendar;

	if (sinfo == NULL || e == NULL)
		return;

	calendar = sinfo->calendar;

	if (calendar->next_event == e)
		calendar->next_event = e->next;

	if (calendar->first_run_event == e)
		calendar->first_run_event = find_timed_event(calendar->events, TIMED_RUN_EVENT);

	if (e->prev == NULL)
		calendar->events = e->next;
	else
		e->prev->next = e->next;

	if (e->next != NULL)
		e->next->prev = e->prev;

	free_timed_event(e);
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

	if (event_type & TIMED_RUN_EVENT)
		static_cast<resource_resv *>(event_ptr)->run_event = te;
	if (event_type & TIMED_END_EVENT)
		static_cast<resource_resv *>(event_ptr)->end_event = te;

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
	const char *name;

	if (te == NULL)
		return 0;

	switch (te->event_type) {
		case TIMED_RUN_EVENT:
		case TIMED_END_EVENT:
			te->name = static_cast<resource_resv*>(te->event_ptr)->name;
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
			te->name = static_cast<node_info*>(te->event_ptr)->name;
			break;
		default:
			log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING,
				__func__, "Unknown event type: %d", (int)te->event_type);
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
		log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_SCHED, LOG_WARNING,
			__func__, "unknown dedicated time change");
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
	if (elist == NULL)
		return 0;


	for (const auto& dt : conf.ded_time) {
		auto te_start = create_event(TIMED_DED_START_EVENT, dt.from, policy, (event_func_t) dedtime_change, (void *) DEDTIME_START);
		if (te_start == NULL)
			return 0;

		auto te_end = create_event(TIMED_DED_END_EVENT, dt.to, policy, (event_func_t) dedtime_change, (void *) DEDTIME_END);
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
 * @param[in] reslist	- resource list to simulate
 * @param[in] end	- end time
 * @param[in] calendar	- calendar to simulate
 * @param[in] incl_arr	- only use events for resresvs in this array (can be NULL)
 * @param[in] exclude	- job/resv to ignore (possibly NULL)
 *
 * @return static pointer to amount of resources available during
 * @retval the entire length from now to end
 * @retval	NULL	: on error
 *
 * @par MT-safe: No
 */
schd_resource *
simulate_resmin(schd_resource *reslist, time_t end, event_list *calendar,
	resource_resv **incl_arr, resource_resv *exclude)
{
	static schd_resource *retres = NULL;	/* return pointer */

	schd_resource *cur_res;
	schd_resource *cur_resmin;
	schd_resource *res;
	schd_resource *resmin = NULL;
	timed_event *te;
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
		auto resresv = static_cast<resource_resv *>(te->event_ptr);
		if (incl_arr == NULL || find_resource_resv_by_indrank(incl_arr, -1, resresv->rank) !=NULL) {
			if (resresv != exclude) {
				for (auto req = resresv->resreq; req != NULL; req = req->next) {
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
const char *
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

	if (!sinfo->queues.empty()) {
		if (policy->is_ded_time && sinfo->has_ded_queue) {
			for (auto qinfo: sinfo->queues) {
				if (qinfo->is_ded_queue &&
					qinfo->jobs !=NULL)
					return 1;
			}
		}
		if (policy->is_prime == PRIME && sinfo->has_prime_queue) {
			for (auto qinfo: sinfo->queues) {
				if (qinfo->is_prime_queue &&
					qinfo->jobs !=NULL)
					return 1;
			}
		}
		if (policy->is_prime == NON_PRIME && sinfo->has_nonprime_queue) {
			for (auto qinfo: sinfo->queues) {
				if (qinfo->is_nonprime_queue &&
					qinfo->jobs !=NULL)
					return 1;
			}
		}
	}
	return 0;
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
