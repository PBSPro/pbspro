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

#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <ctype.h>
#include <memory.h>
#ifndef NDEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <pbs_ifl.h>
#include "pbs_internal.h"
#include "log.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "pbs_error.h"


/**
 * @file	attr_resc_func.c
 * @brief
 * This file contains functions for decoding "nodes" and "select" resources
 *
 * The prototypes are declared in "attribute.h", also see resource.h
 *
 * ----------------------------------------------------------------------------
 * Attribute functions for attributes with value type resource
 * ----------------------------------------------------------------------------
 */


/**
 * @brief
 * 	decode_nodes - decode a node requirement specification,
 *	Check if node requirement specification is syntactically ok,
 *	then call decode_str()
 *
 *	val if of the form:	node_spec[+node_spec...]
 *	where node_spec is:	number | properity | number:properity
 *
 * @param[out] patr - pointer to attribute structure
 * @param[in] name - attribute name
 * @param[in] rescn - resource name
 * @param[in] val - attribute value
 *
 * @return	int
 * @retval	0	success
 * @retval	>0	error
 *
 */

int
decode_nodes(struct attribute *patr, char *name, char *rescn, char *val)
{
	char *pc;

	pc = val;

	if ((pc == NULL) || (*pc == '\0'))  /* effectively unsetting value */
		return (decode_str(patr, name, rescn, val));

	while (1) {
		while (isspace((int)*pc))
			++pc;

		if (! isalnum((int)*pc))
			return (PBSE_BADATVAL);
		if (isdigit((int)*pc)) {
			while (isalnum((int)*++pc)) ;
			if (*pc == '\0')
				break;
			else if ((*pc != '+') && (*pc != ':') && (*pc != '#'))
				return (PBSE_BADATVAL);
		} else if (isalpha((int)*pc)) {
			while (isalnum((int)*++pc) || *pc == '-' || *pc == '.' || *pc == '=' || *pc == '_');
			if (*pc  == '\0')
				break;
			else if ((*pc != '+') && (*pc != ':') && (*pc != '#'))
				return (PBSE_BADATVAL);
		}
		++pc;
	}
	return (decode_str(patr, name, rescn, val));
}


/**
 * @brief
 * 	decode_select - decode a selection specification,
 *	Check if the specification is syntactically ok, then call decode_str()
 *
 *	Spec is of the form:
 *
 * @param[out] patr - pointer to attribute structure
 * @param[in] name - attribute name
 * @param[in] rescn - resource name
 * @param[in] val - attribute value
 *
 * @return      int
 * @retval      0       success
 * @retval      >0      error
 *
 */

int
decode_select(struct attribute *patr, char *name, char *rescn, char *val)
{
	int   new_chunk = 1;
	char *pc;
	char *quoted = NULL;

	if (val == NULL)
		return (PBSE_BADATVAL);
	pc = val;
	/* skip leading white space */
	while (isspace((int)*pc))
		++pc;

	if (*pc == '\0')
		return (PBSE_BADATVAL);

	while (*pc) {

		/* each chunk must start with number or letter */
		if (! isalnum((int)*pc))
			return (PBSE_BADATVAL);

		if (new_chunk && isdigit((int)*pc)) {
			/* if digit, it is chunk multipler */
			while (isdigit((int)*++pc)) ;
			if (*pc == '\0')	/* just number is ok */
				return (decode_str(patr, name, rescn, val));
			else if (*pc == '+') {
				++pc;
				if (*pc == '\0')
					return (PBSE_BADATVAL);
				continue;
			} else if (*pc != ':')
				return (PBSE_BADATVAL);
			++pc;
			/* a colon must be followed by a resource=value */
		}

		/* resource=value pairs */
		new_chunk = 0;

		/* resource first and must start with alpha */
		if (! isalpha((int)*pc))
			return (PBSE_BADATVAL);

		while (isalnum((int)*pc) || *pc == '-' || *pc == '_')
			++pc;
		if (*pc != '=')
			return (PBSE_BADATVAL);

		++pc;	/* what following the '=' */
		if (*pc == '\0')
			return (PBSE_BADATVAL);

		/* next comes the value substring */

		while (*pc) {

			/* is it a quoted substring ? */
			if (*pc == '\'' || *pc == '"') {
				/* quoted substring, goto close quote */
				quoted = pc;
				while (*++pc) {
					if (*pc == *quoted) {
						quoted = NULL;
						break;
					}
				}
				if (quoted != NULL) /* didn't find close */
					return (PBSE_BADATVAL);
				++pc;
				continue;
			}

			if (*pc == '\0') {
				/* valid end of string */
				return (decode_str(patr, name, rescn, val));

			} else if (*pc == ':') {
				/* should start new resource=value */
				++pc;
				if (*pc)
					break;
				else
					return (PBSE_BADATVAL);
			} else if (*pc == '+') {
				/* should start new chunk */
				++pc;
				new_chunk = 1;
				if (*pc)
					break;	/* end of chunk, next */
				else
					return (PBSE_BADATVAL);

			} if  (isprint((int)*pc)) {
				++pc;	/* legal character */

			} else
				return (PBSE_BADATVAL);
		}
	}
	return (decode_str(patr, name, rescn, val));
}

/**
 * @brief Verification of resource name
 *
 * A custom resource must start with an alpha character,
 * must be followed by alphanumeric characters excluding '_' and '-'
 *
 * @param[in] name of the resource
 *
 * @retval -1 if resource name does not start with an alpha character
 * @retval -2 if resource name, past first character does not follow the
 * required format
 * @retval 0 if resource name matches required format
 */
int
verify_resc_name(char *name)
{

	char *val;

	if (!isalpha((int)*name)) {
		return -1;
	}

	val = name;

	while (*++val) {
		if ( !isalnum((int)*val) && (*val != '_') &&
			(*val != '-') ) {
			return -2;
		}
	}

	return 0;
}


/**
 * @brief Verification of type and flag values
 *
 * @param[in] resc_type - The resource type
 * @param[in] pflag_ir - The invisible and read-only flags
 * @param[in][out] presc_flag - Pointer to the resource flags
 * @param[in] rescname - The name of the resource
 * @param[in][out] buf - A buffer to hold error message if any
 * @param[in] autocorrect - If possible, fix inconsistencies in types and flags
 * @retval 0 on success
 * @retval -1 on error
 * @retval -2 when errors that got autocorrected
 */
int
verify_resc_type_and_flags(int resc_type, int *pflag_ir, int *presc_flag, char *rescname, char *buf, int buflen, int autocorrect)
{
	char fchar;
	int correction = 0;

	if (*pflag_ir == 2) { /* both flag i and r are set */
		if (autocorrect) {
			snprintf(buf, buflen, "Erroneous to have flag "
			"'i' and 'r' on resource \"%s\"; ignoring 'r' flag.",
			rescname);
			correction = 1;
		}
		else {
			snprintf(buf, buflen, "Erroneous to have flag "
			"'i' and 'r' on resource \"%s\".",
			rescname);
			return -1;
		}
	}
	*pflag_ir = 0;
	if ((*presc_flag & (ATR_DFLAG_FNASSN | ATR_DFLAG_ANASSN)) &&
		((*presc_flag & ATR_DFLAG_CVTSLT) == 0)) {
		if (*presc_flag & ATR_DFLAG_ANASSN)
			fchar = 'n';
		else
			fchar = 'f';
		if (autocorrect) {
			snprintf(buf, buflen, "Erroneous to have flag '%c' without "
				"'h' on resource \"%s\"; adding 'h' flag.",
				fchar, rescname);
			correction = 1;
		}
		else {
			snprintf(buf, buflen, "Erroneous to have flag '%c' without "
				"'h' on resource \"%s\".",
				fchar, rescname);
			return -1;
		}
	}

	if ((*presc_flag & (ATR_DFLAG_FNASSN | ATR_DFLAG_ANASSN)) ==
		(ATR_DFLAG_FNASSN | ATR_DFLAG_ANASSN)) {
		*presc_flag &= ~ATR_DFLAG_FNASSN;
		if (autocorrect) {
			snprintf(buf, buflen, "Erroneous to have flag 'n' and 'f' "
				"on resource \"%s\"; ignoring 'f' flag.", rescname);
			correction = 1;
		}
		else {
			snprintf(buf, buflen, "Erroneous to have flag 'n' and 'f' "
				"on resource \"%s\".", rescname);
			return -1;
		}
	}

	if (((resc_type == ATR_TYPE_BOOL) || (resc_type == ATR_TYPE_STR) || (resc_type == ATR_TYPE_ARST)) &&
		((*presc_flag & (ATR_DFLAG_RASSN | ATR_DFLAG_FNASSN | ATR_DFLAG_ANASSN)) != 0)) {
		*presc_flag &= ~(ATR_DFLAG_RASSN | ATR_DFLAG_FNASSN | ATR_DFLAG_ANASSN);
		if (autocorrect) {
			snprintf(buf, buflen, "Erroneous to have flag 'n', 'f', "
				"or 'q' on resource \"%s\" which is type string, "
				"string_array, or boolean; ignoring those flags.", rescname);
			correction = 1;
		}
		else {
			snprintf(buf, buflen, "Erroneous to have flag 'n', 'f', "
				"or 'q' on resource \"%s\" which is type string, "
				"string_array, or boolean.", rescname);
			return -1;
		}
	}

	if (autocorrect && correction)
		return -2;

	return 0;
}

/**
 * @brief parse type expression associated to the definition of a new resource
 *
 * @param[in] val - The value associated to the resource
 * @param[out] resc_type_p - resource type
 *
 * @retval 0 on success
 * @retval -1 on error
 */
int
parse_resc_type(char *val, int *resc_type_p)
{
	struct resc_type_map *p_resc_type_map;

	p_resc_type_map = find_resc_type_map_by_typest(val);
	if (p_resc_type_map == NULL)
		return -1;
	*resc_type_p = p_resc_type_map->rtm_type;

	return 0;
}

/**
 * @brief parse flags expression associated to the definition of a new resource
 *
 * @param[in] val - The value associated to the resource
 * @param[out] flag_ir_p - invisible and read-only flags
 * @param[out] resc_flag_p - resource flags
 *
 * @retval 0 on success
 * @retval -1 on error;
 */
int
parse_resc_flags(char *val, int *flag_ir_p, int *resc_flag_p)
{
	int resc_flag = READ_WRITE;
	int flag_ir = 0;

	if ((val == NULL) || (flag_ir_p == NULL) || (resc_flag_p == NULL))
		return -1;

	while (*val) {
		if (*val == 'q')
			resc_flag |= ATR_DFLAG_RASSN;
		else if (*val == 'f')
			resc_flag |= ATR_DFLAG_FNASSN;
		else if (*val == 'n')
			resc_flag |= ATR_DFLAG_ANASSN;
		else if (*val == 'h')
			resc_flag |= ATR_DFLAG_CVTSLT;
		else if (*val == 'm')
			resc_flag |= ATR_DFLAG_MOM;
		else if (*val == 'r') {
			if (flag_ir == 0) {
				resc_flag &= ~READ_WRITE;
				resc_flag |= NO_USER_SET;
			}
			flag_ir++;
		}
		else if (*val == 'i') {
			resc_flag &= ~READ_WRITE;
			resc_flag |= ATR_DFLAG_OPRD |
			ATR_DFLAG_OPWR |
			ATR_DFLAG_MGRD | ATR_DFLAG_MGWR;
			flag_ir++;
		}
		else
			return -1;
		val++;
	}
	*flag_ir_p = flag_ir;
	*resc_flag_p = resc_flag;
	return 0;
}
