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
 * @file	qdel.c
 * @brief
 * 	qdel - (PBS) delete batch job
 *
 * @author  Terry Heidelberg
 * 			Livermore Computing
 *
 * @author  Bruce Kelly
 * 			National Energy Research Supercomputer Center
 *
 * @author  Lawrence Livermore National Laboratory
 * 			University of California
 */

#include <unistd.h>
#include "cmds.h"
#include "pbs_ifl.h"
#include <pbs_config.h>   /* the master config generated by configure */
#include <pbs_version.h>

#define MAX_TIME_DELAY_LEN 32
#define GETOPT_ARGS "W:x"

extern void free_svrjobidlist(svr_jobid_list_t *list, int shallow);
extern int append_jobid(svr_jobid_list_t *svr, char *jobid);
extern int add_jid_to_list_by_name(char *job_id, char *svrname, svr_jobid_list_t **svr_jobid_list_hd);

/**
 * @brief	Process the deljob error response from server
 *
 * @param[in]	clusterid - cluster name (PBS_SERVER)
 * @param[in]	list - list of deljob response objects
 * @param[out]	rmtlist - return pointer to list of jobs to try deleting on remote servers
 *
 * @return int
 * @retval error code from server
 */
static int
process_deljobstat(char *clusterid, struct batch_deljob_status **list, svr_jobid_list_t **rmtlist)
{
	struct batch_deljob_status *p_delstatus;
	struct batch_deljob_status *next = NULL;
	struct batch_deljob_status *prev = NULL;
	char *errtxt = NULL;
	int any_failed = 0;

	for (p_delstatus = *list; p_delstatus != NULL; prev = p_delstatus, p_delstatus = next) {
		next = p_delstatus->next;
		if (p_delstatus->code == PBSE_UNKJOBID && rmtlist != NULL) {
			char rmt_server[PBS_MAXDEST + 1];

			/* Check if job was moved to a remote cluster */
			if (locate_job(p_delstatus->name, clusterid, rmt_server)) {
				if (add_jid_to_list_by_name(strdup(p_delstatus->name), rmt_server, rmtlist) != 0)
					return pbs_errno;
				else {	/* Job found on remote server, let's remove it from error list */
					if (prev != NULL)
						prev->next = next;
					else
						*list = next;
					p_delstatus->next = NULL;
					pbs_delstatfree(p_delstatus);
					p_delstatus = prev;
					continue;
				}
			}
		}
		if (p_delstatus->code != PBSE_HISTJOBDELETED) {
			errtxt = pbse_to_txt(p_delstatus->code);
			if ((errtxt != NULL) && (p_delstatus->code != PBSE_HISTJOBDELETED)) {
				fprintf(stderr, "%s: %s %s\n", "qdel", errtxt, p_delstatus->name);
				any_failed = p_delstatus->code;
			}
		}
	}

	return any_failed;
}

/**
 * @brief	Get the mail suppression limit
 *
 * @param[in]	connect - connection fd
 *
 * @return int
 * @retval mail suppression limit
 */
static int
get_mail_suppress_count(int connect)
{
	struct batch_status *ss = NULL;
	struct attrl attr = {0};
	char *errmsg;
	char *keystr, *valuestr;
	int maillimit = 0;

	attr.name = ATTR_dfltqdelargs;
	attr.value = "";
	ss = pbs_statserver(connect, &attr, NULL);

	if (ss == NULL && pbs_errno != PBSE_NONE) {
		if ((errmsg = pbs_geterrmsg(connect)) != NULL)
			fprintf(stderr, "qdel: %s\n", errmsg);
		else
			fprintf(stderr, "qdel: Error %d\n", pbs_errno);
		exit(pbs_errno);
	}

	if (ss != NULL && ss->attribs != NULL && ss->attribs->value != NULL) {
		if (parse_equal_string(ss->attribs->value, &keystr, &valuestr)) {
			if (strcmp(keystr, "-Wsuppress_email") == 0)
				maillimit = atol(valuestr);
			else
				fprintf(stderr, "qdel: unsupported %s \'%s\'\n",
					ss->attribs->name, ss->attribs->value);
		}
	}
	pbs_statfree(ss);

	return maillimit;
}

/**
 * @brief	Helper function to handle job deletion for a given cluster
 *
 * @param[in]	clusterid - id of the cluster, currently the PBS_SERVER value
 * @param[in]	jobids - the list of jobs to delete on this server
 * @param[in]	numids - count of jobs to delete
 * @param[in]	dfltmail - mail suppression limit from the -W CLI value
 * @param[in]	warg - -W tokens ('force' et al)
 *
 * @return int
 * @retval pbs_errno
 */
static int
delete_jobs_for_cluster(char *clusterid, char **jobids, int numids, int dfltmail, char *warg)
{
	int connect;
	int mails; /* number of emails we can send */
	int numofjobs;
	struct batch_deljob_status *p_delstatus;
	int any_failed = 0;
	char warg1[MAX_TIME_DELAY_LEN + 7];
	svr_jobid_list_t *rmtsvr_jobid_list = NULL;
	svr_jobid_list_t *iter_remote = NULL;

	strcpy(warg1, NOMAIL);

	if (clusterid == NULL || jobids == NULL)
		return PBSE_INTERNAL;

	if (numids <= 0)
		return PBSE_NONE;

	connect = cnt2server(clusterid);
	if (connect <= 0) {
		fprintf(stderr, "Couldn't connect to cluster: %s\n", clusterid);
		return pbs_errno;
	}

	/* retrieve default: suppress_email from server: default_qdel_arguments */
	mails = dfltmail;
	if (mails == 0)
		mails = get_mail_suppress_count(connect);
	if (mails == 0)
		mails = QDEL_MAIL_SUPPRESS;

	/* First, delete mail limit number of jobs */
	numofjobs = (mails <= numids) ? mails : numids;
	p_delstatus = pbs_deljoblist(connect, jobids, numofjobs, warg);
	any_failed = process_deljobstat(clusterid, &p_delstatus, &rmtsvr_jobid_list);
	pbs_delstatfree(p_delstatus);

	if (numofjobs < numids) {	/* More jobs to delete */
		int any_failed_local = 0;
		/* when jobs to be deleted over the mail suppression limit, mail function is disabled
		* by sending the flag below to server via its extend field:
		*   "" -- delete a job with a mail
		*   "nomail" -- delete a job without sending a mail
		*   "force" -- force job to be deleted with a mail
		*   "nomailforce" -- force job to be deleted without sending a mail
		*   "nomaildeletehist" -- delete history of a job without sending mail
		*   "nomailforcedeletehist" -- force delete history of a job without sending mail.
		*
		* current warg1 "nomail" should be at start
		*/
		strcat(warg1, warg);
		pbs_strncpy(warg, warg1, sizeof(warg));

		p_delstatus = pbs_deljoblist(connect, &jobids[numofjobs], (numids - numofjobs), warg);
		any_failed_local = process_deljobstat(clusterid, &p_delstatus, &rmtsvr_jobid_list);
		pbs_delstatfree(p_delstatus);
		if (any_failed_local)
			any_failed = any_failed_local;
	}

	/* Delete any jobs which were found on remote servers */
	for (iter_remote = rmtsvr_jobid_list; iter_remote != NULL; iter_remote = iter_remote->next) {
		int fd;
		int any_failed_local = 0;

		fd = pbs_connect(iter_remote->svrname);
		if (fd > 0) {
			p_delstatus = pbs_deljoblist(fd, iter_remote->jobids, iter_remote->total_jobs, warg);
			any_failed_local = process_deljobstat(iter_remote->svrname, &p_delstatus, NULL);
			pbs_delstatfree(p_delstatus);
			if (any_failed_local)
				any_failed = any_failed_local;
			pbs_disconnect(fd);
		}
	}

	free_svrjobidlist(rmtsvr_jobid_list, 0);
	pbs_disconnect(connect);

	return any_failed;
}

/**
 * @brief	Helper function to group the total list of jobs by each cluster
 *
 * @param[in]	jobids - list of jobids
 * @param[in]	numjids - the number of job ids
 *
 * @return svr_jobid_list_t *
 * @retval the svr_jobid_list_t list of clusters and jobids within each
 * @retval NULL for error
 */
static svr_jobid_list_t *
group_jobs_by_cluster(char **jobids, int numjids, int *any_failed)
{
	int i;
	char server_out[PBS_MAXSERVERNAME];
	char job_id_out[PBS_MAXCLTJOBID];
	svr_jobid_list_t *svr_jobid_list_hd = NULL;
	char *dflt_server = pbs_default();

	/* Club jobs by each server */
	for (i = 0; i < numjids; i++) {
		if (get_server(jobids[i], job_id_out, server_out)) {
			fprintf(stderr, "qdel: illegally formed job identifier: %s\n", jobids[i]);
			*any_failed = 1;
			continue;
		}
		if (server_out[0] == '\0') {
			if (dflt_server != NULL)
				pbs_strncpy(server_out, dflt_server, sizeof(server_out));
		}
		if (server_out[0] == '\0') {
			fprintf(stderr, "Couldn't determine server name for job %s\n", jobids[i]);
			*any_failed = 1;
			continue;
		}

		if (add_jid_to_list_by_name(jobids[i], server_out, &svr_jobid_list_hd) != 0)
			return NULL;
	}

	return svr_jobid_list_hd;
}

int
main(argc, argv, envp) /* qdel */
int argc;
char **argv;
char **envp;
{
	int c;
	int errflg = 0;
	int any_failed = 0;
	char *pc;
	int forcedel = FALSE;
	int deletehist = FALSE;
	char *keystr, *valuestr;
	char **jobids = NULL;
	int dfltmail = 0;
	int numids = 0;
	/* -W no longer supports a time delay */
	/* max length is "nomailforcedeletehist" plus terminating '\0' */
	char warg[MAX_TIME_DELAY_LEN + 1];
	svr_jobid_list_t *jobsbycluster = NULL;
	svr_jobid_list_t *iter_list = NULL;
	int any_failed_local = 0;

	/*test for real deal or just version and exit*/

	PRINT_VERSION_AND_EXIT(argc, argv);

	if (initsocketlib())
		return 1;

	warg[0] = '\0';
	while ((c = getopt(argc, argv, GETOPT_ARGS)) != EOF) {
		switch (c) {
			case 'W':
				pc = optarg;
				if (strlen(pc) == 0) {
					fprintf(stderr, "qdel: illegal -W value\n");
					errflg++;
					break;
				}
				if (strcmp(pc, FORCE) == 0) {
					forcedel = TRUE;
					break;
				}
				if (parse_equal_string(optarg, &keystr, &valuestr)) {
					if (strcmp(keystr, SUPPRESS_EMAIL) == 0) {
						dfltmail = atol(valuestr);
						break;
					}
				}

				while (*pc != '\0') {
					if (! isdigit(*pc)) {
						fprintf(stderr, "qdel: illegal -W value\n");
						errflg++;
						break;
					}
					pc++;
				}
				break;
			case  'x' :
				deletehist = TRUE;
				break;
			default :
				errflg++;
		}
	}

	if (errflg || optind >= argc) {
		static char usage[] =
			"usage:\n"
		"\tqdel [-W force|suppress_email=X] [-x] job_identifier...\n"
		"\tqdel --version\n";
		fprintf(stderr, "%s", usage);
		exit(2);
	}

	if (forcedel && deletehist)
		snprintf(warg, sizeof(warg), "%s%s", FORCE, DELETEHISTORY);
	else if (forcedel)
		strcpy(warg, FORCE);
	else if (deletehist)
		strcpy(warg, DELETEHISTORY);

	/*perform needed security library initializations (including none)*/

	if (CS_client_init() != CS_SUCCESS) {
		fprintf(stderr, "qdel: unable to initialize security library.\n");
		exit(1);
	}

	jobids = &argv[optind];
	numids = argc - optind;
	if (jobids == NULL || numids <= 0) {
		/* No jobs to delete */
		return 0;
	}

	/* Send delete job list request by each cluster */
	jobsbycluster = group_jobs_by_cluster(jobids, numids, &any_failed);
	if (jobsbycluster == NULL) {
		exit(1);
	}
	for (iter_list = jobsbycluster; iter_list != NULL; iter_list = iter_list->next) {
		any_failed_local = delete_jobs_for_cluster(iter_list->svrname, iter_list->jobids,
						     iter_list->total_jobs, dfltmail, warg);
	}
	free_svrjobidlist(jobsbycluster, 1);
	if (any_failed_local)
		any_failed = any_failed_local;

	/* cleanup security library initializations before exiting */
	CS_close_app();

	if (any_failed == 0 && pbs_errno != PBSE_NONE)
		any_failed = PBSE_NONE;

	exit(any_failed);
}
