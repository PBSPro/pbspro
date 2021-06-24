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

#ifndef _BATCH_REQUEST_H
#define _BATCH_REQUEST_H
#ifdef __cplusplus
extern "C" {
#endif

#include "pbs_share.h"
#include "attribute.h"
#include "libpbs.h"
#include "net_connect.h"

#define PBS_SIGNAMESZ 16
#define MAX_JOBS_PER_REPLY 500

/* QueueJob */
struct rq_queuejob {
	char rq_destin[PBS_MAXSVRRESVID + 1];
	char rq_jid[PBS_MAXSVRJOBID + 1];
	pbs_list_head rq_attr; /* svrattrlist */
};

/* JobCredential */
struct rq_jobcred {
	int rq_type;
	long rq_size;
	char *rq_data;
};

/* UserCredential */
struct rq_usercred {
	char rq_user[PBS_MAXUSER + 1];
	int rq_type;
	long rq_size;
	char *rq_data;
};

/* Job File */
struct rq_jobfile {
	int rq_sequence;
	int rq_type;
	long rq_size;
	char rq_jobid[PBS_MAXSVRJOBID + 1];
	char *rq_data;
};

/* Hook File */
struct rq_hookfile {
	int rq_sequence;
	long rq_size;
	char rq_filename[MAXPATHLEN + 1];
	char *rq_data;
};

/*
 * job or destination id - used by RdyToCommit, Commit, RerunJob,
 * status ..., and locate job - is just a char *
 *
 * Manage - used by Manager, DeleteJob, ReleaseJob, ModifyJob
 */
struct rq_manage {
	int rq_cmd;
	int rq_objtype;
	char rq_objname[PBS_MAXSVRJOBID + 1];
	pbs_list_head rq_attr; /* svrattrlist */
};

/* DeleteJobList */
struct rq_deletejoblist {
	int rq_count;
	char **rq_jobslist;
	int rq_resume;
	int jobid_to_resume;
	int subjobid_to_resume;
};

/* Management - used by PBS_BATCH_Manager requests */
struct rq_management {
	struct rq_manage rq_manager;
	struct batch_reply *rq_reply;
	time_t rq_time;
};

/* ModifyVnode - used for node state changes */
struct rq_modifyvnode {
	struct pbsnode *rq_vnode_o; /* old/previous vnode state */
	struct pbsnode *rq_vnode; /* new/current vnode state */
};

/* HoldJob -  plus preference flag */
struct rq_hold {
	struct rq_manage rq_orig;
	int rq_hpref;
};

/* MessageJob */
struct rq_message {
	int rq_file;
	char rq_jid[PBS_MAXSVRJOBID + 1];
	char *rq_text;
};

/* RelnodesJob */
struct rq_relnodes {
	char rq_jid[PBS_MAXSVRJOBID + 1];
	char *rq_node_list;
};

/* PySpawn */
struct rq_py_spawn {
	char rq_jid[PBS_MAXSVRJOBID + 1];
	char **rq_argv;
	char **rq_envp;
};

/* MoveJob */
struct rq_move {
	char rq_jid[PBS_MAXSVRJOBID + 1];
	char rq_destin[(PBS_MAXSVRRESVID > PBS_MAXDEST ? PBS_MAXSVRRESVID : PBS_MAXDEST) + 1];
	char *run_exec_vnode;
	int orig_rq_type;
	void *ptask_runjob;
	int peersvr_stream;
};

/* Resource Query/Reserve/Free */
struct rq_rescq {
	int rq_rhandle;
	int rq_num;
	char **rq_list;
};

/* RunJob */
struct rq_runjob {
	char rq_jid[PBS_MAXSVRJOBID + 1];
	char *rq_destin;
	unsigned long rq_resch;
};

/* EndJob */
struct rq_endjob {
	struct job *rq_pjob;
	char rq_jid[PBS_MAXSVRJOBID + 1];
	char *rq_destin;
};

/* SignalJob */
struct rq_signal {
	char rq_jid[PBS_MAXSVRJOBID + 1];
	char rq_signame[PBS_SIGNAMESZ + 1];
};

/* Status (job, queue, server, hook) */
struct rq_status {
	char *rq_id; /* allow mulitple (job) ids */
	pbs_list_head rq_attr;
};

/* Select Job  and selstat */
struct rq_selstat {
	pbs_list_head rq_selattr;
	pbs_list_head rq_rtnattr;
};

/* TrackJob */
struct rq_track {
	int rq_hopcount;
	char rq_jid[PBS_MAXSVRJOBID + 1];
	char rq_location[PBS_MAXDEST + 1];
	char rq_state[2];
};

/* RegisterDependentJob */
struct rq_register {
	char rq_owner[PBS_MAXUSER + 1];
	char rq_svr[PBS_MAXSERVERNAME + 1];
	char rq_parent[PBS_MAXSVRJOBID + 1];
	char rq_child[PBS_MAXCLTJOBID + 1]; /* need separate entry for */
	int rq_dependtype;		    /* from server_name:port   */
	int rq_op;
	long rq_cost;
};

/* Authenticate request */
struct rq_auth {
	char rq_auth_method[MAXAUTHNAME + 1];
	char rq_encrypt_method[MAXAUTHNAME + 1];
	unsigned int rq_port;
};

/* Deferred Scheduler Reply */
struct rq_defschrpy {
	int rq_cmd;
	char *rq_id;
	int rq_err;
	char *rq_txt;
};

/* Copy/Delete Files (Server -> MOM Only) */

#define STDJOBFILE 1
#define JOBCKPFILE 2
#define STAGEFILE  3

#define STAGE_DIR_IN  0
#define STAGE_DIR_OUT 1

#define STAGE_DIRECTION 1 /* mask for setting/extracting direction of file copy from rq_dir */
#define STAGE_JOBDIR    2 /* mask for setting/extracting "sandbox" mode flag from rq_dir */

struct rq_cpyfile {
	char rq_jobid[PBS_MAXSVRJOBID + 1]; /* used in Copy & Delete */
	char rq_owner[PBS_MAXUSER + 1];     /* used in Copy only	   */
	char rq_user[PBS_MAXUSER + 1];      /* used in Copy & Delete */
	char rq_group[PBS_MAXGRPN + 1];     /* used in Copy only     */
	int rq_dir;			    /* direction and sandbox flags: used in Copy & Delete */
	pbs_list_head rq_pair;		    /* list of rqfpair,  used in Copy & Delete */
};

struct rq_cpyfile_cred {
	struct rq_cpyfile rq_copyfile; /* copy/delete info */
	int rq_credtype;	       /* cred type */
	size_t rq_credlen;	     /* credential length bytes */
	char *rq_pcred;		       /* encrpyted credential */
};

struct rq_cred {
	char rq_jobid[PBS_MAXSVRJOBID + 1];
	char rq_credid[PBS_MAXUSER + 1]; /* contains id specific for the used security mechanism */
	long rq_cred_validity;		 /* validity of provided credentials */
	int rq_cred_type;		 /* type of credentials like CRED_KRB5, CRED_TLS ... */
	char *rq_cred_data;		 /* credentials in base64 */
	size_t rq_cred_size;		 /* size of credentials */
};

struct rqfpair {
	pbs_list_link fp_link;
	int fp_flag;    /* 1 for std[out|err] 2 for stageout */
	char *fp_local; /* used in Copy & Delete */
	char *fp_rmt;   /* used in Copy only     */
};

struct rq_register_sched {
	char *rq_name;
};

/*
 * ok we now have all the individual request structures defined,
 * so here is the union ...
 */
struct batch_request {
	pbs_list_link rq_link;			/* linkage of all requests */
	struct batch_request *rq_parentbr;	/* parent request for job array request */
	int rq_refct;				/* reference count - child requests */
	int rq_type;				/* type of request */
	int rq_perm;				/* access permissions for the user */
	int rq_fromsvr;				/* true if request from another server */
	int rq_conn;				/* socket connection to client/server */
	int rq_orgconn;				/* original socket if relayed to MOM */
	int rq_extsz;				/* size of "extension" data */
	long rq_time;				/* time batch request created */
	char rq_user[PBS_MAXUSER + 1];		/* user name request is from */
	char rq_host[PBS_MAXHOSTNAME + 1];	/* name of host sending request */
	void *rq_extra;				/* optional ptr to extra info */
	char *rq_extend;			/* request "extension" data */
	int prot;				/* PROT_TCP or PROT_TPP */
	int tpp_ack;				/* send acks for this tpp stream? */
	char *tppcmd_msgid;			/* msg id for tpp commands */
	struct batch_reply rq_reply;		/* the reply area for this request */
	union indep_request {
		struct rq_register_sched rq_register_sched;
		struct rq_auth rq_auth;
		int rq_connect;
		struct rq_queuejob rq_queuejob;
		struct rq_jobcred rq_jobcred;
		struct rq_jobfile rq_jobfile;
		char rq_rdytocommit[PBS_MAXSVRJOBID + 1];
		char rq_commit[PBS_MAXSVRJOBID + 1];
		struct rq_manage rq_delete;
		struct rq_deletejoblist rq_deletejoblist;
		struct rq_hold rq_hold;
		char rq_locate[PBS_MAXSVRJOBID + 1];
		struct rq_manage rq_manager;
		struct rq_management rq_management;
		struct rq_modifyvnode rq_modifyvnode;
		struct rq_message rq_message;
		struct rq_relnodes rq_relnodes;
		struct rq_py_spawn rq_py_spawn;
		struct rq_manage rq_modify;
		struct rq_move rq_move;
		struct rq_register rq_register;
		struct rq_manage rq_release;
		char rq_rerun[PBS_MAXSVRJOBID + 1];
		struct rq_rescq rq_rescq;
		struct rq_runjob rq_run;
		struct rq_endjob rq_end;
		struct rq_selstat rq_select;
		int rq_shutdown;
		struct rq_signal rq_signal;
		struct rq_status rq_status;
		struct rq_track rq_track;
		struct rq_cpyfile rq_cpyfile;
		struct rq_cpyfile_cred rq_cpyfile_cred;
		int rq_failover;
		struct rq_usercred rq_usercred;
		struct rq_defschrpy rq_defrpy;
		struct rq_hookfile rq_hookfile;
		struct rq_preempt rq_preempt;
		struct rq_cred rq_cred;
	} rq_ind;
};

extern struct batch_request *alloc_br(int);
extern struct batch_request *copy_br(struct batch_request *);
extern void reply_ack(struct batch_request *);
extern void req_reject(int, int, struct batch_request *);
extern void req_reject_msg(int, int, struct batch_request *, int);
extern void reply_badattr(int, int, svrattrl *, struct batch_request *);
extern void reply_badattr_msg(int, int, svrattrl *, struct batch_request *, int);
extern int reply_text(struct batch_request *, int, char *);
extern int reply_send(struct batch_request *);
extern int reply_send_status_part(struct batch_request *);
extern int reply_jobid(struct batch_request *, char *, int);
extern int reply_jobid_msg(struct batch_request *, char *, int, int);
extern void reply_free(struct batch_reply *);
extern void dispatch_request(int, struct batch_request *);
extern void free_br(struct batch_request *);
extern int isode_request_read(int, struct batch_request *);
extern void req_stat_job(struct batch_request *);
extern void req_stat_resv(struct batch_request *);
extern void req_stat_resc(struct batch_request *);
extern void req_rerunjob(struct batch_request *);
extern void arrayfree(char **);

#ifdef PBS_NET_H
extern int authenticate_user(struct batch_request *, conn_t *);
#endif

#ifndef PBS_MOM
extern void req_confirmresv(struct batch_request *);
extern void req_connect(struct batch_request *);
extern void req_defschedreply(struct batch_request *);
extern void req_locatejob(struct batch_request *);
extern void req_manager(struct batch_request *);
extern void req_movejob(struct batch_request *);
extern void req_register(struct batch_request *);
extern void req_releasejob(struct batch_request *);
extern void req_rescq(struct batch_request *);
extern void req_runjob(struct batch_request *);
extern void req_selectjobs(struct batch_request *);
extern void req_stat_que(struct batch_request *);
extern void req_stat_svr(struct batch_request *);
extern void req_stat_sched(struct batch_request *);
extern void req_trackjob(struct batch_request *);
extern void req_stat_rsc(struct batch_request *);
extern void req_preemptjobs(struct batch_request *);
#else
extern void req_cpyfile(struct batch_request *);
extern void req_delfile(struct batch_request *);
extern void req_copy_hookfile(struct batch_request *);
extern void req_del_hookfile(struct batch_request *);
#if defined(PBS_SECURITY) && (PBS_SECURITY == KRB5)
extern void req_cred(struct batch_request *);
#endif
#endif

/* PBS Batch Request Decode/Encode routines */
extern int decode_DIS_Authenticate(int, struct batch_request *);
extern int decode_DIS_CopyFiles(int, struct batch_request *);
extern int decode_DIS_CopyFiles_Cred(int, struct batch_request *);
extern int decode_DIS_JobCred(int, struct batch_request *);
extern int decode_DIS_UserCred(int, struct batch_request *);
extern int decode_DIS_JobFile(int, struct batch_request *);
extern int decode_DIS_CopyHookFile(int, struct batch_request *);
extern int decode_DIS_DelHookFile(int, struct batch_request *);
extern int decode_DIS_JobObit(int, struct batch_request *);
extern int decode_DIS_Manage(int, struct batch_request *);
extern int decode_DIS_DelJobList(int, struct batch_request *);
extern int decode_DIS_MoveJob(int, struct batch_request *);
extern int decode_DIS_MessageJob(int, struct batch_request *);
extern int decode_DIS_ModifyResv(int, struct batch_request *);
extern int decode_DIS_PySpawn(int, struct batch_request *);
extern int decode_DIS_QueueJob(int, struct batch_request *);
extern int decode_DIS_Register(int, struct batch_request *);
extern int decode_DIS_RelnodesJob(int, struct batch_request *);
extern int decode_DIS_ReqExtend(int, struct batch_request *);
extern int decode_DIS_ReqHdr(int, struct batch_request *, int *, int *);
extern int decode_DIS_Rescl(int, struct batch_request *);
extern int decode_DIS_Rescq(int, struct batch_request *);
extern int decode_DIS_Run(int, struct batch_request *);
extern int decode_DIS_ShutDown(int, struct batch_request *);
extern int decode_DIS_SignalJob(int, struct batch_request *);
extern int decode_DIS_Status(int, struct batch_request *);
extern int decode_DIS_TrackJob(int, struct batch_request *);
extern int decode_DIS_replySvr(int, struct batch_reply *);
extern int decode_DIS_svrattrl(int, pbs_list_head *);
extern int decode_DIS_Cred(int, struct batch_request *);
extern int encode_DIS_failover(int, struct batch_request *);
extern int encode_DIS_CopyFiles(int, struct batch_request *);
extern int encode_DIS_CopyFiles_Cred(int, struct batch_request *);
extern int encode_DIS_JobObit(int, struct batch_request *);
extern int encode_DIS_Register(int, struct batch_request *);
extern int encode_DIS_TrackJob(int, struct batch_request *);
extern int encode_DIS_reply(int, struct batch_reply *);
extern int encode_DIS_replyTPP(int, char *, struct batch_reply *);
extern int encode_DIS_svrattrl(int, svrattrl *);
extern int encode_DIS_Cred(int, char *, char *, int, char *, size_t, long);
extern int dis_request_read(int, struct batch_request *);
extern int dis_reply_read(int, struct batch_reply *, int);
extern int decode_DIS_PreemptJobs(int, struct batch_request *);

#ifdef __cplusplus
}
#endif
#endif /* _BATCH_REQUEST_H */
