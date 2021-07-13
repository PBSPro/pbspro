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

#ifndef _LIBPBS_H
#define _LIBPBS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <limits.h>
#include "pbs_ifl.h"
#include "list_link.h"
#include "pbs_error.h"
#include "pbs_internal.h"
#include "pbs_client_thread.h"
#include "net_connect.h"
#include "dis.h"

#define VALUE(str) #str
#define TOSTR(str) VALUE(str)

/* Protocol types when connecting to another server (eg mom) */
#define PROT_INVALID -1
#define PROT_TCP 0 /* For TCP based connection */
#define PROT_TPP 1 /* For TPP based connection */

#define PBS_BATCH_PROT_TYPE 2
#define PBS_BATCH_PROT_VER_OLD  1
#define PBS_BATCH_PROT_VER  2
#define SCRIPT_CHUNK_Z (65536)
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#ifndef EOF
#define EOF (-1)
#endif

/* enums for standard job files */
enum job_file {
	JScript,
	StdIn,
	StdOut,
	StdErr,
	Chkpt
};

/*
 * This variable has been moved to Thread local storage
 * The define points to a function pointer which locates
 * the actual variable from the TLS of the calling thread
 */
#ifndef __PBS_CURRENT_USER
#define __PBS_CURRENT_USER
extern char *__pbs_current_user_location(void);
#define pbs_current_user (__pbs_current_user_location())
#endif

#ifndef __PBS_TCP_TIMEOUT
#define __PBS_TCP_TIMEOUT
extern time_t *__pbs_tcptimeout_location(void);
#define pbs_tcp_timeout (*__pbs_tcptimeout_location())
#endif

#ifndef __PBS_TCP_INTERRUPT
#define __PBS_TCP_INTERRUPT
extern int *__pbs_tcpinterrupt_location(void);
#define pbs_tcp_interrupt (*__pbs_tcpinterrupt_location())
#endif

#ifndef __PBS_TCP_ERRNO
#define __PBS_TCP_ERRNO
extern int *__pbs_tcperrno_location(void);
#define pbs_tcp_errno (*__pbs_tcperrno_location())
#endif

extern char pbs_current_group[];

#define NCONNECTS 50 /* max connections per client */
#define PBS_MAX_CONNECTIONS 5000 /* Max connections in the connections array */
#define PBS_LOCAL_CONNECTION INT_MAX

typedef struct pbs_conn {
	int ch_errno;		  /* last error on this connection */
	char *ch_errtxt;	  /* pointer to last server error text	*/
	pthread_mutex_t ch_mutex; /* serialize connection between threads */
	pbs_tcp_chan_t *ch_chan;  /* pointer tcp chan structure for this connection */
} pbs_conn_t;

int destroy_connection(int);
int set_conn_errtxt(int, const char *);
char * get_conn_errtxt(int);
int set_conn_errno(int, int);
int get_conn_errno(int);
pbs_tcp_chan_t * get_conn_chan(int);
int set_conn_chan(int, pbs_tcp_chan_t *);
pthread_mutex_t * get_conn_mutex(int);

#define SVR_CONN_STATE_DOWN 0
#define SVR_CONN_STATE_UP 1

int get_num_servers(void);

/* max number of preempt orderings */
#define PREEMPT_ORDER_MAX 20

typedef struct svr_conn {
	int sd;                      /* File descriptor for the open socket */
	int state;                   /* Connection state */
	char name[PBS_MAXSERVERNAME + 1];  /* server name */
	int port;                    /* server port */
} svr_conn_t;

typedef struct svr_conns_list {
	int cfd;
	svr_conn_t **conn_arr;
	struct svr_conns_list *next; /* Pointer to next set of server connections,
								in case client calls pbs_connect multiple times */
} svr_conns_list_t;

/* PBS Batch Reply Structure */

/* reply to Select Job Request */
struct brp_select {
	struct brp_select *brp_next;
	char brp_jobid[PBS_MAXSVRJOBID + 1];
};

/* reply to Status Job/Queue/Server Request */
struct brp_status {
	pbs_list_link brp_stlink;
	int brp_objtype;
	char brp_objname[(PBS_MAXSVRJOBID > PBS_MAXDEST ? PBS_MAXSVRJOBID : PBS_MAXDEST) + 1];
	pbs_list_head brp_attr; /* head of svrattrlist */
};

/* reply to Resource Query Request */
struct brp_rescq {
	int brq_number; /* number of items in following arrays */
	int *brq_avail;
	int *brq_alloc;
	int *brq_resvd;
	int *brq_down;
};

struct rq_preempt {
	int count;
	preempt_job_info *ppj_list;
};

typedef struct rq_preempt brp_preempt_jobs;

#define BATCH_REPLY_CHOICE_NULL		1	/* no reply choice, just code */
#define BATCH_REPLY_CHOICE_Queue	2	/* Job ID, see brp_jid */
#define BATCH_REPLY_CHOICE_RdytoCom	3	/* select, see brp_jid */
#define BATCH_REPLY_CHOICE_Commit	4	/* commit, see brp_jid */
#define BATCH_REPLY_CHOICE_Select	5	/* select, see brp_select */
#define BATCH_REPLY_CHOICE_Status	6	/* status, see brp_status */
#define BATCH_REPLY_CHOICE_Text		7	/* text, see brp_txt */
#define BATCH_REPLY_CHOICE_Locate	8	/* locate, see brp_locate */
#define BATCH_REPLY_CHOICE_RescQuery	9	/* Resource Query */
#define BATCH_REPLY_CHOICE_PreemptJobs	10	/* Preempt Job */
#define BATCH_REPLY_CHOICE_Delete		11  /* Delete Job status */

/*
 * the following is the basic Batch Reply structure
 */
struct batch_reply
{
	int brp_code;
	int brp_auxcode;
	int brp_choice; /* the union discriminator */
	int brp_is_part;
	int brp_count;
	int brp_type;
	struct batch_status *last;
	union {
		char brp_jid[PBS_MAXSVRJOBID + 1];
		struct brp_select *brp_select; /* select replies */
		pbs_list_head brp_status; /* status (svr) replies */
		struct batch_status *brp_statc; /* status (cmd) replies) */
		struct {
			int tot_jobs;
			int tot_rpys;
			int tot_arr_jobs;
			struct batch_deljob_status *brp_delstatc;
		} brp_deletejoblist;
		struct {
			int brp_txtlen;
			char *brp_str;
		} brp_txt; /* text and credential reply */
		char brp_locate[PBS_MAXDEST + 1];
		struct brp_rescq brp_rescq; /* query resource reply */
		brp_preempt_jobs brp_preempt_jobs; /* preempt jobs reply */
	} brp_un;
};

/*
 * The Batch Request ID numbers
 */
#define PBS_BATCH_Connect		0
#define PBS_BATCH_QueueJob		1
/* Unused -- #define PBS_BATCH_JobCred 2 */
#define PBS_BATCH_jobscript		3
#define PBS_BATCH_RdytoCommit		4
#define PBS_BATCH_Commit		5
#define PBS_BATCH_DeleteJob		6
#define PBS_BATCH_HoldJob		7
#define PBS_BATCH_LocateJob		8
#define PBS_BATCH_Manager		9
#define PBS_BATCH_MessJob		10
#define PBS_BATCH_ModifyJob		11
#define PBS_BATCH_MoveJob		12
#define PBS_BATCH_ReleaseJob		13
#define PBS_BATCH_Rerun			14
#define PBS_BATCH_RunJob		15
#define PBS_BATCH_SelectJobs		16
#define PBS_BATCH_Shutdown		17
#define PBS_BATCH_SignalJob		18
#define PBS_BATCH_StatusJob		19
#define PBS_BATCH_StatusQue		20
#define PBS_BATCH_StatusSvr		21
#define PBS_BATCH_TrackJob		22
#define PBS_BATCH_AsyrunJob		23
#define PBS_BATCH_Rescq			24
#define PBS_BATCH_ReserveResc		25
#define PBS_BATCH_ReleaseResc		26
#define PBS_BATCH_FailOver		27
#define PBS_BATCH_StageIn		48
/* Unused -- #define PBS_BATCH_AuthenResvPort 49 */
#define PBS_BATCH_OrderJob		50
#define PBS_BATCH_SelStat		51
#define PBS_BATCH_RegistDep		52
#define PBS_BATCH_CopyFiles		54
#define PBS_BATCH_DelFiles		55
#define PBS_BATCH_JobObit		56
#define PBS_BATCH_MvJobFile		57
#define PBS_BATCH_StatusNode		58
#define PBS_BATCH_Disconnect		59
/* Unused -- #define PBS_BATCH_CopyFiles_Cred 60 */
/* Unused -- #define PBS_BATCH_DelFiles_Cred 61 */
#define PBS_BATCH_JobCred		62
#define PBS_BATCH_CopyFiles_Cred	63
#define PBS_BATCH_DelFiles_Cred		64
/* Unused -- #define PBS_BATCH_GSS_Context 65 */
#define PBS_BATCH_SubmitResv		70
#define PBS_BATCH_StatusResv		71
#define PBS_BATCH_DeleteResv		72
#define PBS_BATCH_BeginResv		76
#define PBS_BATCH_UserCred		73
/* Unused -- #define PBS_BATCH_UserMigrate		74 */
#define PBS_BATCH_ConfirmResv		75
#define PBS_BATCH_DefSchReply		80
#define PBS_BATCH_StatusSched		81
#define PBS_BATCH_StatusRsc		82
#define PBS_BATCH_StatusHook		83
#define PBS_BATCH_PySpawn		84
#define PBS_BATCH_CopyHookFile		85
#define PBS_BATCH_DelHookFile		86
/* Unused -- #define PBS_BATCH_MomRestart 87 */
/* Unused -- #define PBS_BATCH_AuthExternal 88 */
#define PBS_BATCH_HookPeriodic    89
#define PBS_BATCH_RelnodesJob     90
#define PBS_BATCH_ModifyResv      91
#define PBS_BATCH_ResvOccurEnd    92
#define PBS_BATCH_PreemptJobs     93
#define PBS_BATCH_Cred            94
#define PBS_BATCH_Authenticate    95
#define PBS_BATCH_ModifyJob_Async	96
#define PBS_BATCH_AsyrunJob_ack  	97
#define PBS_BATCH_RegisterSched  	98
#define PBS_BATCH_ModifyVnode    	99
#define PBS_BATCH_DeleteJobList  	100
#define PBS_BATCH_ServerReady    	101

#define PBS_BATCH_FileOpt_Default	0
#define PBS_BATCH_FileOpt_OFlg		1
#define PBS_BATCH_FileOpt_EFlg		2

#define PBS_IFF_CLIENT_ADDR "PBS_IFF_CLIENT_ADDR"

/* time out values for tcp_dis read/write */
#define PBS_DIS_TCP_TIMEOUT_CONNECT	10
#define PBS_DIS_TCP_TIMEOUT_REPLY	10
#define PBS_DIS_TCP_TIMEOUT_SHORT	30
#define PBS_DIS_TCP_TIMEOUT_RERUN	45 /* timeout used in pbs_rerunjob() */
#define PBS_DIS_TCP_TIMEOUT_LONG	600
#define PBS_DIS_TCP_TIMEOUT_VLONG	10800

#define FAILOVER_Register	0 /* secondary server register with primary */
#define FAILOVER_HandShake	1 /* handshake from secondary to primary */
#define FAILOVER_PrimIsBack	2 /* Primary is taking control again */
#define FAILOVER_SecdShutdown	3 /* Primary going down, secondary go down */
#define FAILOVER_SecdGoInactive	4 /* Primary down, secondary go inactive */
#define FAILOVER_SecdTakeOver	5 /* Primary down, secondary take over */

#define EXTEND_OPT_IMPLICIT_COMMIT ":C:" /* option added to pbs_submit() extend parameter to request implicit commit */
#define EXTEND_OPT_NEXT_MSG_TYPE "next_msg_type"
#define EXTEND_OPT_NEXT_MSG_PARAM "next_msg_param"

int is_compose(int, int);
int ps_compose(int, int);
int is_compose_cmd(int, int, char **);
void PBS_free_aopl(struct attropl *);
void advise(char *, ...);
int PBSD_commit(int, char *, int, char **, char *);
int PBSD_jcred(int, int, char *, int, int, char **);
int PBSD_jscript(int, char *, int, char **);
int PBSD_jscript_direct(int, char *, int, char **);
int PBSD_copyhookfile(int, char *, int, char **);
int PBSD_delhookfile(int, char *, int, char **);
int PBSD_mgr_put(int, int, int, int, char *, struct attropl *, char *, int, char **);
int PBSD_manager(int, int, int, int, char *, struct attropl *, char *);
int PBSD_msg_put(int, char *, int, char *, char *, int, char **);
int PBSD_relnodes_put(int, char *, char *, char *, int, char **);
int PBSD_py_spawn_put(int, char *, char **, char **, int, char **);
int PBSD_sig_put(int, char *, char *, char *, int, char **);
int PBSD_term_put(int, int, char *);
int PBSD_jobfile(int, int, char *, char *, enum job_file, int, char **);
int PBSD_status_put(int, int, char *, struct attrl *, char *, int, char **);
int PBSD_select_put(int, int, struct attropl *, struct attrl *, char *);
struct batch_reply *PBSD_rdrpy(int);
struct batch_reply *PBSD_rdrpy_sock(int, int *, int prot);
void PBSD_FreeReply(struct batch_reply *);
struct batch_status *PBSD_status(int, int, char *, struct attrl *, char *);
struct batch_status *PBSD_status_random(int c, int function, char *id, struct attrl *attrib, char *extend, int parent_object);
struct batch_status *PBSD_status_aggregate(int c, int cmd, char *id, void *attrib, char *extend, int parent_object, struct attrl *);
struct batch_status *PBSD_status_get(int c, struct batch_status **last, int *obj_type, int prot);
char *PBSD_queuejob(int, char *, char *, struct attropl *, char *, int, char **, int *);
int decode_DIS_svrattrl(int, pbs_list_head *);
int decode_DIS_attrl(int, struct attrl **);
int decode_DIS_JobId(int, char *);
int decode_DIS_replyCmd(int, struct batch_reply *, int);
int encode_DIS_JobCred(int, int, char *, int);
int encode_DIS_UserCred(int, char *, int, char *, int);
int encode_DIS_JobFile(int, int, char *, int, char *, int);
int encode_DIS_JobId(int, char *);
int encode_DIS_Manage(int, int, int, char *, struct attropl *);
int encode_DIS_MessageJob(int, char *, int, char *);
int encode_DIS_MoveJob(int, char *, char *);
int encode_DIS_ModifyResv(int, char *, struct attropl *);
int encode_DIS_RelnodesJob(int, char *, char *);
int encode_DIS_PySpawn(int, char *, char **, char **);
int encode_DIS_QueueJob(int, char *, char *, struct attropl *);
int encode_DIS_SubmitResv(int, char *, struct attropl *);
int encode_DIS_JobCredential(int, int, char *, int);
int encode_DIS_ReqExtend(int, char *);
int encode_DIS_ReqHdr(int, int, char *);
int encode_DIS_Rescq(int, char **, int);
int encode_DIS_Run(int, char *, char *, unsigned long);
int encode_DIS_ShutDown(int, int);
int encode_DIS_SignalJob(int, char *, char *);
int encode_DIS_Status(int, char *, struct attrl *);
int encode_DIS_attrl(int, struct attrl *);
int encode_DIS_attropl(int, struct attropl *);
int encode_DIS_CopyHookFile(int, int, char *, int, char *);
int encode_DIS_DelHookFile(int, char *);
int encode_DIS_JobsList(int, char **, int);
char *PBSD_submit_resv(int, char *, struct attropl *, char *);
int DIS_reply_read(int, struct batch_reply *, int);
int tcp_pre_process(conn_t *);
char *PBSD_modify_resv(int, char *, struct attropl *, char *);
int PBSD_cred(int, char *, char *, int, char *, long, int, char **);
int PBSD_server_ready(int);
int tcp_send_auth_req(int, unsigned int, char *, char *, char *);
svr_conn_t **get_conn_svr_instances(int);
int pbs_register_sched(const char *sched_id, int primary_conn_id, int secondary_conn_id);
int get_svr_inst_fd(int vfd, char *svr_inst_id);
int random_srv_conn(int fd, svr_conn_t **svr_conns);
int get_obj_location_hint(char *, int obj_type);
char *PBS_get_server(char *, char *, uint *);
int encode_DIS_JobsList(int sock, char **jobs_list, int numofjobs);
int get_server_fd_from_jid(int c, char *jobid);
int multi_svr_op(int fd);
int get_job_svr_inst_id(int c, char *job_id);

int pbs_register_sched_msvr_instance(const char *sched_id, int primary_conn_id, int secondary_conn_id);
void pbs_connect_msvr_instance(svr_conn_t *conn);
int pbs_disconnect_msvr_instance(svr_conn_t *svr_conn);
#ifdef __cplusplus
}
#endif
#endif /* _LIBPBS_H */
