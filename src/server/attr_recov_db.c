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

#include <pbs_config.h>   /* the master config generated by configure */

#include "pbs_ifl.h"
#include <assert.h>
#include <errno.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "list_link.h"
#include "attribute.h"
#include "log.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "resource.h"
#include "pbs_db.h"
#include <openssl/sha.h>


/* Global Variables */
extern int resc_access_perm;

extern struct attribute_def	svr_attr_def[];
extern struct attribute_def	que_attr_def[];


/**
 * @brief
 *	Compute and check whether the quick save area has been modified
 *
 * @param[in] 	    qs       - pointer to the quick save area
 * @param[in]	    len      - length of the quick save area
 * @param[in/out]   oldhash  - pointer to a opaque value of current quick save area signature/hash
 *
 * @return      Error code
 * @retval	 0 - quick save area was not changed
 * @retval	 1 - quick save area has changed
 */
int
compare_obj_hash(void *qs, int len, void *oldhash)
{
	char hash[DIGEST_LENGTH];

	if (SHA1((const unsigned char *) qs, len, (unsigned char *) &hash) == NULL)
		exit(-1);

	if (memcmp(hash, oldhash, DIGEST_LENGTH) != 0) {
		memcpy(oldhash, hash, DIGEST_LENGTH); /* update the signature */
		return 1;
	}

	return 0; /* qs was not modified */
}

/**
 * @brief
 *	Encode a single  attribute to the database structure of type pbs_db_attr_list_t
 *
 * @param[in]	padef - Address of parent's attribute definition array
 * @param[in]	pattr - Address of the parent objects attribute array
 * @param[out]	db_attr_list - pointer to the structure of type pbs_db_attr_list_t for storing in DB
 *
 * @return  error code
 * @retval   -1 - Failure
 * @retval    0 - Success
 *
 */
int
encode_single_attr_db(attribute_def *padef, attribute *pattr, pbs_db_attr_list_t *db_attr_list)
{
	pbs_list_head *lhead;
	int rc = 0;

	lhead = &db_attr_list->attrs;

	rc = padef->at_encode(pattr, lhead, padef->at_name, NULL, ATR_ENCODE_DB, NULL);
	if (rc < 0)
		return -1;

	db_attr_list->attr_count += rc;

	return 0;
}

/**
 * @brief
 *	Encode the given attributes to the database structure of type pbs_db_attr_list_t
 *
 * @param[in]	padef - Address of parent's attribute definition array
 * @param[in]	pattr - Address of the parent objects attribute array
 * @param[in]	numattr - Number of attributes in the list
 * @param[in]	all  - Encode all attributes
 *
 * @return  error code
 * @retval   -1 - Failure
 * @retval    0 - Success
 *
 */
int
encode_attr_db(attribute_def *padef, attribute *pattr, int numattr, pbs_db_attr_list_t *db_attr_list, int all)
{
	int i;

	db_attr_list->attr_count = 0;

	CLEAR_HEAD(db_attr_list->attrs);

	for (i = 0; i < numattr; i++) {
		if (!((pattr + i)->at_flags & ATR_VFLAG_MODIFY))
			continue;

		if ((((padef + i)->at_flags & ATR_DFLAG_NOSAVM) == 0) || all) {
			if (encode_single_attr_db((padef + i), (pattr + i), db_attr_list) != 0)
				return -1;

			(pattr+i)->at_flags &= ~ATR_VFLAG_MODIFY;
		}
	}
	return 0;
}

/**
 * @brief
 *	Decode the list of attributes from the database to the regular attribute structure
 *
 * @param[in]	  parent - pointer to parent object
 * @param[in]	  db_attr_list - Information about the database attributes
 * @param[in]     padef_idx - Search index of this attribute array
 * @param[in]	  padef - Address of parent's attribute definition array
 * @param[in/out] pattr - Address of the parent objects attribute array
 * @param[in]	  limit - Number of attributes in the list
 * @param[in]	  unknown	- The index of the unknown attribute if any
 *
 * @return      Error code
 * @retval	 0  - Success
 * @retval	-1  - Failure
 *
 *
 */
int
decode_attr_db(void *parent, pbs_db_attr_list_t *db_attr_list, void *padef_idx, attribute_def *padef, attribute *pattr, int limit, int unknown)
{
	int index;
	svrattrl *pal = (svrattrl *)0;
	svrattrl *tmp_pal = (svrattrl *)0;
	void **palarray = NULL;
	pbs_list_head *attr_list;

	if ((palarray = calloc(limit, sizeof(void *))) == NULL) {
		log_err(-1, __func__, "Out of memory");
		return -1;
	}

	/* set all privileges (read and write) for decoding resources	*/
	/* This is a special (kludge) flag for the recovery case, see	*/
	/* decode_resc() in lib/Libattr/attr_fn_resc.c			*/

	resc_access_perm = ATR_DFLAG_ACCESS;

	attr_list = &db_attr_list->attrs;

	for (pal = (svrattrl *) GET_NEXT(*attr_list); pal != NULL; pal = (svrattrl *) GET_NEXT(pal->al_link)) {
		/* find the attribute definition based on the name */
		index = find_attr(padef_idx, padef, pal->al_name);
		if (index < 0) {

			/*
			* There are two ways this could happen:
			* 1. if the (job) attribute is in the "unknown" list -
			*    keep it there;
			* 2. if the server was rebuilt and an attribute was
			*    deleted, -  the fact is logged and the attribute
			*    is discarded (system,queue) or kept (job)
			*/
			if (unknown > 0) {
				index = unknown;
			} else {
				snprintf(log_buffer,LOG_BUF_SIZE, "unknown attribute \"%s\" discarded", pal->al_name);
				log_err(-1, __func__, log_buffer);
				(void)free(pal);
				continue;
			}
		}
		if (palarray[index] == NULL)
			palarray[index] = pal;
		else {
			tmp_pal = palarray[index];
			while (tmp_pal->al_sister)
				tmp_pal = tmp_pal->al_sister;

			/* this is the end of the list of attributes */
			tmp_pal->al_sister = pal;
		}
	}


	/* now do the decoding */
	for (index = 0; index < limit; index++) {
		/*
		 * In the normal case we just decode the attribute directly
		 * into the real attribute since there will be one entry only
		 * for that attribute.
		 *
		 * However, "entity limits" are special and may have multiple,
		 * the first of which is "SET" and the following are "INCR".
		 * For the SET case, we do it directly as for the normal attrs.
		 * For the INCR,  we have to decode into a temp attr and then
		 * call set_entity to do the INCR.
		 */
		/*
		 * we don't store the op value into the database, so we need to
		 * determine (in case of an ENTITY) whether it is the first
		 * value, or was decoded before. We decide this based on whether
		 * the flag has ATR_VFLAG_SET
		 *
		 */
		pal = palarray[index];
		while (pal) {
			if ((padef[index].at_type == ATR_TYPE_ENTITY) && is_attr_set(&pattr[index])) {
				/* for INCR case of entity limit, decode locally */
				set_attr_generic(&pattr[index], &padef[index], pal->al_value, pal->al_resc, INCR);
			} else {
				int rc = set_attr_generic(&pattr[index], &padef[index], pal->al_value, pal->al_resc, INTERNAL);
				if (! rc) {
					int act_rc = 0;
					if (padef[index].at_action)
						if ((act_rc = (padef[index].at_action(&pattr[index], parent, ATR_ACTION_RECOV)))) {
							log_errf(act_rc, __func__, "Action function failed for %s attr, errn %d", (padef+index)->at_name, act_rc);
							for ( index++; index <= limit; index++) {
								while (pal) {
									tmp_pal = pal->al_sister;
									free(pal);
									pal = tmp_pal;
								}
								if (index < limit)
									pal = palarray[index];
							}
							free(palarray);
							/* bailing out from this function */
							/* any previously allocated attrs will be */
							/* freed by caller (parent obj recov function) */
							return -1;
						}
				}
			}
			(pattr+index)->at_flags = (pal->al_flags & ~ATR_VFLAG_MODIFY) | ATR_VFLAG_MODCACHE;

			tmp_pal = pal->al_sister;
			pal = tmp_pal;
		}
	}
	(void)free(palarray);

	return 0;
}
