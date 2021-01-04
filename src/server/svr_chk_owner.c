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
 * @file    svr_chk_owner.c
 *
 * @brief
 * 		svr_chk_owner.c	-	This file contains functions related to authorizing a job request.
 *
 * Functions included are:
 * 	svr_chk_owner()
 *	svr_authorize_jobreq()
 *	svr_get_privilege()
 *	authenticate_user()
 *	chk_job_request()
 *	chk_rescResv_request()
 *	svr_chk_ownerResv()
 *	svr_authorize_resvReq()
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <sys/types.h>
#include "libpbs.h"
#include "string.h"
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "net_connect.h"

#include <unistd.h>

#include "job.h"
#include "reservation.h"
#include "pbs_error.h"
#include "log.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "libutil.h"


/* Global Data */

extern char  server_host[];
extern char *msg_badstate;
extern char *msg_permlog;
extern char *msg_unkjobid;
extern char *msg_system;
extern char *msg_unkresvID;
extern char *msg_delProgress;
extern time_t time_now;

/* Global functions */
extern int svr_chk_histjob(job *pjob);

/* Non-global functions */
static	int svr_authorize_resvReq(struct batch_request*, resc_resv*);


/**
 * @brief
 * 		svr_chk_owner - compare a user name from a request and the name of
 *		the user who owns the job.
 *
 * @param[in]	preq	-	request structure which contains the user name
 * @param[in]	pjob	-	job structure
 *
 * @return	int
 * @retval	0	: success
 * @retval	!0	: user is not the job owner
 */


int
svr_chk_owner(struct batch_request *preq, job *pjob)
{
	char  owner[PBS_MAXUSER+1];
	char *pu;
	char *ph;
	char  rmtuser[PBS_MAXUSER+PBS_MAXHOSTNAME+2];
	extern int ruserok(const char *rhost, int suser, const char *ruser,
		const char *luser);

	/* Are the owner and requestor the same? */
	snprintf(rmtuser, sizeof(rmtuser), "%s", get_jattr_str(pjob, JOB_ATR_job_owner));
	pu = rmtuser;
	ph = strchr(rmtuser, '@');
	if (!ph)
		return -1;
	*ph++ = '\0';
	if (strcmp(preq->rq_user, pu) == 0) {
		/* Avoid the lookup if they match. */
		if (strcmp(preq->rq_host, ph) == 0)
			return 0;
		/* Perform the lookup. */
		if (is_same_host(preq->rq_host, ph))
			return 0;
	}

	/* map requestor user@host to "local" name */

	pu = site_map_user(preq->rq_user, preq->rq_host);
	if (pu == NULL)
		return (-1);
	(void)strncpy(rmtuser, pu, PBS_MAXUSER);

	/*
	 * Get job owner name without "@host" and then map to "local" name.
	 */

	get_jobowner(get_jattr_str(pjob, JOB_ATR_job_owner), owner);
	pu = site_map_user(owner, get_hostPart(get_jattr_str(pjob, JOB_ATR_job_owner)));

	if (server.sv_attr[(int)SVR_ATR_FlatUID].at_val.at_long) {
		/* with flatuid, all that must match is user names */
		return (strcmp(rmtuser, pu));
	} else  {
		/* non-flatuid space, must validate rmtuser vs owner */
		return (ruserok(preq->rq_host, 0, rmtuser, pu));
	}
}


/**
 * @brief
 * 		svr_authorize_jobreq - determine if requestor is authorized to make
 *		request against the job.  This is only called for batch requests
 *		against jobs, not manager requests against queues or the server.
 *
 * @param[in]	preq	-	request structure which contains the user name
 * @param[in]	pjob	-	job structure
 *
 * @return	int
 * @retval	0	: if authorized (job owner, operator, administrator)
 * @retval	!0	: not authorized.
 */

int
svr_authorize_jobreq(struct batch_request *preq, job *pjob)
{
	/* Is requestor special privileged? */

	if ((preq->rq_perm & (ATR_DFLAG_OPRD | ATR_DFLAG_OPWR |
		ATR_DFLAG_MGRD | ATR_DFLAG_MGWR)) != 0)
		return (0);

	/* if not, see if requestor is the job owner */

	else if (svr_chk_owner(preq, pjob) == 0)
		return (0);

	else
		return (-1);
}

/**
 * @brief
 * 		svr_get_privilege - get privilege level of a user.
 *
 *		Privilege is granted to a user at a host.  A user is automatically
 *		granted "user" privilege.  The user@host pair must appear in
 *		the server's administrator attribute list to be granted "manager"
 *		privilege and/or appear in the operators attribute list to be
 *		granted "operator" privilege.  If either acl is unset, then root
 *		on the server machine is granted that privilege.
 *
 *		If "PBS_ROOT_ALWAYS_ADMIN" is defined, then root always has privilege
 *		even if not in the list.
 *
 *		The returns are based on the access permissions of attributes, see
 *		attribute.h.
 *
 * @param[in]	user	-	user in user@host pair
 * @param[in]	host	-	host in user@host pair
 *
 * @return	int
 * @retval	access privilage of the user
 */

int
svr_get_privilege(char *user, char *host)
{
	int   is_root = 0;
	int   priv = (ATR_DFLAG_USRD | ATR_DFLAG_USWR);
	char  uh[PBS_MAXUSER + PBS_MAXHOSTNAME + 2];

	(void)strcpy(uh, user);
	(void)strcat(uh, "@");
	(void)strcat(uh, host);

	if (strcmp(user, PBS_DEFAULT_ADMIN) == 0) {
		char myhostname[PBS_MAXHOSTNAME+1];
		/* First try without DNS lookup. */
		if (strcasecmp(host, server_host) == 0) {
			is_root = 1;
		} else if (strcasecmp(host, LOCALHOST_SHORTNAME) == 0) {
			is_root = 1;
		} else if (strcasecmp(host, LOCALHOST_FULLNAME) == 0) {
			is_root = 1;
		} else {
			if (gethostname(myhostname, (sizeof(myhostname) - 1)) == -1) {
				myhostname[0] = '\0';
			}
			if (strcasecmp(host, myhostname) == 0) {
				is_root = 1;
			}
		}
		if (is_root == 0) {
			/* Now try with DNS lookup. */
			if (is_same_host(host, server_host)) {
				is_root = 1;
			} else if (is_same_host(host, myhostname)) {
				is_root = 1;
			}
		}
	}

#ifdef PBS_ROOT_ALWAYS_ADMIN
	if (is_root)
		return (priv | ATR_DFLAG_MGRD | ATR_DFLAG_MGWR | ATR_DFLAG_OPRD | ATR_DFLAG_OPWR);
#endif	/* PBS_ROOT_ALWAYS_ADMIN */

	if (!(server.sv_attr[(int)SVR_ATR_managers].at_flags & ATR_VFLAG_SET)) {
		if (is_root)
			priv |= (ATR_DFLAG_MGRD | ATR_DFLAG_MGWR);

	} else if (acl_check(&server.sv_attr[SVR_ATR_managers], uh, ACL_User))
		priv |= (ATR_DFLAG_MGRD | ATR_DFLAG_MGWR);

	if (!is_attr_set(&server.sv_attr[SVR_ATR_operators])) {
		if (is_root)
			priv |= (ATR_DFLAG_OPRD | ATR_DFLAG_OPWR);

	} else if (acl_check(&server.sv_attr[SVR_ATR_operators], uh, ACL_User))
		priv |= (ATR_DFLAG_OPRD | ATR_DFLAG_OPWR);

	return (priv);
}

/**
 * @brief
 * 		authenticate_user - authenticate user by checking name against credential
 *		       provided on connection via Authenticate User request.
 *
 * @param[in]	preq	-	user to be authenticated
 * @param[in]	pcred	-	credential provided on connection via Authenticate User request.
 *
 * @return	int
 * @retval	0	: if user is who s/he claims
 * @retval	nonzero	: error code
 */

int
authenticate_user(struct batch_request *preq, struct connection *pcred)
{
	char uath[PBS_MAXUSER + PBS_MAXHOSTNAME + 1];

	if (strncmp(preq->rq_user, pcred->cn_username, PBS_MAXUSER))
		return (PBSE_BADCRED);
	if (strncasecmp(preq->rq_host, pcred->cn_hostname, PBS_MAXHOSTNAME))
		return (PBSE_BADCRED);
	if (pcred->cn_timestamp) {
		if ((pcred->cn_timestamp - CREDENTIAL_TIME_DELTA > time_now) ||
			(pcred->cn_timestamp + CREDENTIAL_LIFETIME < time_now))
			return (PBSE_EXPIRED);
	}

	/* If Server's Acl_User enabled, check if user in list */

	if (server.sv_attr[SVR_ATR_AclUserEnabled].at_val.at_long) {

		(void)strcpy(uath, preq->rq_user);
		(void)strcat(uath, "@");
		(void)strcat(uath, preq->rq_host);
		if (acl_check(&server.sv_attr[SVR_ATR_AclUsers],
			uath, ACL_User) == 0) {
			/* not in list, next check if listed as a manager */

			if ((svr_get_privilege(preq->rq_user, preq->rq_host) &
				(ATR_DFLAG_MGWR | ATR_DFLAG_OPWR)) == 0)
				return (PBSE_PERM);
		}
	}

	/* A site stub for additional checking */

	return (site_allow_u(preq->rq_user, preq->rq_host));
}

/**
 * @brief
 * 		chk_job_request - check legality of a request against a job
 * @par
 *		this checks the most conditions common to most job batch requests.
 *		It also returns a pointer to the job if found and the tests pass.
 *		If the request is for a single subjob or a range of subjobs (of an
 *		Job Array),  the return job pointer is to the parent Array Job.
 * @par
 *		Depending on what the "jobid" identifies, the following is returned
 *		in the integer pointed to by rc:
 *	   	IS_ARRAY_NO (0)       - for a regular job
 *	   	IS_ARRAY_ArrayJob (1) - for an Array Job
 *	   	IS_ARRAY_Single (2)   - for a single subjob
 *	   	IS_ARRAY_Range (3)    - for a range of  subjobs
 *
 * @param[in]	jobid	-	Job Id.
 * @param[in,out]	preq	-	job batch request
 * @param[out]	rc	-	Depending on what the "jobid" identifies,
 * 						the following is returned
 *						(0)       - for a regular job
 *						(1) - for an Array Job
 *						(2)   - for a single subjob
 *						(3)    - for a range of  subjobs
 * @param[out]	err		PBSE reason why request was rejected
 *
 * @return	job *
 * @retval	a pointer to the job	: if found and the tests pass.
 * @retval	NULL	: failed
 */


job *
chk_job_request(char *jobid, struct batch_request *preq, int *rc, int *err)
{
	int	 t;
	int	 histerr = 0;
	job	*pjob;
	int deletehist = 0;
	char	*p1;
	char	*p2;

	if (preq->rq_extend && strstr(preq->rq_extend, DELETEHISTORY))
		deletehist = 1;
	t = is_job_array(jobid);
	if ((t == IS_ARRAY_NO) || (t == IS_ARRAY_ArrayJob))
		pjob = find_job(jobid);		/* regular or ArrayJob itself */
	else
		pjob = find_arrayparent(jobid); /* subjob(s) */

	*rc = t;

	if (pjob == NULL) {
		log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_INFO,
			jobid, msg_unkjobid);
		if (err != NULL)
			*err = PBSE_UNKJOBID;

		if (preq->rq_type != PBS_BATCH_DeleteJobList)
			req_reject(PBSE_UNKJOBID, 0, preq);
		return NULL;
	} else {
		histerr = svr_chk_histjob(pjob);
		if (histerr && deletehist == 0) {
			if (err != NULL)
				*err = histerr;
			if (preq->rq_type != PBS_BATCH_DeleteJobList)
				req_reject(histerr, 0, preq);
			return NULL;
		}
		if (deletehist == 1&& check_job_state(pjob, JOB_STATE_LTR_MOVED) &&
			!check_job_substate(pjob, JOB_SUBSTATE_FINISHED)) {
			job_purge(pjob);
			if (preq->rq_type != PBS_BATCH_DeleteJobList)
				req_reject(PBSE_UNKJOBID, 0, preq);
			return NULL;
		}
	}

	/*
	 * The job was found using the job ID in the request, but it may not
	 * match exactly (i.e. FQDN vs. unqualified hostname). Overwrite the
	 * host portion of the job ID in the request with the host portion of
	 * the one from the server job structure. Do not modify anything
	 * before the first dot in the job ID because it may be an array job.
	 * This will allow find_job() to look for an exact match when the
	 * request is serviced by MoM.
	 */
	p1 = strchr(pjob->ji_qs.ji_jobid, '.');
	if (p1) {
		p2 = strchr(jobid, '.');
		if (p2)
			*p2 = '\0';
		strncat(jobid, p1, PBS_MAXSVRJOBID-1);
	}

	if (svr_authorize_jobreq(preq, pjob) == -1) {
		(void)sprintf(log_buffer, msg_permlog, preq->rq_type,
			"Job", pjob->ji_qs.ji_jobid,
			preq->rq_user, preq->rq_host);
		log_event(PBSEVENT_SECURITY, PBS_EVENTCLASS_JOB, LOG_INFO,
			pjob->ji_qs.ji_jobid, log_buffer);
		if (err != NULL)
			*err = PBSE_PERM;
		if (preq->rq_type != PBS_BATCH_DeleteJobList)
			req_reject(PBSE_PERM, 0, preq);
		return NULL;
	}

	if ((t == IS_ARRAY_NO) && (check_job_state(pjob, JOB_STATE_LTR_EXITING))) {

		/* special case Deletejob with "force" */
		if ((preq->rq_type == PBS_BATCH_DeleteJob) &&
			(preq->rq_extend != NULL) &&
			(strcmp(preq->rq_extend, "force") == 0)) {
			return pjob;
		}

		log_eventf(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, LOG_INFO, pjob->ji_qs.ji_jobid,
			   "%s, state=%c", msg_badstate, get_job_state(pjob));
		if (err != NULL)
			*err = PBSE_BADSTATE;
		if (preq->rq_type != PBS_BATCH_DeleteJobList)
			req_reject(PBSE_BADSTATE, 0, preq);
		return NULL;
	}

	return pjob;
}

/**
 * @brief
 * 		chk_rescResv_request - check legality of a request against named
 *	 	resource reservation
 * @par
 *		This checks the conditions common to most batch requests
 *		against a resc_resv object.  If the object is found in the
 *		system and the tests are passed, a non-zero resc_resv
 *		pointer is returned.
 * @par
 *		If the object can't be found or an applied test fails,
 *		an appropriate error event is logged, an error code is passed
 *		back to the requester via function req_reject(), the batch
 *		request structure is handled appropriately, and
 *		value NULL is returned to the caller.
 * @note
 *		Notes: On failure the reply back to the requester will be handled
 *	       for the caller - as is the batch request structure itself.
 *	       It would be better if the caller got back an error code
 *	       and then called a function passing it that code and request
 *	       and let it handle the event log and rejection of the
 *	       request.  Currently, we are modeled along the lines of
 *	       the function chk_job_request().
 *
 * @param[in]	resvID	-	reservation ID
 * @param[in,out]	preq	-	job batch request
 *
 * @return	resc_resv *
 * @retval	resc_resv object ptr	: successful
 * @retval	NULL	: failed test/no object
 */

resc_resv  *
chk_rescResv_request(char *resvID, struct batch_request *preq)
{
	resc_resv	*presv;

	if ((presv = find_resv(resvID)) == NULL) {
		log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_RESV, LOG_INFO,
			resvID, msg_unkresvID);
		req_reject(PBSE_UNKRESVID, 0, preq);
		return NULL;
	}

	if (resvID[0] == PBS_MNTNC_RESV_ID_CHAR && ! (preq->rq_perm & (ATR_DFLAG_OPWR | ATR_DFLAG_MGWR))) {
		req_reject(PBSE_PERM, 0, preq);
		return NULL;
	}

	if (svr_authorize_resvReq(preq, presv) == -1) {
		(void)sprintf(log_buffer, msg_permlog, preq->rq_type,
			"RESCRESV", presv->ri_qs.ri_resvID,
			preq->rq_user, preq->rq_host);
		log_event(PBSEVENT_SECURITY, PBS_EVENTCLASS_RESV, LOG_INFO,
			presv->ri_qs.ri_resvID, log_buffer);
		req_reject(PBSE_PERM, 0, preq);
		return NULL;
	}

	return (presv);
}

/**
 * @return
 * 		svr_chk_ownerResv - compare a user name from a request and the name of
 *		the user who owns the resources reservation.
 *
 * @param[in]	preq	-	request structure which contains the user name
 * @param[in]	presv	-	resources reservation.
 *
 * @return	int
 * @retval	0	: if same
 * @retval	nonzero	: if user is not the reservation owner
 */


int
svr_chk_ownerResv(struct batch_request *preq, resc_resv *presv)
{
	char  owner[PBS_MAXUSER + 1];
	char *host;
	char *pu;
	char  rmtuser[PBS_MAXUSER + 1];

	/* map user@host to "local" name */

	pu = site_map_user(preq->rq_user, preq->rq_host);
	if (pu == NULL)
		return (-1);
	(void)strncpy(rmtuser, pu, PBS_MAXUSER);

	get_jobowner(presv->ri_wattr[(int)RESV_ATR_resv_owner].at_val.at_str, owner);
	host = get_hostPart(presv->ri_wattr[(int)RESV_ATR_resv_owner].at_val.at_str);
	pu = site_map_user(owner, host);

	return (strcmp(rmtuser, pu));
}


/**
 * @brief
 * 		svr_authorize_resvReq - determine if requestor is authorized to make
 *		request against the reservation.  This is only called for batch requests
 *		against reservations.
 *
 * @param[in]	preq	-	batch request structure
 * @param[in]	presv	-	resources reservation.
 *
 * @return	int
 * @retval	0	: if authorized (reservation owner, operator, administrator)
 * @retval	-1	: if not authorized.
 */

static	int
svr_authorize_resvReq(struct batch_request *preq, resc_resv *presv)
{
	/* Is requestor special privileged? */

	if ((preq->rq_perm & (ATR_DFLAG_OPRD | ATR_DFLAG_OPWR |
		ATR_DFLAG_MGRD | ATR_DFLAG_MGWR)) != 0)
		return (0);
	/* Only Manager has privilage to force modify reservation */
	if (preq->rq_type == PBS_BATCH_ModifyResv && (preq->rq_extend != NULL) &&
	    (strcmp(preq->rq_extend, FORCE) == 0) && ((preq->rq_perm & ATR_DFLAG_MGWR) == 0))
		return (-1);

	/* if not, see if requestor is the reservation owner */

	else if (svr_chk_ownerResv(preq, presv) == 0)
		return (0);

	else
		return (-1);
}
