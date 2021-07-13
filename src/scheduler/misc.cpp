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
 * Miscellaneous functions of scheduler.
 */
#include <pbs_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <sstream>
#include <algorithm>
#include <pbs_ifl.h>
#include <pbs_internal.h>
#include <pbs_error.h>
#include <log.h>
#include <pbs_share.h>
#include <libutil.h>
#include <libpbs.h>
#include "config.h"
#include "constant.h"
#include "misc.h"
#include "globals.h"
#include "fairshare.h"
#include "resource_resv.h"
#include "resource.h"


/**
 * @brief
 *		string_dup - duplicate a string
 *
 * @param[in]	str	-	string to duplicate
 *
 * @return	newly allocated string
 *
 */

char *
string_dup(const char *str)
{
	char *newstr;
	size_t len;

	if (str == NULL)
		return NULL;

	len = strlen(str) + 1;
	if ((newstr = static_cast<char *>(malloc(len))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	pbs_strncpy(newstr, str, len);

	return newstr;
}

/**
 * @brief
 * 		add a string to a string to a string array only if it is unique
 *
 * @param[in,out]	str_arr	-	array of strings of unique values
 * @param[in]	str	-	string to add
 * @return	int
 * @retval	index in array if success	: string is added to array
 * @retval	-1 failure	: string could not be added to the array
 */
int
add_str_to_unique_array(char ***str_arr, char *str)
{
	int ind;
	if (str_arr == NULL || str == NULL)
		return -1;

	ind = find_string_idx(*str_arr, str);
	if (ind >= 0) /* found it! */
		return ind;

	return add_str_to_array(str_arr, str);
}

/**
 * @brief
 * 		add string to string array
 *
 * @param[in]	str_arr	-	pointer to an array of strings to be added to(i.e. char ***)
 * @param[in]	str	-	string to add to array
 *
 * @return	int
 * @retval	index	-	index of string on success
 * @retval	-1	: failure
 */
int
add_str_to_array(char ***str_arr, char *str)
{
	char **tmp_arr;
	int cnt;

	if (str_arr == NULL || str == NULL)
		return -1;

	if (*str_arr == NULL)
		cnt = 0;
	else
		cnt = count_array(*str_arr);

	tmp_arr = static_cast<char **>(realloc(*str_arr, (cnt+2)*sizeof(char*)));
	if (tmp_arr == NULL)
		return -1;

	tmp_arr[cnt] = string_dup(str);
	tmp_arr[cnt+1] = NULL;

	*str_arr = tmp_arr;

	return cnt;
}

/**
 * @brief
 * 		res_to_num - convert a resource string to a numeric sch_resource_t
 *
 * @param[in]	res_str	-	the resource string
 * @param[out]	type	-	the type of the resource
 *
 * @return	sch_resource_t
 * @retval	a number in kilobytes or seconds
 * @retval	0 for False, if type is boolean
 * @retval	1 for True, if type is boolean
 * @retval	SCHD_INFINITY_RES	: if not a number
 *
 */
sch_resource_t
res_to_num(const char *res_str, struct resource_type *type)
{
	sch_resource_t count = SCHD_INFINITY_RES;	/* convert string resource to numeric */
	sch_resource_t count2 = SCHD_INFINITY_RES;	/* convert string resource to numeric */
	char *endp;				/* used for strtol() */
	char *endp2;				/* used for strtol() */
	long multiplier = 1;			/* multiplier to count */
	int is_size = 0;			/* resource value is a size type */
	int is_time = 0;			/* resource value is a time spec */

	if (res_str == NULL)
		return SCHD_INFINITY_RES;

	if (!strcasecmp(ATR_TRUE, res_str)) {
		if (type != NULL) {
			type->is_boolean = 1;
			type->is_non_consumable = 1;
		}
		count = 1;
	}
	else if (!strcasecmp(ATR_FALSE, res_str)) {
		if (type != NULL) {
			type->is_boolean = 1;
			type->is_non_consumable = 1;
		}
		count = 0;
	}
	else if (!is_num(res_str)) {
		if (type != NULL) {
			type->is_string = 1;
			type->is_non_consumable = 1;
		}
		count = SCHD_INFINITY_RES;
	}
	else {
		count = (sch_resource_t) strtod(res_str, &endp);

		if (*endp == ':') { /* time resource -> convert to seconds */
			count2 = (sch_resource_t) strtod(endp+1, &endp2);
			if (*endp2 == ':') { /* form of HH:MM:SS */
				count *= 3600;
				count += count2 * 60;
				count += strtol(endp2 + 1, &endp, 10);
				if (*endp != '\0')
					count = SCHD_INFINITY_RES;
			}
			else			 { /* form of MM:SS */
				count *= 60;
				count += count2;
			}
			multiplier = 1;
			is_time = 1;
		}
		else if (*endp == 'k' || *endp == 'K') {
			multiplier = 1;
			is_size = 1;
		}
		else if (*endp == 'm' || *endp == 'M') {
			multiplier = MEGATOKILO;
			is_size = 1;
		}
		else if (*endp == 'g' || *endp == 'G') {
			multiplier = GIGATOKILO;
			is_size = 1;
		}
		else if (*endp == 't' || *endp == 'T') {
			multiplier = TERATOKILO;
			is_size = 1;
		}
		else if (*endp == 'b' || *endp == 'B') {
			count = ceil(count / KILO);
			multiplier = 1;
			is_size = 1;
		}
		else if (*endp == 'w') {
			count = ceil(count / KILO);
			multiplier = SIZEOF_WORD;
			is_size = 1;
		}
		else	/* catch all */
			multiplier = 1;

		if (*endp != '\0' && *(endp + 1) == 'w')
			multiplier *= SIZEOF_WORD;

		if (type != NULL) {
			type->is_consumable = 1;
			if (is_size)
				type->is_size = 1;
			else if (is_time)
				type->is_time = 1;
			else
				type->is_num = 1;
		}
	}

	return count * multiplier;
}

/**
 * @brief
 *      skip_line - find if the line of the config file needs to be skipped
 *                  due to it being a comment or other means
 *
 * @param[in]	line	-	the line from the config file
 *
 * @return	true:1/false:0
 * @retval	true	: if the line should be skipped
 * @retval	false	: if it should be parsed
 *
 */
int
skip_line(char *line)
{
	int skip = 0;				/* whether or not to skil the line */

	if (line != NULL) {
		while (isspace((int) *line))
			line++;

		/* '#' is comment in config files and '*' is comment in holidays file */
		if (line[0] == '\0' || line[0] == '#' || line[0] == '*')
			skip = 1;
	}

	return skip;
}

/**
 *	@brief  combination of log_event() and translate_fail_code()
 *		If we're actually going to log a message, translate
 *		err into a message and then log it.  The translated
 *		error will be printed after the message
 *
 *	@param[in] event - the event type
 *	@param[in] event_class - the event class
 *	@param[in] sev   - the severity of the log message
 *	@param[in] name  - the name of the object
 *	@param[in] text  - the text of the message
 *			if NULL, only print translated error text
 *	@param[in] err   - schderr error structure to be translated
 *
 *	@return nothing
 */
void
schdlogerr(int event, int event_class, int sev, const std::string& name, const char *text,
	schd_error *err)
{

	if (err == NULL)
		return;

	if (will_log_event(event)) {
		char logbuf[MAX_LOG_SIZE];

		translate_fail_code(err, NULL, logbuf);
		if (text == NULL)
			log_event(event, event_class, sev, name, logbuf);
		else
			log_eventf(event, event_class, sev, name, "%s %s", text, logbuf);
	}
}

/**
 * @brief
 * 	log_eventf - a combination of log_event() and printf()
 *
 * @param[in] eventtype - event type
 * @param[in] objclass - event object class
 * @param[in] sev - indication for whether to syslogging enabled or not
 * @param[in] objname - object name stating log msg related to which object
 * @param[in] fmt - format string
 * @param[in] ... - arguments to format string
 *
 * @return void
 */
void
log_eventf(int eventtype, int objclass, int sev, const std::string& objname, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_log_eventf(eventtype, objclass, sev, objname.c_str(), fmt, args);
	va_end(args);
}

/**
 * @brief
 * 	log_event - log a server event to the log file
 *
 *	Checks to see if the event type is being recorded.  If they are,
 *	pass off to log_record().
 *
 *	The caller should ensure proper formating of the message if "text"
 *	is to contain "continuation lines".
 *
 * @param[in] eventtype - event type
 * @param[in] objclass - event object class
 * @param[in] sev - indication for whether to syslogging enabled or not
 * @param[in] objname - object name stating log msg related to which object
 * @param[in] text - log msg to be logged.
 *
 *	Note, "sev" or severity is used only if syslogging is enabled,
 *	see syslog(3) and log_record.c for details.
 */

void
log_event(int eventtype, int objclass, int sev, const std::string& objname, const char *text)
{
	if (will_log_event(eventtype))
		log_record(eventtype, objclass, sev, objname.c_str(), text);
}

/**
 * @brief
 * 		take a generic NULL terminated pointer array and return a
 *             filtered specialized array based on calling filter_func() on every
 *             member.  This can be used with any standard scheduler array
 *	       like resource_resv or node_info or resdef
 *
 * @param[in] ptrarr	-	the array to filter
 * @param[in] filter_func	-	pointer to a function that will filter the array
 * @param[in] arg	-	an optional arg passed to filter_func
 * @param[in] flags	-	control how ptrs are filtered
 *						FILTER_FULL - leave the filtered array full size
 *
 * @par
 * 	   filter_func prototype: @fn int func( void *, void * )
 *                                           object   arg
 *		  object - specific member of ptrarr[]
 *		  arg    - arg parameter
 *               - returns 1: ptr will be added to filtered array
 *               - returns 0: ptr will NOT be added to filtered array
 *
 * @return	void ** filtered array.
 */
void **
filter_array(void **ptrarr, int (*filter_func)(void*, void*),
	void *arg, int flags)
{
	void **new_arr = NULL;                      /* the filtered array */
	void **tmp;
	int i, j;
	int size;

	if (ptrarr == NULL || filter_func == NULL)
		return NULL;

	size = count_array(ptrarr);

	if ((new_arr = static_cast<void **>(malloc((size + 1) * sizeof(void *)))) == NULL) {
		log_err(errno, __func__, "Error allocating memory");
		return NULL;
	}

	for (i = 0, j = 0; i < size; i++) {
		if (filter_func(ptrarr[i], arg)) {
			new_arr[j] = ptrarr[i];
			j++;
		}
	}
	new_arr[j] = NULL;

	if (!(flags & FILTER_FULL)) {
		if ((tmp = static_cast<void **>(realloc(new_arr, (j+1) * sizeof(void *)))) == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			free(new_arr);
			return NULL;
		}
		new_arr = tmp;
	}
	return new_arr;
}

/**
 * @brief
 * 		match two NULL terminated string arrays
 *
 * @param[in]	strarr	-	the string array to search
 * @param[in]	str	-	the string to find
 *
 * @return	enum match_string_array_ret
 * @retval	SA_FULL_MATCH	: full match
 * @retval	SA_SUB_MATCH		: full match of one array and it is
 *									a subset of the other
 * @retval	SA_PARTIAL_MATCH	: at least one match but not all
 * @retval	SA_NO_MATCH	: no match
 *
 */
enum match_string_array_ret match_string_array(const char * const *strarr1, const char * const *strarr2)
{
	int match = 0;
	int i;
	int strarr2_len;

	if (strarr1 == NULL || strarr2 == NULL)
		return SA_NO_MATCH;

	strarr2_len = count_array(strarr2);

	for (i = 0; strarr1[i] != NULL; i++) {
		if (is_string_in_arr(const_cast<char **>(strarr2), strarr1[i]))
			match++;
	}

	/* i is the length of strarr1 since we just looped through the whole array */
	if (match == i && match == strarr2_len)
		return SA_FULL_MATCH;

	if (match == i || match == strarr2_len)
		return SA_SUB_MATCH;

	if (match)
		return SA_PARTIAL_MATCH;

	return SA_NO_MATCH;
}
// overloaded
enum match_string_array_ret match_string_array(const std::vector<std::string> &strarr1, const std::vector<std::string> &strarr2)
{
	unsigned int match = 0;

	if (strarr1.empty() || strarr2.empty())
		return SA_NO_MATCH;

	for (auto &str1: strarr1) {
		if (std::find(strarr2.begin(), strarr2.end(), str1) != strarr2.end())
			match++;
	}

	/* i is the length of strarr1 since we just looped through the whole array */
	if (match == strarr1.size() && match == strarr2.size())
		return SA_FULL_MATCH;

	if (match == strarr1.size() || match == strarr2.size())
		return SA_SUB_MATCH;

	if (match)
		return SA_PARTIAL_MATCH;

	return SA_NO_MATCH;
}
/**
 * @brief
 * 		convert a string array into a printable string
 *
 * @param[in]	strarr	-	string array to convert
 *
 * @return	converted string stored in local static ptr (no need to free)
 *
 * @par MT-safe:	yes
 *
 */
char *
string_array_to_str(char **strarr)
{
	char *arrbuf = NULL;
	int len = 0;
	int i;

	if (strarr == NULL)
		return NULL;

	if (strarr[0] == NULL)
		return NULL;

	for (i = 0; strarr[i] != NULL; i++)
		len += strlen(strarr[i]);
	len += i; /* added space for the commas */

	arrbuf = static_cast<char *>(malloc(len + 1));
	if (arrbuf == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}
	arrbuf[0] = '\0';

	for (i = 0; strarr[i] != NULL; i++) {
		strcat(arrbuf, strarr[i]);
		strcat(arrbuf, ",");
	}
	arrbuf[strlen(arrbuf) - 1] = '\0';

	return arrbuf;
}

/**
 * @brief
 * 		calc_used_walltime - calculate the  used amount of a resource resv
 *
 * @param[in]	resresv	-	the resource resv to calculate
 *
 * @return	used amount of the resource resv
 * @retval	0	: if resresv starts in the future
 *					or if the resource used for walltime is NULL
 */

time_t
calc_used_walltime(resource_resv *resresv)
{
	time_t used_amount = 0;
	resource_req *used = NULL;

	if (resresv == NULL)
		return 0;

	if (resresv->is_job && resresv->job !=NULL) {
		used = find_resource_req(resresv->job->resused, allres["walltime"]);

		/* If we can't find the used structure, we will just assume no usage */
		if (used == NULL)
			used_amount = 0;
		else
			used_amount = (time_t) used->amount;
	} else {
		if (resresv->server->server_time > resresv->start)
			used_amount = resresv->server->server_time - resresv->start;
		else
			used_amount = 0;
	}
	return used_amount;
}
/**
 * @brief
 * 		calc_time_left_STF - calculate the amount of time left
 *  	for minimum duration and maximum duration of a STF resource resv
 *
 * 	@param[in]	resresv		-	the resource resv to calculate
 * 	@param[out]	min_time_left	-	time left to complete minimum duration
 *
 * 	@return	time left to complete maximum duration of the job
 * 	@retval	0	: if used amount is greater than duration
 * 	@retval	-1	: on error
 */
int
calc_time_left_STF(resource_resv *resresv, sch_resource_t* min_time_left)
{
	time_t 		used_amount = 0;

	if (min_time_left == NULL || resresv->duration == UNSPECIFIED)
		return -1;

	used_amount = calc_used_walltime(resresv);
	*min_time_left = IF_NEG_THEN_ZERO((resresv->min_duration - used_amount));

	return IF_NEG_THEN_ZERO((resresv->duration - used_amount));
}
/**
 * @brief
 *		calc_time_left - calculate the remaining time of a resource resv
 *
 * @param[in]	resresv	-	the resource resv to calculate
 * @param[in]	use_hard_duration - use the resresv's hard_duration instead of normal duration
 *
 * @return	time left on job
 * @retval	-1	: on error
 *
 */
int
calc_time_left(resource_resv *resresv, int use_hard_duration)
{
	time_t used_amount = 0;
	long duration;

	if (use_hard_duration && resresv->hard_duration == UNSPECIFIED)
		return -1;

	else if (!use_hard_duration && resresv->duration == UNSPECIFIED)
		return -1;

	if (use_hard_duration)
		duration = resresv->hard_duration;
	else
		duration = resresv->duration;

	used_amount = calc_used_walltime(resresv);

	return IF_NEG_THEN_ZERO(duration - used_amount);
}

/**
 * @brief
 *		cstrcmp	- check string compare - compares two strings but doesn't bomb
 *		  if either one is null
 *
 * @param[in]	s1	-	string one
 * @param[in]	s2	-	string two
 *
 * @return	int
 * @retval	-1	: if s1 < s2
 * @retval	0	: if s1 == s2
 * @retval	1	: if s1 > s2
 *
 */
int
cstrcmp(const char *s1, const char *s2)
{
	if (s1 == NULL && s2 == NULL)
		return 0;

	if (s1 == NULL && s2 != NULL)
		return -1;

	if (s1 != NULL && s2 == NULL)
		return 1;

	return strcmp(s1, s2);
}

/**
 * @brief
 *		is_num - checks to see if the string is a number, size, float
 *		 or time in string form
 *
 * @param[in]	str	-	the string to test
 *
 * @return	int
 * @retval	1	: if str is a number
 * @retval	0	: if str is not a number
 * @retval	-1	: on error
 *
 */
int
is_num(const char *str)
{
	int i;
	int colon_count = 0;
	int str_len = -1;

	if (str == NULL)
		return -1;

	if (str[0] == '-' || str[0] == '+')
		str++;

	str_len = strlen(str);
	for (i = 0; i < str_len && (isdigit(str[i]) || str[i] == ':'); i++) {
		if (str[i] == ':')
			colon_count++;
	}

	/* is the string completely numeric or a time(HH:MM:SS or MM:SS) */
	if (i == str_len && colon_count <= 2)
		return 1;

	/* is the string a size type resource like 'mem' */
	if ((i == (str_len - 2)) || (i == (str_len - 1))) {
		auto c = tolower(str[i]);
		if (c == 'k' || c == 'm' || c == 'g' || c == 't') {
			c = tolower(str[i+1]);
			if (c == 'b' || c == 'w' || c == '\0')
				return 1;
		} else if (i == (str_len - 1)) {
			/* catch the case of "b" or "w" */
			if (c == 'b' || c == 'w')
				return 1;
		}
	}

	/* last but not least, make sure we didn't stop on a decmal point */
	if (str[i] == '.') {
		for (i++ ; i < str_len && isdigit(str[i]); i++)
			;

		/* number is a float */
		if (i == str_len)
			return 1;
	}

	/* the string is not a number or a size or time */
	return 0;
}


/**
 * @brief
 *		count_array - count the number of elements in a NULL terminated array
 *		      of pointers
 *
 * @param[in]	arr	the array to count
 *
 * @return	number of elements in the array
 *
 */
int
count_array(const void *arr)
{
	int i;
	void **ptr_arr;

	if (arr == NULL)
		return 0;

	ptr_arr = (void **) arr;

	for (i = 0; ptr_arr[i] != NULL; i++)
		;

	return i;
}

/**
 * @brief
 *  dup_array - make a shallow copy of elements in a NULL terminated array of pointers.
 *
 * @param[in]	arr	-	the array to copy
 *
 * @return	array of pointers
 *
 */
void **
dup_array(void *ptr)
{
	void **ret;
	void **arr;
	int len = 0;

	arr = (void **)ptr;
	if (arr == NULL)
		return NULL;

	len = count_array(arr);
	ret = static_cast<void **>(malloc((len +1) * sizeof(void *)));
	if (ret == NULL)
		return NULL;
	memcpy(ret, arr, len * sizeof(void *));
	ret[len] = NULL;
	return ret;
}

/**
 * @brief
 *		remove_ptr_from_array - remove a pointer from a ptr list and move
 *				the rest of the pointers up to fill the hole
 *				Pointer array size will not change - an extra
 *				NULL is added to the end
 *
 *	  arr - pointer array
 *	  ptr - pointer to remove from array
 *
 *	returns non-zero if the ptr was successfully removed from the array
 *		zero if the array has not been modified
 *
 */
int
remove_ptr_from_array(void *arr, void *ptr)
{
	int i;
	void **parr;

	if (arr == NULL || ptr == NULL)
		return 0;

	parr = (void **) arr;

	for (i = 0; parr[i] != NULL && parr[i] != ptr; i++)
		;

	if (parr[i] != NULL) {
		for (int j = i; parr[j] != NULL; j++)
			parr[j] = parr[j + 1];
		return 1;
	}

	return 0;
}

/**
 * @brief add pointer to NULL terminated pointer array
 * @param[in] ptr_arr - pointer array to add to
 * @param[in] ptr - pointer to add
 *
 * @return void *
 * @retval pointer array with new element added
 * @retval NULL on error
 */
void *
add_ptr_to_array(void *ptr_arr, void *ptr)
{
	void **arr;
	int cnt;

	cnt = count_array(ptr_arr);

	if (cnt == 0) {
		arr = static_cast<void **>(malloc(sizeof(void *) * 2));
		if (arr == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			return NULL;
		}
		arr[0] = ptr;
		arr[1] = NULL;
	} else {
		arr = static_cast<void **>(realloc(ptr_arr, (cnt + 1) * sizeof(void *)));
		if (arr == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			return NULL;
		}
		arr[cnt - 1] = ptr;
		arr[cnt] = NULL;
	}
	return arr;
}

/**
 * @brief
 *		is_valid_pbs_name - is str a valid pbs username (POSIX.1 + ' ')
 *			    a valid name is: alpha numeric '-' '_' '.' ' '
 * 			    For fairshare entities: colen ':'
 *
 * @param[in]	str	-	string to check validity
 * @param[in]	len	-	length of str buffer or -1 if length is unknown
 *
 * @return	int
 * @retval	1	: valid PBS username
 * @retval	0	: not valid username
 */
int
is_valid_pbs_name(char *str, int len)
{
	int i;
	int valid = 1;

	if (str == NULL)
		return 0;

	/* if str is not null terminated, this could cause problems */
	if (len < 0)
		len = strlen(str) + 1;

	for (i = 0; i < len && valid; i++) {
		if (str[i] == '\0')
			break;
		if (!(isalpha(str[i]) || isdigit(str[i]) || str[i] == '.' ||
			str[i] == '-' || str[i] == '_' || str[i] == ' ' || str[i] == ':')) {
			valid = 0;
		}
	}

	if (i == len)
		valid = 0;

	return valid;
}

/**
 * @brief
 * 		clear an schd_error structure for reuse
 *
 * @param[in]	err	-	error structure to clear
 *
 * @return	void
 */
void
clear_schd_error(schd_error *err)
{
	if (err == NULL)
		return;

	set_schd_error_codes(err, SCHD_UNKWN, SUCCESS);
	set_schd_error_arg(err, ARG1, NULL);
	set_schd_error_arg(err, ARG2, NULL);
	set_schd_error_arg(err, ARG3, NULL);
	set_schd_error_arg(err, SPECMSG, NULL);
	err->rdef = NULL;
	err->next = NULL;
}

/**
 * @brief
 *  	constructor schd_error
 *
 * @return	new schd_error strucuture
 * @retval	NULL	: Error
 */
schd_error *
new_schd_error() {
	schd_error *err;
	if ((err = static_cast<schd_error *>(calloc(1, sizeof(schd_error)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}
	clear_schd_error(err);
	return err;
}

/**
 * @brief
 * 		copy constructor for schd_error
 *
 * @param[in]	oerr	-	output error.
 *
 * @return	new schd_error structure
 * @retval	NULL	: Error
 */
schd_error *
dup_schd_error(schd_error *oerr) {
	schd_error *nerr;
	if(oerr == NULL)
		return NULL;

	nerr = new_schd_error();
	if(nerr == NULL)
		return NULL;

	nerr->rdef = oerr->rdef;
	set_schd_error_codes(nerr, oerr->status_code, oerr->error_code);
	set_schd_error_arg(nerr, ARG1, oerr->arg1);
	set_schd_error_arg(nerr, ARG2, oerr->arg2);
	set_schd_error_arg(nerr, ARG3, oerr->arg3);
	set_schd_error_arg(nerr, SPECMSG, oerr->specmsg);

	return nerr;
}

/**
 * @brief
 * 		make a shallow copy of a schd_error and move all argument data
 *		to err.
 *
 * @param[in]	err	-	schd_error to move TO
 * @param[in]	oerr-	schd_error to move FROM
 *
 * @return	nothing
 */
void move_schd_error(schd_error *err, schd_error *oerr)
{
	if (oerr == NULL || err == NULL)
		return;

	/* we're about to overwrite these, free just incase so we don't leak*/
	free(err->arg1);
	free(err->arg2);
	free(err->arg3);
	free(err->specmsg);
	free_schd_error_list(err->next);

	memcpy(err, oerr, sizeof(schd_error));

	/* Now that err has taken over the memory for the points,
	 * NULL them on the original so we don't accidentally free them
	 */
	oerr->arg1 = NULL;
	oerr->arg2 = NULL;
	oerr->arg3 = NULL;
	oerr->specmsg = NULL;
	oerr->next = NULL;
	clear_schd_error(oerr);
}

/**
 * @brief
 *		deep copy oerr into err.  This will allocate memory
 *		for members of err, but not a new structure itself (like
 *		dup_schd_error() would.
 * @param[out] err
 * @param[in] oerr
 */
void
copy_schd_error(schd_error *err, schd_error *oerr)
{
	set_schd_error_codes(err, oerr->status_code, oerr->error_code);
	set_schd_error_arg(err, ARG1, oerr->arg1);
	set_schd_error_arg(err, ARG2, oerr->arg2);
	set_schd_error_arg(err, ARG3, oerr->arg3);
	set_schd_error_arg(err, SPECMSG, oerr->specmsg);
	err->rdef = oerr->rdef;
}

/**
 * @brief
 * 		safely set the schd_config arg buffers without worrying about leaking
 *
 * @param[in,out]	err	-	object to set
 * @param[in]	arg_field	-	arg buffer to set
 * @param[in]	arg	-	string to set arg to
 *
 * @return	nothing
 */
void set_schd_error_arg(schd_error *err, enum schd_error_args arg_field, const char *arg) {

	if(err == NULL)
		return;

	switch(arg_field) {
		case ARG1:
			free(err->arg1);
			if (arg != NULL)
				err->arg1 = string_dup(arg);
			else
				err->arg1 = NULL;
			break;
		case ARG2:
			free(err->arg2);
			if (arg != NULL)
				err->arg2 = string_dup(arg);
			else
				err->arg2 = NULL;
			break;
		case ARG3:
			free(err->arg3);
			if (arg != NULL)
				err->arg3 = string_dup(arg);
			else
				err->arg3 = NULL;
			break;
		case SPECMSG:
			free(err->specmsg);
			if (arg != NULL)
				err->specmsg = string_dup(arg);
			else
				err->specmsg = NULL;
			break;
		default:
			log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_SCHED, LOG_DEBUG, __func__, "Invalid schd_error arg message type");
	}

}

/**
 * @brief
 * 		set the status code and error code of a schd_error structure
 *
 *	@note
 *		this ensures both codes are set together
 *
 * @param[in,out]	err	-	error structure to set
 * @param[in]	status_code	-	status code
 * @param[in]	error_code	-	error code
 *
 * @return	nothing
 */
void set_schd_error_codes(schd_error *err, enum schd_err_status status_code, enum sched_error_code error_code)
{
	if(err == NULL)
		return;
	if(status_code < SCHD_UNKWN || status_code >= SCHD_STATUS_HIGH)
		return;
	if(error_code < PBSE_NONE || error_code > ERR_SPECIAL)
		return;

	err->status_code = status_code;
	err->error_code = error_code;
}

/**
 * @brief
 * 		destructor of schd_error: Free a single schd_error structure
 *
 * @param[in]	err	-	Error structure
 */
void
free_schd_error(schd_error *err)
{
	if(err == NULL)
		return;

	free(err->arg1);
	free(err->arg2);
	free(err->arg3);
	free(err->specmsg);

	err->next = NULL; /* just incase people try and access freed memory */

	free(err);
}

/**
 * @brief
 * 		list schd_error destructor: Free multiple schd_error's in a list
 *
 * @param[in]	 err_list	-	Error list.
 */
void
free_schd_error_list(schd_error *err_list) {
	schd_error *err, *tmp;

	err = err_list;
	while (err != NULL) {
		tmp = err->next;
		free_schd_error(err);
		err = tmp;
	}

}

/**
 * @brief
 * 		create a simple schd_error with no arguments
 *
 * @param[in]	error_code	-	error code for new schd_error
 * @param[in]	status_code -	status code for new schd_error
 *
 * @return	new schd_error
 */
schd_error *create_schd_error(enum sched_error_code error_code, enum schd_err_status status_code)
{
	schd_error *nse;
	nse = new_schd_error();
	if(nse == NULL)
		return NULL;
	set_schd_error_codes(nse, status_code, error_code);
	return nse;
}

/**
 * @brief
 *		create a schd_error complete with arguments
 * @par
 *	schd_error fields: error_code, status_code, arg1, arg2, arg3 and specmsg.
 *
 * @param[in]	error_code	-	Error Code.
 * @param[in]	status_code	-	Status Code.
 * @param[in]	arg1	-	Argument 1
 * @param[in]	arg2	-	Argument 2
 * @param[in]	arg3	-	Argument 3
 * @param[in]	specmsg	-	string to set arg to
 *
 * @return	new schd_error
 */
schd_error *create_schd_error_complex(enum sched_error_code error_code, enum schd_err_status status_code, char *arg1, char *arg2, char *arg3, char *specmsg)
{
	schd_error *nse;

	nse = create_schd_error(error_code, status_code);
	if(nse == NULL)
		return NULL;

	if(arg1 != NULL)
		set_schd_error_arg(nse, ARG1, arg1);

	if(arg2 != NULL)
		set_schd_error_arg(nse, ARG2, arg2);

	if(arg3 != NULL)
		set_schd_error_arg(nse, ARG3, arg3);

	if(specmsg != NULL)
		set_schd_error_arg(nse, SPECMSG, specmsg);

	return nse;
}
/**
 * @brief
 * 		add a schd_error to a linked list of schd_errors.
 *		The way this works: head of the schd_error list is already created and
 *		passed into the caller (e.g., from main_sched_loop() -> is_ok_to_run()).  The caller will maintain
 *		a 'prev_err' pointer.  The address of the prev_err(i.e., &prev_err) is passed into this function.
 *		The first call, we add the head.  Each additional call, we set up the next pointers.
 *		If err-> next is not NULL, we assume we're adding a sublist of schd_error's to the main list.
 *
 *	@example
 *		main_sched_loop(): foo_err = new_schd_error()
 *		main_sched_loop(): is_ok_to_run(..., foo_err)
 *		is_ok_to_run(): schd_error *prev_err = NULL;
 *		is_ok_to_run() add_err(&prev_err, err);
 *		is_ok_to_run(): err = new_schd_err()
 *		is_ok_to_run() add_err(&prev_err, err)
 *		Note: main_sched_loop() did not pass the address of foo_err into is_ok_to_run()
 *		Note2: main_sched_loop() holds the head of the list, so we don't return the list
 *
 * @param[in]	prev_err	-	address of the pointer previous to the end of the list (i.e. (*prev_err)->next->next == NULL
 * @param[in]	err	-	schd_error to add to the list (may be a list of schd_error's)
 *
 * @return	nothing
 *
 * @note	nothing stops duplicate entries from being added
 */
void add_err(schd_error **prev_err, schd_error *err)
{
	schd_error *cur = NULL;

	if (err == NULL || prev_err == NULL)
		return;

	if(*prev_err == NULL)
		(*prev_err) = err;
	else
		(*prev_err)->next = err;


	if(err->next != NULL) {
		for (cur = err; cur->next != NULL; cur = cur->next)
			;
		(*prev_err) = cur;
	} else
		(*prev_err) = err;
}

/**
 * @brief
 * 		turn a resource/resource_req into a string for printing using a
 *		internal static buffer
 *
 * @param[in]	p	-	pointer to resource/req
 * @param[in] fld	-	the field of the resource to print
 *
 * @return	char *
 * @retval	the resource in string format (in internal static string)
 * @retval	"" on error
 *
 * @par	MT-Safe: No
 *
 * @note
 * 		This function can not be used more than once in a printf() type func
 *	 	The static string will be overwritten before printing and you will get
 *	 	the same value for all calls.  Use res_to_str_r().
 */
char *
res_to_str(void *p, enum resource_fields fld)
{
	static char *resbuf = NULL;
	static int resbuf_size = 1024;

	if (resbuf == NULL) {
		if ((resbuf = static_cast<char *>(malloc(resbuf_size))) == NULL)
			return const_cast<char *>("");
	}

	return res_to_str_re(p, fld, &resbuf, &resbuf_size, NO_FLAGS);

}

/**
 * @brief
 * 		convert a number that is a resource into a string with a provided
 * 		non-expandable buffer.  This is useful for size types or scheduler constants
 *
 * @param[in]	amount	-	amount of resource
 * @param[in]	def	-	resource definition of amount
 * @param[in]	fld	-	the field of the resource to print - Should be RF_REQUEST or
 *              		RF_AVAIL
 * @param[in,out]	buf	-	buffer for res to str conversion -- not expandable
 * @param[in]	bufsize	-	size of buf
 *
 * @return	char *
 * @retval	the resource in string format (in provided buffer)
 * @retval	""	: on error
 */
char *
res_to_str_c(sch_resource_t amount, resdef *def, enum resource_fields fld,
	char *buf, int bufsize)
{
	schd_resource res = {0};
	resource_req req = {0};
	const char *unknown[] = {"unknown", NULL};

	if (buf == NULL)
		return const_cast<char *>("");

	buf[0] = '\0';

	if (def == NULL)
		return const_cast<char *>("");

	switch (fld) {
		case RF_REQUEST:
			req.amount = amount;
			req.def = def;
			req.name = def->name.c_str();
			req.type = def->type;
			req.res_str = const_cast<char *>("unknown");
			return res_to_str_re(((void*) &req), fld, &buf, &bufsize, NOEXPAND);
			break;
		case RF_AVAIL:
		default:
			res.avail = amount;
			res.assigned = amount;
			res.def = def;
			res.name = def->name.c_str();
			res.type = def->type;
			res.orig_str_avail = const_cast<char *>("unknown");
			res.str_avail = const_cast<char **>(unknown);
			res.str_assigned = const_cast<char *>("unknown");
			return res_to_str_re(((void*) &res), fld, &buf, &bufsize, NOEXPAND);
	}
	return const_cast<char *>("");
}
/**
 * @brief
 * 		convert a resource to string with a non-expandable buffer
 *
 * @param[in]	p	-	pointer to resource/req
 * @param[in]	fld	-	the field of the resource to print
 * @param[in]	buf	-	buffer for res to str conversion -- not expandable
 * @param[in,out]	bufsize	-	size of buf
 *
 * @return	char *
 * @retval	the resource in string format (in provided buffer)
 * @retval	""	: on error
 */
char *
res_to_str_r(void *p, enum resource_fields fld, char *buf, int bufsize)
{
	return res_to_str_re(p, fld, &buf, &bufsize, NOEXPAND);
}

/**
 * @brief
 * 		turn a resource (resource/resource_req) into
 *      a string for printing.  If the buffer needs expanding, it
 *		will be expanded(except when specified by flags).
 *
 * @param[in]	p	-	a pointer to a resource/req
 * @param[in]	fld	-	The field of the resource type to print.  This is
 *						used to determine if p is a resource or a resource_req
 * @param[in,out]	buf	-	buffer to copy string into
 * @param[in,out]	buf_size	-	size of buffer
 * @param[in]	flags	-	flags to control printing
 *							PRINT_INT_CONST - print internal constant names
 *							NOEXPAND - don't expand the buffer, just fill upto the max size
 *
 * @return	char *
 * @retval	the resource in string format (in buf)
 * @retval	""  on error
 */
char *
res_to_str_re(void *p, enum resource_fields fld, char **buf,
	int *bufsize, unsigned int flags)
{
	schd_resource *res = NULL;
	resource_req *req = NULL;
	struct resource_type *rt;
	char *str;
	int free_str = 0;
	sch_resource_t amount;

	char localbuf[1024];
	char *ret;
	resource_type rtype;

	if (buf == NULL || bufsize == NULL)
		return const_cast<char *>("");

	if (*bufsize > 0)
		**buf = '\0';

	if (p == NULL)
		return const_cast<char *>("");

	ret = *buf;

	if (*bufsize <= 0) {
		if (!(flags & NOEXPAND)) {
			if ((*buf = static_cast<char *>(malloc(1024))) == NULL) {
				log_err(errno, __func__, MEM_ERR_MSG);
				return const_cast<char *>("");
			}
			else
				*bufsize = 1024;
		}
	}

	/* empty string */
	**buf = '\0';

	switch (fld) {
		case RF_REQUEST:
			req = static_cast<resource_req *>(p);
			rt = &(req->type);
			str = req->res_str;
			amount = req->amount;
			break;

		case RF_DIRECT_AVAIL:
			res = static_cast<schd_resource *>(p);
			if (res->indirect_res != NULL) {
				if (flags & NOEXPAND)
					snprintf(*buf, *bufsize, "@");
				else {
					ret = pbs_strcat(buf, bufsize, "@");
					if (ret == NULL)
						return const_cast<char *>("");
				}

				str = res->indirect_vnode_name;
				rtype.is_string = 1;
				rtype.is_non_consumable = 1;
				rt = &rtype;
				amount = 0;
				break;
			}
			/* if not indirect, fall through normally */
		case RF_AVAIL:
			res = static_cast<schd_resource *>(p);
			if (res->indirect_res != NULL)
				res = res->indirect_res;
			rt = &(res->type);
			str = string_array_to_str(res->str_avail);
			if (str == NULL)
				str = const_cast<char *>("");
			else
				free_str = 1;
			amount = res->avail;
			break;

		case RF_ASSN:
			res = static_cast<schd_resource *>(p);
			rt = &(res->type);
			str = res->str_assigned;
			amount = res->assigned;
			break;

		default:
			return const_cast<char *>("");
	}

	/* error checking */
	if (rt->is_string) {
		if (flags & NOEXPAND)
			snprintf(*buf, *bufsize, "%s", str);
		else
			ret = pbs_strcat(buf, bufsize, str);
	}
	else if (rt->is_boolean) {
		if (flags & NOEXPAND)
			snprintf(*buf, *bufsize, "%s", amount ? ATR_TRUE : ATR_FALSE);
		else
			ret = pbs_strcat(buf, bufsize, amount ? ATR_TRUE : ATR_FALSE);
	}
	else if (rt->is_size) {
		if (amount == 0) /* need to special case 0 or it falls into tb case */
			snprintf(localbuf, sizeof(localbuf), "0kb");
		else if (((long)amount % TERATOKILO) == 0)
			snprintf(localbuf, sizeof(localbuf), "%ldtb", (long) (amount/TERATOKILO));
		else if (((long)amount % GIGATOKILO) == 0)
			snprintf(localbuf, sizeof(localbuf), "%ldgb", (long) (amount/GIGATOKILO));
		else if (((long)amount % MEGATOKILO) == 0)
			snprintf(localbuf, sizeof(localbuf), "%ldmb", (long) (amount/MEGATOKILO));
		else
			snprintf(localbuf, sizeof(localbuf), "%ldkb", (long) amount);
		if (flags & NOEXPAND)
			snprintf(*buf, *bufsize, "%s", localbuf);
		else
			ret = pbs_strcat(buf, bufsize, localbuf);
	}
	else if (rt->is_num) {
		int const_print = 0;
		if (amount == UNSPECIFIED_RES) {
			if (flags & PRINT_INT_CONST) {
				if (flags & NOEXPAND)
					snprintf(*buf, *bufsize, UNSPECIFIED_STR);
				else
					ret = pbs_strcat(buf, bufsize, UNSPECIFIED_STR);

				const_print = 1;
			}
		} else if (amount == SCHD_INFINITY_RES) {
			if (flags & PRINT_INT_CONST) {
				if (flags & NOEXPAND)
					snprintf(*buf, *bufsize, SCHD_INFINITY_STR);
				else
					ret = pbs_strcat(buf, bufsize, SCHD_INFINITY_STR);

				const_print = 1;
			}
		}

		if (const_print == 0) {
			if (rt->is_float)
				snprintf(localbuf, sizeof(localbuf), "%.*f",
					float_digits(amount, FLOAT_NUM_DIGITS), (double) amount);
			else
				snprintf(localbuf, sizeof(localbuf), "%ld", (long) amount);
			if (flags & NOEXPAND)
				snprintf(*buf, *bufsize, "%s", localbuf);
			else
				ret = pbs_strcat(buf, bufsize, localbuf);
		}
	} else if (rt->is_time) {
		char resbuf[1024];
		convert_duration_to_str((long) amount, resbuf, sizeof(resbuf));
	}

	if (free_str)
		free(str);

	if (ret == NULL)
		return const_cast<char *>("");
	return *buf;
}

/*
 * @brief   helper function to free an array of pointers
 *
 * @param[in] inp - array of pointers
 * @return void
 */
void
free_ptr_array(void *inp)
{
	int i;
	void **arr;

	if (inp == NULL)
		return;

	arr = (void **)inp;

	for (i = 0; arr[i] != NULL; i++)
		free(arr[i]);
	free(arr);
}

/**
 *
 *	@brief break apart a comma delimited string into an array of strings.
 *	       It's an overloaded function of break_comma_list in libutils
 *
 *	@param[in] strlist - the comma delimited string
 *
 *	@return std::vector<std::string>
 *
 */
std::vector<std::string>
break_comma_list(const std::string &strlist)
{
	std::stringstream sstream(strlist);
	std::string str;
	std::vector<std::string> ret;
	while (std::getline(sstream, str, ','))
		ret.push_back(str);
	return ret;
}
