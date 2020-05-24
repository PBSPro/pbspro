/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
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

/** @file	int_status.c
 * @brief
 * The function that underlies all the status requests
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include "libpbs.h"


static struct batch_status *alloc_bs();

/**
 * @brief
 *	-wrapper function for PBSD_status_put which sends
 *	status batch request
 *
 * @param[in] c - socket descriptor
 * @param[in] function - request type
 * @param[in] id - object id
 * @param[in] attrib - pointer to attribute list
 * @param[in] extend - extention string for req encode
 *
 * @return	structure handle
 * @retval 	pointer to batch status on SUCCESS
 * @retval 	NULL on failure
 *
 */
struct batch_status *
PBSD_status(int c, int function, char *objid, struct attrl *attrib, char *extend)
{
	int rc;
	struct batch_status *PBSD_status_get(int c);

	/* send the status request */

	if (objid == NULL)
		objid = "";	/* set to null string for encoding */

	rc = PBSD_status_put(c, function, objid, attrib, extend, PROT_TCP, NULL);
	if (rc) {
		return NULL;
	}

	/* get the status reply */
	return (PBSD_status_get(c));
}

/**
 * @brief
 *	Returns pointer to status record
 *
 * @param[in] c - index into connection table
 *
 * @return returns a pointer to a batch_status structure
 * @retval pointer to batch status on SUCCESS
 * @retval NULL on failure
 */
struct batch_status *
PBSD_status_get(int c)
{
	struct brp_cmdstat  *stp; /* pointer to a returned status record */
	struct batch_status *bsp  = NULL;
	struct batch_status *rbsp = NULL;
	struct batch_reply  *reply;
	int i;

	/* read reply from stream into presentation element */

	reply = PBSD_rdrpy(c);
	if (reply == NULL) {
		pbs_errno = PBSE_PROTOCOL;
	} else if (reply->brp_choice != BATCH_REPLY_CHOICE_NULL  &&
		reply->brp_choice != BATCH_REPLY_CHOICE_Text &&
		reply->brp_choice != BATCH_REPLY_CHOICE_Status) {
		pbs_errno = PBSE_PROTOCOL;
	} else if (get_conn_errno(c) == 0) {
		/* have zero or more attrl structs to decode here */
		stp = reply->brp_un.brp_statc;
		i = 0;
		pbs_errno = 0;
		while (stp != NULL) {
			if (i++ == 0) {
				rbsp = bsp = alloc_bs();
				if (bsp == NULL) {
					pbs_errno = PBSE_SYSTEM;
					break;
				}
			} else {
				bsp->next = alloc_bs();
				bsp = bsp->next;
				if (bsp == NULL) {
					pbs_errno = PBSE_SYSTEM;
					break;
				}
			}
			if ((bsp->name = strdup(stp->brp_objname)) == NULL) {
				pbs_errno = PBSE_SYSTEM;
				break;
			}
			bsp->attribs = stp->brp_attrl;
			if (stp->brp_attrl)
				stp->brp_attrl = 0;
			bsp->next = NULL;
			stp = stp->brp_stlink;
		}
		if (pbs_errno) {
			pbs_statfree(rbsp);
			rbsp = NULL;
		}
	}
	PBSD_FreeReply(reply);
	return rbsp;
}

/**
 * @brief
 *	Allocate a batch status reply structure
 */
static struct batch_status *
alloc_bs()
{
	struct batch_status *bsp;

	bsp = (struct batch_status *)malloc(sizeof(struct batch_status));
	if (bsp) {

		bsp->next = NULL;
		bsp->name = NULL;
		bsp->attribs = NULL;
		bsp->text = NULL;
	}
	return bsp;
}
