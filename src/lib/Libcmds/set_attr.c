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

/**
 * @file	set_attr.c
 * @brief
 *	Add an entry to an attribute list. First, create the entry and set
 *  the fields. If the attribute list is empty, then just point it at the
 *  new entry. Otherwise, append the new entry to the list.
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include "cmds.h"
#include "attribute.h"


/* static pointer to be used by set_attr_resc */
static struct attrl* new_attr;

/**
 * @brief
 *	Add an entry to an attribute list. First, create the entry and set
 * 	the fields. If the attribute list is empty, then just point it at the
 * 	new entry. Otherwise, append the new entry to the list.
 *
 * @param[in/out] attrib - pointer to attribute list
 * @param[in]     attrib_name - attribute name
 * @param[in]     attrib_value - attribute value
 *
 * @return	error code
 * @return	0	success
 * @return	1	error
 *
 */

int
set_attr(struct attrl **attrib, char *attrib_name, char *attrib_value)
{
	struct attrl *attr, *ap;

	attr = new_attrl();
	if (attr == NULL) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}
	if (attrib_name == NULL)
		attr->name = NULL;
	else {
		attr->name = (char *) malloc(strlen(attrib_name)+1);
		if (attr->name == NULL) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		strcpy(attr->name, attrib_name);
	}
	if (attrib_value == NULL)
		attr->value = NULL;
	else {
		attr->value = (char *) malloc(strlen(attrib_value)+1);
		if (attr->name == NULL) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		strcpy(attr->value, attrib_value);
	}
	new_attr = attr; /* set global var new_attrl in case set_attr_resc want to add resource to it */
	if (*attrib == NULL) {
		*attrib = attr;
	} else {
		ap = *attrib;
		while (ap->next != NULL) ap = ap->next;
		ap->next = attr;
	}

	return 0;
}

/**
 * @brief
 *	wrapper function for set_attr.
 *
 * @param[in/out] attrib - pointer to attribute list
 * @param[in]     attrib_name - attribute name
 * @param[in]     attrib_value - attribute value
 *
 * @return	error code
 * @retval	0	success
 * @retval	1	failure
 */

int
set_attr_resc(struct attrl **attrib, char *attrib_name, char *attrib_resc, char *attrib_value)
{
	if (set_attr(attrib, attrib_name, attrib_value))
		return 1;

	if (attrib_resc != NULL) {
		new_attr->resource = (char *) malloc(strlen(attrib_resc)+1);
		if (new_attr->resource == NULL) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		strcpy(new_attr->resource, attrib_resc);
	}
	return 0;
}
