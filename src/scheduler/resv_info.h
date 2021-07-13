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

#ifndef	_RESV_INFO_H
#define	_RESV_INFO_H

#include <pbs_config.h>
#include "data_types.h"

/*
 *	stat_resvs - status the reservations in batch_status form from the
 *	server
 */
struct batch_status *stat_resvs(int pbs_sd);

/*
 *	query_reservations - query the reservations from the server
 */
resource_resv **query_reservations(int pbs_sd, server_info *sinfo, struct batch_status *resvs);

/*
 *	query_resv_info - convert the servers batch_statys structure into a
 */
resource_resv *query_resv(struct batch_status *resv, server_info *sinfo);

/*
 *	new_resv_info - allocate and initialize new resv_info structure
 */
#ifdef NAS /* localmod 005 */
resv_info *new_resv_info(void);
#else
resv_info *new_resv_info();
#endif /* localmod 005 */

/*
 *	free_resv_info - free all the memory used by a rev_info structure
 */
void free_resv_info(resv_info *rinfo);

/*
 *	dup_resv_info - duplicate a reservation
 */
resv_info *dup_resv_info(resv_info *rinfo, server_info *sinfo);

/*
 *      check_new_reservations - check for new reservations and handle them
 *                               if we can serve the reservation, we reserve it
 *                               and if we can't, we delete the reservation
 */
int check_new_reservations(status *policy, int pbs_sd, resource_resv **resvs, server_info *sinfo);

/**
 *      confirm_reservation - attempts to confirm a resource reservation
 */
int confirm_reservation(status *policy, int pbs_sd, resource_resv *nresv, server_info *sinfo);

/**
 *      checks that vnodes associated to a reservation that are not
 *                               available
 */
int check_vnodes_unavailable(resource_resv *resv);

/**
 * Release reousrces allocated to a reservation
 */
void release_nodes(resource_resv *resc_resv);

/*
 *	create_resv_nodes - create a node universe for a reservation
 */
node_info **create_resv_nodes(nspec **nspec_arr, server_info *sinfo);

/*
 *	release_running_resv_nodes - adjust nodes resources for reservations that
 *				  that are being altered or are degraded.
 */
void release_running_resv_nodes(resource_resv *resv, server_info *sinfo);

/* Reduce resv_nodes of a reservation on pbs_ralter -lselect for running jobs */
int ralter_reduce_chunks(resource_resv *resv);

/* Will we try and confirm this reservation in this cycle */
int will_confirm(resource_resv *resv, time_t server_time);

/* wrapper for pbs_confirmresv */
int send_confirmresv(int virtual_sd, resource_resv *resv, const char *location, unsigned long start, const char *extend);

/* wrapper for pbs_statresv */
struct batch_status *send_statresv(int virtual_fd, char *id, struct attrl *attrib, char *extend);

/* Update jobs and nodes for resv */
void modify_jobs_nodes_for_resv(resource_resv *resresv, time_t server_time);

#endif /* _RESV_INFO_H */
