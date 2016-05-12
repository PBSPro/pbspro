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
#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <ctype.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pbs_ifl.h>
#include <errno.h>
#include "list_link.h"
#include "attribute.h"
#include "pbs_error.h"
#include "pbs_share.h"


/*
 * This file contains functions for manipulating attributes of type
 *	size, which is an integer optionally followed by k,K,m,M,g,
 *	G,t, or T, optionally followed by w,W,b,B.
 *	If 'w' or 'W' is not specified, b for bytes is assumed.
 *
 * The attribute has functions for:
 *	Decoding the value string to the machine representation.
 *	Encoding the internal attribute to external form
 *	Setting the value by =, + or - operators.
 *	Comparing a (decoded) value with the attribute value.
 *
 * Some or all of the functions for an attribute type may be shared with
 * other attribute types.
 *
 * The prototypes are declared in "attribute.h"
 *
 * --------------------------------------------------
 * The Set of Attribute Functions for attributes with
 * value type "size"
 * --------------------------------------------------
 */


/**
 * @brief
 * 	decode_size - decode size into attribute structure
 *
 * @param[in] patr - ptr to attribute to decode
 * @param[in] name - attribute name
 * @param[in] rescn - resource name or null
 * @param[out] val - string holding values for attribute structure
 *
 * @retval      int
 * @retval      0       if ok
 * @retval      >0      error number1 if error,
 * @retval      *patr   members set
 *
 */

int
decode_size(struct attribute *patr, char *name, char *rescn, char *val)
{
	int to_size(char *, struct size_value *);

	patr->at_val.at_size.atsv_num   = 0;
	patr->at_val.at_size.atsv_shift = 0;
	if ((val != (char *)0) && (strlen(val) != 0)) {
		errno = 0;
		if (to_size(val, &patr->at_val.at_size) != 0)
			return (PBSE_BADATVAL);
		if (errno != 0)
			return (PBSE_BADATVAL);
		patr->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
	} else {
		patr->at_flags = (patr->at_flags & ~ATR_VFLAG_SET) |
			(ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE);
	}
	return (0);
}

/**
 * @brief
 * 	encode_size - encode attribute of type size into attr_extern
 *
 * @param[in] attr - ptr to attribute to encode
 * @param[in] phead - ptr to head of attrlist list
 * @param[in] atname - attribute name
 * @param[in] rsname - resource name or null
 * @param[in] mode - encode mode
 * @param[out] rtnl - ptr to svrattrl
 *
 * @retval      int
 * @retval      >0      if ok, entry created and linked into list
 * @retval      =0      no value to encode, entry not created
 * @retval      -1      if error
 *
 */

/*ARGSUSED*/

#define CVNBUFSZ 23

int
encode_size(attribute *attr, pbs_list_head *phead, char *atname, char *rsname, int mode, svrattrl **rtnl)
{
	size_t	     ct;
	char	     cvnbuf[CVNBUFSZ];
	svrattrl *pal;
	void from_size(struct size_value *, char *);

	if (!attr)
		return (-1);
	if (!(attr->at_flags & ATR_VFLAG_SET))
		return (0);

	from_size(&attr->at_val.at_size, cvnbuf);
	ct = strlen(cvnbuf) + 1;

	pal = attrlist_create(atname, rsname, ct);
	if (pal == (svrattrl *)0)
		return (-1);

	(void)memcpy(pal->al_value, cvnbuf, ct);
	pal->al_flags = attr->at_flags;
	if (phead)
		append_link(phead, &pal->al_link, pal);
	if (rtnl)
		*rtnl = pal;

	return (1);
}

/*
 * set_size - set attribute A to attribute B,
 *	either A=B, A += B, or A -= B
 *
 * @param[in]   attr - pointer to new attribute to be set (A)
 * @param[in]   new  - pointer to attribute (B)
 * @param[in]   op   - operator
 *
 * @return      int
 * @retval      0       if ok
 * @retval     >0       if error
 *
 */

int
set_size(struct attribute *attr, struct attribute *new, enum batch_op op)
{
	u_Long		  old;
	struct size_value tmpa;	/* the two temps are used to insure that the */
	struct size_value tmpn;	/* real attributes are not changed if error  */
	int normalize_size(struct size_value *a, struct size_value *b,
		struct size_value *c, struct size_value *d);

	assert(attr && new && (new->at_flags & ATR_VFLAG_SET));

	if (op == INCR) {
		if (((attr->at_flags & ATR_VFLAG_SET) == 0) ||
			((attr->at_val.at_size.atsv_num == 0)))
			op = SET;  /* if adding to null, just set instead */
	}

	switch (op) {
		case SET:	attr->at_val.at_size.atsv_num   = new->at_val.at_size.atsv_num;
			attr->at_val.at_size.atsv_shift = new->at_val.at_size.atsv_shift;
			attr->at_val.at_size.atsv_units = new->at_val.at_size.atsv_units;
			break;

		case INCR:	if (normalize_size(&attr->at_val.at_size,
				&new->at_val.at_size, &tmpa, &tmpn) < 0)
				return (PBSE_BADATVAL);
			old  = tmpa.atsv_num;
			tmpa.atsv_num += tmpn.atsv_num;
			if (tmpa.atsv_num < old)
				return (PBSE_BADATVAL);
			attr->at_val.at_size = tmpa;
			break;

		case DECR:	if (normalize_size(&attr->at_val.at_size,
				&new->at_val.at_size, &tmpa, &tmpn) < 0)
				return (PBSE_BADATVAL);
			old  = tmpa.atsv_num;
			tmpa.atsv_num -= tmpn.atsv_num;
			if (tmpa.atsv_num > old)
				return (PBSE_BADATVAL);
			attr->at_val.at_size = tmpa;
			break;

		default:	return (PBSE_INTERNAL);
	}
	attr->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;

	return (0);
}

/*
 * comp_size - compare two attributes of type size
 *
 * @param[in] attr - pointer to attribute structure
 * @param[in] with - pointer to attribute structure
 *
 * @return      int
 * @retval      0	if 1st == 2nd 
 * @retval      1	if 1st > 2nd
 * @retval	-1 	if 1st < 2nd
 *
 */

int
comp_size(struct attribute *attr, struct attribute *with)
{
	struct size_value tmpa;
	struct size_value tmpw;
	int normalize_size(struct size_value *a, struct size_value *b,
		struct size_value *c, struct size_value *d);

	if (normalize_size(&attr->at_val.at_size, &with->at_val.at_size,
		&tmpa, &tmpw) != 0) {
		if (tmpa.atsv_shift >
			tmpw.atsv_shift)
			return (1);
		else if (tmpa.atsv_shift <
			tmpw.atsv_shift)
			return (-1);
		else
			return (0);
	} else if (tmpa.atsv_num > tmpw.atsv_num)
		return (1);
	else if (tmpa.atsv_num < tmpw.atsv_num)
		return (-1);
	else
		return (0);

}

/*
 * free_size - use free_null to (not) free space
 */

/**
 * @brief
 * 	normalize_size - normalize two size values, adjust so the shift
 *	counts are the same, but not less than 10 (KB) otherwise a
 *	chance for overflow.
 *
 * param[in] a - pointer to size_value structure
 * param[in] b -  pointer to size_value structure
 * param[in] ta -  pointer to size_value structure
 * param[in] tb -  pointer to size_value structure
 *
 */

int
normalize_size(struct size_value *a, struct size_value *b, struct size_value *ta, struct size_value *tb)
{
	int	adj;
	u_Long	temp;

	/*
	 * we do the work in copies of the original attributes
	 * to preserve the original (in case of error)
	 */
	*ta = *a;
	*tb = *b;

	/* if either unit is in bytes (vs words), then both must be */

	if ((ta->atsv_units == ATR_SV_WORDSZ) &&
		(tb->atsv_units != ATR_SV_WORDSZ)) {
		ta->atsv_num *= SIZEOF_WORD;
		ta->atsv_units = ATR_SV_BYTESZ;
	} else if ((ta->atsv_units != ATR_SV_WORDSZ) &&
		(tb->atsv_units == ATR_SV_WORDSZ)) {
		tb->atsv_num *= SIZEOF_WORD;
		tb->atsv_units = ATR_SV_BYTESZ;
	}

	/* if either value is in units, round it up to kilos */
	if (ta->atsv_shift == 0) {
		ta->atsv_num = (ta->atsv_num + 1023) >> 10;
		ta->atsv_shift = 10;
	}
	if (tb->atsv_shift == 0) {
		tb->atsv_num = (tb->atsv_num + 1023) >> 10;
		tb->atsv_shift = 10;
	}

	adj = ta->atsv_shift - tb->atsv_shift;

	if (adj > 0) {
		temp = ta->atsv_num;
		if ((adj > sizeof(u_Long) * 8) ||
			(((temp << adj) >> adj) != ta->atsv_num))
			return (-1);	/* would overflow */
		ta->atsv_shift = tb->atsv_shift;
		ta->atsv_num   = ta->atsv_num << adj;
	} else if (adj < 0) {
		adj = -adj;
		temp = tb->atsv_num;
		if ((adj > sizeof(u_Long) * 8) ||
			(((temp << adj) >> adj) != tb->atsv_num))
			return (-1);	/* would overflow */
		tb->atsv_shift = ta->atsv_shift;
		tb->atsv_num   = tb->atsv_num << adj;
	}
	return (0);
}


/**
 * @brief 
 *	Decode the value string into a size_value structure.
 *
 * @param[in] val - String containing the text to convert.
 * @param[out] psize - The size_value structure for the decoded value.
 *
 * @return - int
 * @retval - 0 - Success
 * @retval - !=0 - Failure
 *
 */

int
to_size(char *val, struct size_value *psize)
{
	int   havebw = 0;
	char *pc;

	if ((val == NULL) || (psize == NULL))
		return (PBSE_BADATVAL);

	psize->atsv_units = ATR_SV_BYTESZ;
	psize->atsv_num = strTouL(val, &pc, 10);
	if (pc == val)		/* no numeric part */
		return (PBSE_BADATVAL);

	switch (*pc) {
		case '\0':	break;
		case 'k':
		case 'K':	psize->atsv_shift = 10;
			break;
		case 'm':
		case 'M':	psize->atsv_shift = 20;
			break;
		case 'g':
		case 'G':	psize->atsv_shift = 30;
			break;
		case 't':
		case 'T':	psize->atsv_shift = 40;
			break;
		case 'p':
		case 'P':	psize->atsv_shift = 50;
			break;
		case 'b':
		case 'B':	havebw = 1;
			break;
		case 'w':
		case 'W':	havebw = 1;
			psize->atsv_units = ATR_SV_WORDSZ;
			break;
		default:	return (PBSE_BADATVAL);	 /* invalid string */
	}
	if (*pc != '\0')
		pc++;
	if (*pc != '\0') {
		if (havebw)
			return (PBSE_BADATVAL);  /* invalid string */
		switch (*pc) {
			case 'b':
			case 'B':	break;
			case 'w':
			case 'W':	psize->atsv_units = ATR_SV_WORDSZ;
				break;
			default:	return (PBSE_BADATVAL);
		}
		pc++;
	}
	/* Make sure we reached the end of the size specification. */
	if (*pc != '\0')
		return (PBSE_BADATVAL);  /* invalid string */
	return (0);
}

/**
 * @brief
 * 	from_size - encode a string FROM a size_value structure
 *
 * @param[in] psize - pointer to size_value structure
 * @param[out] cvnbuf - buffer to hold size_value info
 *
 * @return	Void
 *
 */

void
from_size(struct size_value *psize, char *cvnbuf)
{

#ifdef WIN32
	(void)sprintf(cvnbuf, "%I64u", psize->atsv_num);
#else
	(void)sprintf(cvnbuf, "%llu", psize->atsv_num);
#endif

	switch (psize->atsv_shift) {
		case  0:	break;
		case 10:	strcat(cvnbuf, "k");
			break;
		case 20:	strcat(cvnbuf, "m");
			break;
		case 30:	strcat(cvnbuf, "g");
			break;
		case 40:	strcat(cvnbuf, "t");
			break;
		case 50:	strcat(cvnbuf, "p");
	}
	if (psize->atsv_units & ATR_SV_WORDSZ)
		strcat(cvnbuf, "w");
	else
		strcat(cvnbuf, "b");
}

/**
 * @brief
 * 	get_kilobytes - return the size in the number of kilobytes from
 *	a "size" type attribute.  A value saved in bytes/words is rounded up.
 *	If the value is not set, or the attriute is not type "size", then
 *	zero is returned.
 *
 * @param[in] attr - pointer to attribute structure
 *
 * @return	u_Long
 * @retval	0	Error
 * @retval	val 	kilobytes
 *
 */
u_Long
get_kilobytes_from_attr(struct attribute *attr)
{
	u_Long val;

	if (!attr || !(attr->at_flags & ATR_VFLAG_SET) ||
		attr->at_type != ATR_TYPE_SIZE)
		return (0);

	val = attr->at_val.at_size.atsv_num;
	if (attr->at_val.at_size.atsv_units == ATR_SV_WORDSZ)
		val *= SIZEOF_WORD;
	if (attr->at_val.at_size.atsv_shift == 0)
		val = (val + 1023) >> 10;
	else
		val = val << (attr->at_val.at_size.atsv_shift - 10);
	return val;
}

/**
 * @brief  Return the size in the number of bytes from
 *	a "size" type attribute.  A value saved in bytes/words is rounded up.
 *	If the value is not set, or the attriute is not type "size", then
 *	zero is returned.
 *
 * @param[in] attr- server attributes 
 *
 * @return - size in bytes
 *
 */
u_Long
get_bytes_from_attr(struct attribute *attr)
{
	u_Long val;

	if (!attr || !(attr->at_flags & ATR_VFLAG_SET) ||
		attr->at_type != ATR_TYPE_SIZE)
		return (0);

	val = attr->at_val.at_size.atsv_num;
	if (attr->at_val.at_size.atsv_units == ATR_SV_WORDSZ)
		val *= SIZEOF_WORD;
	if (attr->at_val.at_size.atsv_shift != 0)
		val = val << (attr->at_val.at_size.atsv_shift);
	return val;
}
