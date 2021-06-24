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

#ifndef	_PBS_JOB_H
#define	_PBS_JOB_H
#ifdef	__cplusplus
extern "C" {
#endif

#include "list_link.h"
#include "attribute.h"
#include "range.h"
#include "Long.h"

/*
 * job.h - structure definations for job objects
 *
 * Include Files Required:
 *	<sys/types.h>
 *	"list_link.h"
 *	"attribute.h"
 *	"server_limits.h"
 *	"reservation.h"
 */

#ifndef	_SERVER_LIMITS_H
#include "server_limits.h"
#endif
#include "work_task.h"

#ifdef PBS_MOM /* For the var_table used in env funcs */
/* struct var_table = used to hold environment variables for the job */

struct var_table {
	char **v_envp;
	int    v_ensize;
	int    v_used;
};
#endif

/*
 * Dependent Job Structures
 *
 * This set of structures are used by the server to track job
 * dependency.
 *
 * The depend (parent) structure is used to record the type of
 * dependency.  It also heads the list of depend_job related via this type.
 * For a type of "sycnto", the number of jobs expected, registered and
 * ready are also recorded.
 */

struct depend {
	pbs_list_link dp_link;	/* link to next dependency, if any       */
	short	  dp_type;	/* type of dependency (all) 	         */
	short	  dp_numexp;	/* num jobs expected (on or syncct only) */
	short	  dp_numreg;	/* num jobs registered (syncct only)     */
	short	  dp_released;	/* This job released to run (syncwith)   */
	short	  dp_numrun;    /* num jobs supposed to run		 */
	pbs_list_head dp_jobs;	/* list of related jobs  (all)           */
};

/*
 * The depend_job structure is used to record the name and location
 * of each job which is involved with the dependency
 */

struct depend_job {
	pbs_list_link dc_link;
	short	dc_state;	/* released / ready to run (syncct)	 */
	long	dc_cost;	/* cost of this child (syncct)		 */
	char	dc_child[PBS_MAXSVRJOBID+1]; /* child (dependent) job	 */
	char	dc_svr[PBS_MAXSERVERNAME+1]; /* server owning job	 */
};

/*
 * Warning: the relation between the numbers assigned to after* and before*
 * is critical.
 */
#define JOB_DEPEND_TYPE_AFTERSTART	 0
#define JOB_DEPEND_TYPE_AFTEROK		 1
#define JOB_DEPEND_TYPE_AFTERNOTOK	 2
#define JOB_DEPEND_TYPE_AFTERANY	 3
#define JOB_DEPEND_TYPE_BEFORESTART	 4
#define JOB_DEPEND_TYPE_BEFOREOK	 5
#define JOB_DEPEND_TYPE_BEFORENOTOK	 6
#define JOB_DEPEND_TYPE_BEFOREANY	 7
#define JOB_DEPEND_TYPE_ON		 8
#define JOB_DEPEND_TYPE_RUNONE		 9
#define JOB_DEPEND_NUMBER_TYPES		11

#define JOB_DEPEND_OP_REGISTER		1
#define JOB_DEPEND_OP_RELEASE		2
#define JOB_DEPEND_OP_READY		3
#define JOB_DEPEND_OP_DELETE		4
#define JOB_DEPEND_OP_UNREG		5

/*
 * The badplace structure is used to keep track of destinations
 * which have been tried by a route queue and given a "reject"
 * status back, see svr_movejob.c.
 */
typedef struct	badplace {
	pbs_list_link	bp_link;
	char		bp_dest[PBS_MAXROUTEDEST+1];
} badplace;

/*
 * The grpcache structure defined here is used by MOM to maintain the
 * home directory, uid and gid of the user name under
 * which the job is running.
 * The information is keep here rather than make repeated hits on the
 * password and group files.
 */
struct grpcache {
	uid_t  gc_uid;		/* uid job will execute under */
	gid_t  gc_gid;		/* gid job will execute under */
	gid_t  gc_rgid;		/* login gid of user uid      */
	char   gc_homedir[1];	/* more space allocated as part of this	 */
	/* structure following here		 */
};

/*
 * Job attributes/resources are maintained in one of two ways.
 * Most of the attributes are maintained in a decoded or parsed form.
 * This allows quick access to the attribute and resource values
 * when making decisions about the job (scheduling, routing, ...),
 *
 * Any attribute or resource which is not recognized on this server
 * are kept in an "attrlist", a linked list of the "external"
 * form (attr_extern, see attribute.h).  These are maintained because
 * the job may be passed on to another server (route or qmove) that
 * does recognize them.
 * See the job structure entry ji_attrlist and the attrlist structure.
 */


/*
 * The following job_atr enum provide an index into the array of
 * decoded job attributes, for quick access.
 * Most of the attributes here are "public", but some are Read Only,
 * Private, or even Internal data items; maintained here because of
 * their variable size.
 *
 * "JOB_ATR_LAST" must be the last value as its number is used to
 * define the size of the array.
 */

enum job_atr {
#include "job_attr_enum.h"
#include "site_job_attr_enum.h"
	JOB_ATR_UNKN, /* the special "unknown" type */
	JOB_ATR_LAST  /* This MUST be LAST	*/
};

/* the following enum defines the type of checkpoint to be done */
/* none, based on cputime or walltime.   Used only by Mom       */
enum PBS_Chkpt_By {
	PBS_CHECKPOINT_NONE,	/* no checkpoint                   */
	PBS_CHECKPOINT_CPUT,	/* checkpoint by cputime interval  */
	PBS_CHECKPOINT_WALLT	/* checkpoint by walltime interval */
};

typedef struct string_and_number_t {
	char	*str;
	int	num;
} string_and_number_t;

typedef struct resc_limit {		/* per node limits for Mom	*/
	int	  rl_ncpus;		/* number of cpus		*/
	int	  rl_ssi;		/* ssinodes (for irix cpusets	*/
	long long rl_mem;		/* working set size (real mem)	*/
	long long rl_vmem;		/* total mem space (virtual)	*/
	int	  rl_naccels;		/* number of accelerators	*/
	long long rl_accel_mem;		/* accelerator mem (real mem)	*/
	pbs_list_head rl_other_res;	/* list of all other resources found in execvnode and sched select*/
	unsigned int  rl_res_count;	/* total count of resources */
	char	  *chunkstr;		/* chunk represented */
	int	  chunkstr_sz;		/* size of chunkstr */
	char	  *chunkspec;		/* the spec in select string representing the chunk */
	string_and_number_t host_chunk[2]; /* chunks representing exec_host/exec_host2  */
} resc_limit_t;

/*
 * The "definations" for the job attributes are in the following array,
 * it is also indexed by the JOB_ATR_... enums.
 */

extern attribute_def job_attr_def[];
extern void *job_attr_idx;

#ifndef PBS_MOM

typedef enum histjob_type {
	T_FIN_JOB,	/* Job finished execution or terminated */
	T_MOV_JOB,	/* Job moved to different destination */
	T_MOM_DOWN	/* Non-rerunnable Job failed because of
			 MOM failure.*/
} histjob_type;

#endif /* SERVER only! */

#ifdef	PBS_MOM
#include "tm_.h"

/*
 * host_vlist - an array of these is hung off of the hnodent for this host,
 *	i.e. if the hnodent index equals that in ji_nodeid;
 *	The array contains one entry for each vnode allocated for this job
 *	from this host.
 *
 *	WARNING: This array only exists for cpuset machines !
 *
 *		 The mom hooks code (src/resmom/mom_hook_func.c:run_hook())
 *		 depends on this structure. If newer resource members
 *		 are added (besides hv_ncpus, hv_mem), then please update
 *		 mom hooks code, as well as the appropriate RFE.
 *
 */
typedef struct host_vlist {
	char		hv_vname[PBS_MAXNODENAME+1]; /* vnode name     */
	int		hv_ncpus;		     /* ncpus assigned */
	size_t		hv_mem;			     /* mem assigned   */
} host_vlist_t;

/*
 **	Track nodes with an array of structures which each
 **	point to a list of events
 */
typedef	struct	hnodent {
	tm_host_id	hn_node;	/* host (node) identifier (index) */
	char	       *hn_host;	/* hostname of node */
	int		hn_port;	/* port of Mom */
	int		hn_stream;	/* stream to MOM on node */
	time_t		hn_eof_ts; 	/* timestamp of when the stream went down */
	int		hn_sister;	/* save error for KILL_JOB event */
	int		hn_nprocs;	/* num procs allocated to this node */
	int		hn_vlnum;	/* num entries in vlist */
	host_vlist_t   *hn_vlist;	/* list of vnodes allocated */
	resc_limit_t	hn_nrlimit;	/* resc limits per node */
	void	       *hn_setup;	/* save any setup info here */
	pbs_list_head	hn_events;	/* pointer to list of events */
} hnodent;

typedef struct vmpiprocs {
	tm_node_id	vn_node;	/* user's vnode identifier */
	hnodent	       *vn_host;	/* parent (host) nodeent entry */
	char	       *vn_hname;	/* host name for MPI, if null value */
	/* use vn_host->hn_host             */
	/* host name for MPI */
	char	       *vn_vname;	/* vnode name */
	int		vn_cpus;	/* number of cpus allocated to proc */
	int		vn_mpiprocs;	/* number for mpiprocs  */
	int		vn_threads;	/* number for OMP_NUM_THREADS */
	long long	vn_mem;		/* working set size (real mem)	*/
	long long	vn_vmem;	/* total mem space (virtual)	*/
	int		vn_naccels;	/* number of accelerators */
	int		vn_need_accel;	/* should we reserve the accelerators */
	char	       *vn_accel_model;	/* model of the desired accelerator */
	long long	vn_accel_mem;	/* amt of accelerator memory wanted */
} vmpiprocs;

/* the following enum defines if a node resource is to be reported by Mom */
enum PBS_NodeRes_Status {
	PBS_NODERES_ACTIVE,	/* resource reported from a non-released node */
	PBS_NODERES_DELETE	/* resource reported from a released node */
};

/*
 * Mother Superior gets to hold an array of information from each
 * of the other nodes for resource usage.
 */
typedef struct	noderes {
	char		*nodehost;	/* corresponding node name */
	long		nr_cput;	/* cpu time */
	long		nr_mem;		/* memory */
	long		nr_cpupercent;  /* cpu percent */
	attribute	nr_used;	/* node resources used */
	enum PBS_NodeRes_Status nr_status;
} noderes;

/* State for a sister */

#define SISTER_OKAY		0
#define SISTER_KILLDONE		1000
#define SISTER_BADPOLL		1001
#define SISTER_EOF		1099

/* job flags for ji_flags (mom only) */

#define	MOM_CHKPT_ACTIVE	0x0001	/* checkpoint in progress */
#define	MOM_CHKPT_POST		0x0002	/* post checkpoint call returned */
#define	MOM_SISTER_ERR		0x0004	/* a sisterhood operation failed */
#define	MOM_NO_PROC		0x0008	/* no procs found for job */
#define	MOM_RESTART_ACTIVE	0x0010	/* restart in progress */


#define PBS_MAX_POLL_DOWNTIME 300 /* 5 minutes by default */
#endif	/* MOM */

/*
 * specific structures for Job Array attributes
 */

/* subjob index table */
typedef struct ajinfo {
	int tkm_ct;			  /* count of original entries in table */
	int tkm_start;			  /* start of range (x in x-y:z) */
	int tkm_end;			  /* end of range (y in x-y:z) */
	int tkm_step;			  /* stepping factor for range (z in x-y:z) */
	int tkm_flags;			  /* special flags for array job */
	int tkm_subjsct[PBS_NUMJOBSTATE]; /* count of subjobs in various states */
	int tkm_dsubjsct;		  /* count of deleted subjobs */
	range *trm_quelist;		  /* pointer to range list */
} ajinfo_t;

/*
 * Discard Job Structure,  see Server's discard_job function
 *	Used to record which Mom has responded to when we need to tell them
 *	to discard a running job because of problems with a Mom
 */

struct jbdscrd {
	struct	machine_info *jdcd_mom;	/* ptr to Mom */
	int		 jdcd_state;	/* 0 - waiting on her */
};
#define JDCD_WAITING 0	/* still waiting to hear from this Mom */
#define JDCD_REPLIED 1	/* this Mom has replied to the discard job */
#define JDCD_DOWN   -1	/* this Mom is down */


/* Special array job flags in tkm_flags */
#define TKMFLG_NO_DELETE 0x01 /* delete subjobs in progess */
#define TKMFLG_CHK_ARRAY 0x02 /* chk_array_doneness() already in call stack */

/* Structure for block job reply processing */
struct block_job_reply {
	char jobid[PBS_MAXSVRJOBID + 1];
	char client[PBS_MAXHOSTNAME + 1];
	int port;
	int exitstat;
	time_t reply_time;	/* The timestamp at which the block job tried it's first attempt to reply */
	char *msg;	/* Abort message to be send to client */
	int fd;
};
#define BLOCK_JOB_REPLY_TIMEOUT 60

/*
 * THE JOB
 *
 * This structure is used by the server to maintain internal
 * quick access to the state and status of each job.
 * There is one instance of this structure per job known by the server.
 *
 * This information must be PRESERVED and is done so by updating the
 * job file in the jobs subdirectory which corresponds to this job.
 *
 * ji_state is the state of the job.  It is kept up front to provide for a
 * "quick" update of the job state with minimum rewritting of the job file.
 * Which is why the sub-struct ji_qs exists, that is the part which is
 * written on the "quick" save.  Update the symbolic constants JSVERSION_**
 * if any changes to the format of the "quick-save" area are made.
 *
 * The unparsed string set forms of the attributes (including resources)
 * are maintained in the struct attrlist as discussed above.
 */


#define	JSVERSION_18	800	/* 18 denotes the PBS version and it covers the job structure from >= 13.x to <= 18.x */
#define	JSVERSION_19	1900	/* 1900 denotes the 19.x.x version */
#define	JSVERSION	2200	/* denotes 22.x and newer */
#define	ji_taskid	ji_extended.ji_ext.ji_taskidx
#define	ji_nodeid	ji_extended.ji_ext.ji_nodeidx

enum bg_hook_request {
	BG_NONE,
	BG_IS_DISCARD_JOB,
	BG_PBS_BATCH_DeleteJob,
	BG_PBSE_SISCOMM,
	BG_IM_DELETE_JOB_REPLY,
	BG_IM_DELETE_JOB,
	BG_IM_DELETE_JOB2,
	BG_CHECKPOINT_ABORT
};

struct job {

	/*
	 * Note: these members, upto ji_qs, are not saved to disk
	 * IMPORTANT: if adding to this are, see create_subjob()
	 * in array_func.c; add the copy of the required elements
	 */

	pbs_list_link ji_alljobs;	     /* links to all jobs in server */
	pbs_list_link ji_jobque;	     /* SVR: links to jobs in same queue, MOM: links to polled jobs */
	pbs_list_link ji_unlicjobs;	     /* links to unlicensed jobs */
	int ji_momhandle;		     /* open connection handle to MOM */
	int ji_mom_prot;		     /* PROT_TCP or PROT_TPP */
	struct batch_request *ji_rerun_preq; /* outstanding rerun request */
#ifdef PBS_MOM
	void *ji_pending_ruu;			    /* pending last update */
	struct batch_request *ji_preq;		    /* outstanding request */
	struct grpcache *ji_grpcache;		    /* cache of user's groups */
	enum PBS_Chkpt_By ji_chkpttype;		    /* checkpoint type  */
	time_t ji_chkpttime;			    /* periodic checkpoint time */
	time_t ji_chkptnext;			    /* next checkpoint time */
	time_t ji_sampletim;			    /* last usage sample time, irix only */
	time_t ji_polltime;			    /* last poll from mom superior */
	time_t ji_actalarm;			    /* time of site callout alarm */
	time_t ji_joinalarm;			    /* time of job's sister join job alarm, also, time obit sent, all */
	time_t ji_overlmt_timestamp;		    /*time the job exceeded limit*/
	int ji_jsmpipe;				    /* pipe from child starter process */
	int ji_mjspipe;				    /* pipe to   child starter for ack */
	int ji_jsmpipe2;			    /* pipe for child starter process to send special requests to parent mom */
	int ji_mjspipe2;			    /* pipe for parent mom to ack special request from child starter process */
	int ji_child2parent_job_update_pipe;	    /* read pipe to receive special request from child starter process */
	int ji_parent2child_job_update_pipe;	    /* write pipe for parent mom to send info to child starter process */
	int ji_parent2child_job_update_status_pipe; /* write pipe for parent mom to send job update status to child starter process */
	int ji_parent2child_moms_status_pipe;	    /* write pipe for parent mom to send sister moms status to child starter process */
	int ji_updated;				    /* set to 1 if job's node assignment was updated */
	time_t ji_walltime_stamp;		    /* time stamp for accumulating walltime */
	struct work_task *ji_bg_hook_task;
	struct work_task *ji_report_task;
#ifdef WIN32
	HANDLE		ji_momsubt;	/* process HANDLE to mom subtask */
#else	/* not WIN32 */
	pid_t		ji_momsubt;	/* pid of mom subtask   */
#endif /* WIN32 */
	struct var_table ji_env; /* environment for the job */
	/* ptr to post processing func  */
	void (*ji_mompost)(struct job *, int);
	tm_event_t ji_postevent;	   /* event waiting on mompost */
	int ji_numnodes;		   /* number of nodes (at least 1) */
	int ji_numrescs;		   /* number of entries in ji_resources*/
	int ji_numvnod;			   /* number of virtual nodes */
	int ji_num_assn_vnodes;		   /* number of virtual nodes (full count) */
	tm_event_t ji_obit;		   /* event for end-of-job */
	hnodent *ji_hosts;		   /* ptr to job host management stuff */
	vmpiprocs *ji_vnods;		   /* ptr to job vnode management stuff */
	noderes *ji_resources;		   /* ptr to array of node resources */
	vmpiprocs *ji_assn_vnodes;	   /* ptr to actual assigned vnodes (for hooks) */
	pbs_list_head ji_tasks;		   /* list of task structs */
	pbs_list_head ji_failed_node_list; /* list of mom nodes which fail to join job */
	pbs_list_head ji_node_list;	   /* list of functional mom nodes with vnodes assigned to the job */
	tm_node_id ji_nodekill;		   /* set to nodeid requesting job die */
	int ji_flags;			   /* mom only flags */
	void *ji_setup;			   /* save setup info */

#ifdef WIN32
	HANDLE ji_hJob;				    /* handle for job */
	struct passwd *ji_user;			    /* user info */
#endif						    /* WIN32 */
	int ji_stdout;				    /* socket for stdout */
	int ji_stderr;				    /* socket for stderr */
	int ji_ports[2];			    /* ports for stdout/err */
	enum bg_hook_request ji_hook_running_bg_on; /* set when hook starts in the background*/
	int		ji_msconnected; /* 0 - not connected, 1 - connected */
	pbs_list_head	ji_multinodejobs;	/* links to recovered multinode jobs */
#else						    /* END Mom ONLY -  start Server ONLY */
	struct batch_request *ji_pmt_preq; /* outstanding preempt job request for deleting jobs */
	int ji_discarding;		   /* discarding job */
	struct batch_request *ji_prunreq;  /* outstanding runjob request */
	pbs_list_head ji_svrtask;	   /* links to svr work_task list */
	struct pbs_queue *ji_qhdr;	   /* current queue header */
	struct resc_resv *ji_myResv;	   /* !=0 job belongs to a reservation, see also, attribute JOB_ATR_myResv */

	int ji_lastdest;	     /* last destin tried by route */
	int ji_retryok;		     /* ok to retry, some reject was temp */
	int ji_terminated;	     /* job terminated by deljob batch req */
	int ji_deletehistory;	     /* job history should not be saved */
	pbs_list_head ji_rejectdest; /* list of rejected destinations */
	struct job *ji_parentaj;     /* subjob: parent Array Job */
	ajinfo_t *ji_ajinfo;         /* ArrayJob: information about subjobs and its state counts */
	struct jbdscrd *ji_discard;  /* see discard_job() */
	int ji_jdcd_waiting;	     /* set if waiting on a mom for a response to discard job request */
	char *ji_acctrec;	     /* holder for accounting info */
	char *ji_clterrmsg;	     /* error message to return to client */

	/*
	 * This variable is used to temporarily hold the script for a new job
	 * in memory instead of immediately saving it to the database in the
	 * req_jobscript function. The script is eventually saved into the
	 * database along with saving the job structure as part of req_commit
	 * under one single transaction. After this the memory is freed.
	 */
	char *ji_script;

	/*
	 * This flag is to indicate if queued entity limit attribute usage
	 * is decremented when the job is run
	 */
	int ji_etlimit_decr_queued;

	struct preempt_ordering *preempt_order;
	int preempt_order_index;
	struct work_task *ji_prov_startjob_task;

#endif /* END SERVER ONLY */

	/*
	 * fixed size internal data - maintained via "quick save"
	 * some of the items are copies of attributes, if so this
	 * internal version takes precendent
	 *
	 * This area CANNOT contain any pointers!
	 */
#ifndef PBS_MOM
	char qs_hash[DIGEST_LENGTH];
#endif
	struct jobfix {
		int ji_jsversion;   /* job structure version - JSVERSION */
		int ji_svrflags;    /* server flags */
		time_t ji_stime;    /* time job started execution */
		time_t ji_endtime;   /* time job has ended execution */
		char ji_jobid[PBS_MAXSVRJOBID + 1];   /* job identifier */
		char ji_fileprefix[PBS_JOBBASE + 1];  /* no longer used */
		char ji_queue[PBS_MAXQUEUENAME + 1];  /* name of current queue */
		char ji_destin[PBS_MAXROUTEDEST + 1]; /* dest from qmove/route, MomS for execution */

		int ji_un_type;				 /* type of ji_un union */
		union {					 /* depends on type of queue currently in */
			struct {			 /* if in execution queue .. */
				pbs_net_t ji_momaddr;	 /* host addr of Server */
				unsigned int ji_momport; /* port # */
				int ji_exitstat;	 /* job exit status from MOM */
			} ji_exect;
			struct {
				time_t ji_quetime;  /* time entered queue */
				time_t ji_rteretry; /* route retry time */
			} ji_routet;
			struct {
				int ji_fromsock;	  /* socket job coming over */
				pbs_net_t ji_fromaddr;	  /* host job coming from   */
				unsigned int ji_scriptsz; /* script size */
			} ji_newt;
			struct {
				pbs_net_t ji_svraddr; /* host addr of Server */
				int ji_exitstat;      /* job exit status from MOM */
				uid_t ji_exuid;	      /* execution uid */
				gid_t ji_exgid;	      /* execution gid */
			} ji_momt;
		} ji_un;
	} ji_qs;
	/*
	 * Extended job save area
	 *
	 * This area CANNOT contain any pointers!
	 */
	union jobextend {
		char fill[256]; /* fill to keep same size */
		struct {
			char ji_jid[8];	 /* extended job save data for ALPS */
			int ji_credtype; /* credential type */
#ifdef PBS_MOM
			tm_host_id ji_nodeidx; /* my node id */
			tm_task_id ji_taskidx; /* generate task id's for job */
			int ji_stdout;
			int ji_stderr;
#if MOM_ALPS
			long ji_reservation;
			/* ALPS reservation identifier */
			unsigned long long ji_pagg;
			/* ALPS process aggregate ID */
#endif /* MOM_ALPS */
#endif /* PBS_MOM */
		} ji_ext;
	} ji_extended;

	/*
	 * The following array holds the decode	 format of the attributes.
	 * Its presence is for rapid acces to the attributes.
	 */

	attribute ji_wattr[JOB_ATR_LAST]; /* decoded attributes  */

	short newobj; /* newly created job? */
};

typedef struct job job;


#ifdef	PBS_MOM
/*
 **	Tasks are sessions belonging to a job, running on one of the
 **	nodes assigned to the job.
 */
typedef struct	pbs_task {
	job		*ti_job;	/* pointer to owning job */
	unsigned long	ti_cput;	/* track cput by task */
	pbs_list_link	ti_jobtask;	/* links to tasks for this job */
	int		*ti_tmfd;	/* DIS file descriptors to tasks */
	int		ti_tmnum;	/* next avail entry in ti_tmfd */
	int		ti_tmmax;	/* size of ti_tmfd */
	int		ti_protover;	/* protocol version number */
	int		ti_flags;	/* task internal flags */

#ifdef WIN32
	HANDLE		ti_hProc;	/* keep proc handle */
#endif

	tm_event_t	ti_register;	/* event if task registers */
	pbs_list_head	ti_obits;	/* list of obit events */
	pbs_list_head	ti_info;	/* list of named info */
	struct taskfix {
		char    	ti_parentjobid[PBS_MAXSVRJOBID+1];
		tm_node_id	ti_parentnode;	/* parent vnode */
		tm_node_id	ti_myvnode;	/* my vnode */
		tm_task_id	ti_parenttask;	/* parent task */
		tm_task_id	ti_task;	/* task's taskid */
		int		ti_status;	/* status of task */
		pid_t		ti_sid;		/* session id */
		int		ti_exitstat;	/* exit status */
		union {
			int	ti_hold[16];	/* reserved space */
		} ti_u;
	} ti_qs;
} pbs_task;

/*
 **	A linked list of eventent structures is maintained for all events
 **	for which we are waiting for another MOM to report back.
 */
typedef struct	eventent {
	int		ee_command;	/* command event is for */
	int		ee_fd;		/* TM stream */
	int		ee_retry;	/* event message retry attempt number */
	tm_event_t	ee_client;	/* client event number */
	tm_event_t	ee_event;	/* MOM event number */
	tm_task_id	ee_taskid;	/* which task id */
	char		**ee_argv;	/* save args for spawn */
	char		**ee_envp;	/* save env for spawn */
	pbs_list_link	ee_next;	/* link to next one */
} eventent;

/*
 **	The information needed for a task manager obit request
 **	is indicated with OBIT_TYPE_TMEVENT.  The information needed
 **	for a batch request is indicated with OBIT_TYPE_BREVENT.
 */
#define	OBIT_TYPE_TMEVENT	0
#define	OBIT_TYPE_BREVENT	1

/*
 **	A task can have events which are triggered when it exits.
 **	These are tracked by obitent structures linked to the task.
 */
typedef struct	obitent {
	int		oe_type;	/* what kind of obit */
	union	oe_u {
		struct	oe_tm {
			int		oe_fd;		/* TM reply fd */
			tm_node_id	oe_node;	/* where does notification go */
			tm_event_t	oe_event;	/* event number */
			tm_task_id	oe_taskid;	/* which task id */
		} oe_tm;
		struct batch_request	*oe_preq;
	} oe_u;
	pbs_list_link	oe_next;	/* link to next one */
} obitent;

/*
 **	A task can have a list of named infomation which it makes
 **	available to other tasks in the job.
 */
typedef struct	infoent {
	char		*ie_name;	/* published name */
	void		*ie_info;	/* the glop */
	size_t		ie_len;		/* how much glop */
	pbs_list_link	ie_next;	/* link to next one */
} infoent;

#define	TI_FLAGS_INIT		1	/* task has called tm_init */
#define	TI_FLAGS_CHKPT		2	/* task has checkpointed */
#define	TI_FLAGS_ORPHAN		4	/* MOM not parent of task */
#define	TI_FLAGS_SAVECKP	8	/* save value of CHKPT flag during checkpoint op */

#define TI_STATE_EMBRYO		0
#define	TI_STATE_RUNNING	1
#define TI_STATE_EXITED		2	/* ti_exitstat valid */
#define TI_STATE_DEAD		3

/*
 **      Here is the set of commands for InterMOM (IM) requests.
 */
#define IM_ALL_OKAY		0
#define IM_JOIN_JOB		1
#define IM_KILL_JOB		2
#define IM_SPAWN_TASK		3
#define IM_GET_TASKS		4
#define IM_SIGNAL_TASK		5
#define IM_OBIT_TASK		6
#define IM_POLL_JOB		7
#define IM_GET_INFO		8
#define IM_GET_RESC		9
#define IM_ABORT_JOB		10
#define IM_GET_TID		11	/* no longer used */
#define IM_SUSPEND		12
#define IM_RESUME		13
#define IM_CHECKPOINT		14
#define IM_CHECKPOINT_ABORT	15
#define IM_RESTART		16
#define IM_DELETE_JOB		17
#define IM_REQUEUE		18
#define IM_DELETE_JOB_REPLY	19
#define IM_SETUP_JOB		20
#define IM_DELETE_JOB2		21	/* sent by sister mom to delete job early */
#define IM_SEND_RESC		22
#define IM_UPDATE_JOB		23
#define IM_EXEC_PROLOGUE	24
#define IM_CRED 		25
#define IM_PMIX			26
#define IM_RECONNECT_TO_MS			27
#define IM_JOIN_RECOV_JOB		28

#define IM_ERROR		99
#define IM_ERROR2		100

eventent	*
event_alloc	(job		*pjob,
	int		command,
	int		fd,
	hnodent		*pnode,
	tm_event_t	event,
	tm_task_id	taskid);

pbs_task	*momtask_create	(job		*pjob);

pbs_task	*
task_find	(job		*pjob,
	tm_task_id	taskid);

#endif	/* MOM */

/*
 * server flags (in ji_svrflags)
 */
#define JOB_SVFLG_HERE     0x01	/* SERVER: job created here */
/* MOM: set for Mother Superior */
#define JOB_SVFLG_HASWAIT  0x02 /* job has timed task entry for wait time */
#define JOB_SVFLG_HASRUN   0x04	/* job has been run before (being rerun) */
#define JOB_SVFLG_HOTSTART 0x08	/* job was running, if hot init, restart */
#define JOB_SVFLG_CHKPT	   0x10 /* job has checkpoint file for restart */
#define JOB_SVFLG_SCRIPT   0x20	/* job has a Script file */
#define JOB_SVFLG_OVERLMT1 0x40 /* job over limit first time, MOM only */
#define JOB_SVFLG_OVERLMT2 0x80 /* job over limit second time, MOM only */
#define JOB_SVFLG_ChkptMig 0x100 /* job has migratable checkpoint */
#define JOB_SVFLG_Suspend  0x200 /* job suspended (signal suspend) */
#define JOB_SVFLG_StagedIn 0x400 /* job has files that have been staged in */
#define JOB_SVFLG_HASHOLD  0x800 /* job has a hold request sent to MoM */
#define JOB_SVFLG_HasNodes 0x1000 /* job has nodes allocated to it */
#define JOB_SVFLG_RescAssn 0x2000 /* job resources accumulated in server/que */
#define JOB_SVFLG_SPSwitch 0x2000 /* SP switch loaded for job, SP MOM only */
#define JOB_SVFLG_Actsuspd 0x4000 /* job suspend because workstation active */
#define JOB_SVFLG_cpuperc  0x8000 /* cpupercent violation logged, MOM only */
#define JOB_SVFLG_ArrayJob 0x10000 /* Job is an Array Job */
#define JOB_SVFLG_SubJob   0x20000 /* Job is a subjob of an Array */
#define JOB_SVFLG_StgoFal  0x40000 /* Stageout failed, del jobdir, MOM only */
#define JOB_SVFLG_TERMJOB  0x80000 /* terminate in progress by TERM, MOM only */
/* 0x100000 is UNUSED, previously called JOB_SVFLG_StgoDel for stageout succ */
/* If you intend to use it, make sure jobs to be recovered do not have
 * 0x100000 bit set. Refer SPM229744
 */
#define JOB_SVFLG_AdmSuspd 0x200000 /* Job is suspended for maintenance */
#define JOB_SVFLG_RescUpdt_Rqd 0x400000 /* Broadcast of rsc usage is required */
#define JOB_SVFLG_AlienJob 0x800000 /* job is owned by another server in a msvr setup */

#define MAIL_NONE  (int)'n'
#define MAIL_ABORT (int)'a'
#define MAIL_BEGIN (int)'b'
#define MAIL_END   (int)'e'
#define MAIL_OTHER (int)'o'
#define MAIL_STAGEIN (int)'s'
#define MAIL_CONFIRM (int)'c'	/*scheduler requested reservation be confirmed*/
#define MAIL_SUBJOB (int)'j'
#define MAIL_NORMAL 0
#define MAIL_FORCE  1

#define JOB_FILE_COPY      ".JC"	/* tmp copy while updating */
#define JOB_FILE_SUFFIX    ".JB"	/* job control file */
#define JOB_CRED_SUFFIX    ".CR"	/* job credential file */
#define JOB_EXPORT_SUFFIX  ".XP"	/* job export security context */
#define JOB_SCRIPT_SUFFIX  ".SC"	/* job script file  */
#define JOB_STDOUT_SUFFIX  ".OU"	/* job standard out */
#define JOB_STDERR_SUFFIX  ".ER"	/* job standard error */
#define JOB_CKPT_SUFFIX    ".CK"	/* job checkpoint file */
#define JOB_TASKDIR_SUFFIX ".TK"	/* job task directory */
#define JOB_BAD_SUFFIX     ".BD"	/* save bad job file */
#define JOB_DEL_SUFFIX     ".RM"	/* file pending to be removed */

/*
 * Job states are defined by POSIX as:
 */
#define JOB_STATE_TRANSIT	0
#define JOB_STATE_QUEUED	1
#define JOB_STATE_HELD		2
#define JOB_STATE_WAITING	3
#define JOB_STATE_RUNNING	4
#define JOB_STATE_EXITING	5
#define JOB_STATE_EXPIRED	6
#define JOB_STATE_BEGUN		7
/* Job states defined for history jobs and OGF-BES model */
#define JOB_STATE_MOVED		8
#define JOB_STATE_FINISHED	9

#define JOB_STATE_LTR_UNKNOWN '0'
#define JOB_STATE_LTR_BEGUN 'B'
#define JOB_STATE_LTR_EXITING 'E'
#define JOB_STATE_LTR_FINISHED 'F'
#define JOB_STATE_LTR_HELD 'H'
#define JOB_STATE_LTR_MOVED 'M'
#define JOB_STATE_LTR_QUEUED 'Q'
#define JOB_STATE_LTR_RUNNING 'R'
#define JOB_STATE_LTR_SUSPENDED 'S'
#define JOB_STATE_LTR_TRANSIT 'T'
#define JOB_STATE_LTR_USUSPENDED 'U'
#define JOB_STATE_LTR_WAITING 'W'
#define JOB_STATE_LTR_EXPIRED 'X'

/*
 * job sub-states are defined by PBS (more detailed) as:
 */
#define JOB_SUBSTATE_UNKNOWN	-1
#define JOB_SUBSTATE_TRANSIN 	00	/* Transit in, wait for commit, commit not yet called */
#define JOB_SUBSTATE_TRANSICM	01	/* Transit in, job is being commited */
#define JOB_SUBSTATE_TRNOUT	02	/* transiting job outbound */
#define JOB_SUBSTATE_TRNOUTCM	03	/* transiting outbound, rdy to commit */

#define JOB_SUBSTATE_QUEUED	10	/* job queued and ready for selection */
#define JOB_SUBSTATE_PRESTAGEIN	11	/* job queued, has files to stage in */
#define JOB_SUBSTATE_SYNCRES	13	/* job waiting on sync start ready */
#define JOB_SUBSTATE_STAGEIN	14	/* job staging in files then wait */
#define JOB_SUBSTATE_STAGEGO	15	/* job staging in files and then run */
#define JOB_SUBSTATE_STAGECMP	16	/* job stage in complete */

#define JOB_SUBSTATE_HELD	20	/* job held - user or operator */
#define JOB_SUBSTATE_SYNCHOLD	21	/* job held - waiting on sync regist */
#define JOB_SUBSTATE_DEPNHOLD	22	/* job held - waiting on dependency */

#define JOB_SUBSTATE_WAITING	30	/* job waiting on execution time */
#define JOB_SUBSTATE_STAGEFAIL	37	/* job held - file stage in failed */

#define JOB_SUBSTATE_PRERUN	41	/* job set to MOM to run */
#define JOB_SUBSTATE_RUNNING	42	/* job running */
#define JOB_SUBSTATE_SUSPEND	43	/* job suspended by client */
#define JOB_SUBSTATE_SCHSUSP	45	/* job supsended by scheduler */

#define JOB_SUBSTATE_EXITING	50	/* Start of job exiting processing */
#define JOB_SUBSTATE_STAGEOUT	51	/* job staging out (other) files   */
#define JOB_SUBSTATE_STAGEDEL	52	/* job deleting staged out files  */
#define JOB_SUBSTATE_EXITED	53	/* job exit processing completed   */
#define JOB_SUBSTATE_ABORT	54	/* job is being aborted by server  */
#define JOB_SUBSTATE_KILLSIS	56	/* (MOM) job kill IM to sisters    */
#define JOB_SUBSTATE_RUNEPILOG	57	/* (MOM) job epilogue running      */
#define JOB_SUBSTATE_OBIT	58	/* (MOM) job obit notice sent	   */
#define JOB_SUBSTATE_TERM	59	/* Job is in site termination stage */
#define JOB_SUBSTATE_DELJOB    153	/* (MOM) Job del_job_wait to sisters  */

#define JOB_SUBSTATE_RERUN	60	/* job is rerun, recover output stage */
#define JOB_SUBSTATE_RERUN1	61	/* job is rerun, stageout phase */
#define JOB_SUBSTATE_RERUN2	62	/* job is rerun, delete files stage */
#define JOB_SUBSTATE_RERUN3	63	/* job is rerun, mom delete job */
#define JOB_SUBSTATE_EXPIRED	69	/* subjob (of an array) is gone */

#define JOB_SUBSTATE_BEGUN	70	/* Array job has begun */
#define JOB_SUBSTATE_PROVISION	71	/* job is waiting for provisioning tocomplete */
#define JOB_SUBSTATE_WAITING_JOIN_JOB 72   /* job waiting on IM_JOIN_JOB completion */

/*
 * Job sub-states defined in PBS to support history jobs and OGF-BES model:
 */
#define JOB_SUBSTATE_TERMINATED	91
#define JOB_SUBSTATE_FINISHED	92
#define JOB_SUBSTATE_FAILED	93
#define JOB_SUBSTATE_MOVED	94


/* decriminator for ji_un union type */

#define JOB_UNION_TYPE_NEW   0
#define JOB_UNION_TYPE_EXEC  1
#define JOB_UNION_TYPE_ROUTE 2
#define JOB_UNION_TYPE_MOM   3

/* job hold (internal) types */

#define HOLD_n 0
#define HOLD_u 1
#define HOLD_o 2
#define HOLD_s 4
#define HOLD_bad_password 8

/* Array Job related Defines */

/* See is_job_array() in array_func.c */
#define IS_ARRAY_NO	  0	/* Not an array job nor subjob */
#define IS_ARRAY_ArrayJob 1	/* Is an Array Job    */
#define IS_ARRAY_Single	  2	/* A single Sub Job   */
#define IS_ARRAY_Range	  3	/* A range of Subjobs */
#define PBS_FILE_ARRAY_INDEX_TAG "^array_index^"

/* Special Job Exit Values,  Set by the job starter (child of MOM)   */
/* see server/req_jobobit() & mom/start_exec.c			     */

#define JOB_EXEC_OK	   0	/* job exec successfull */
#define JOB_EXEC_FAIL1	  -1	/* Job exec failed, before files, no retry */
#define JOB_EXEC_FAIL2	  -2	/* Job exec failed, after files, no retry  */
#define JOB_EXEC_RETRY	  -3	/* Job execution failed, do retry    */
#define JOB_EXEC_INITABT  -4	/* Job aborted on MOM initialization */
#define JOB_EXEC_INITRST  -5	/* Job aborted on MOM init, chkpt, no migrate */
#define JOB_EXEC_INITRMG  -6	/* Job aborted on MOM init, chkpt, ok migrate */
#define JOB_EXEC_BADRESRT -7	/* Job restart failed */
#define JOB_EXEC_FAILUID -10	/* invalid uid/gid for job */
#define JOB_EXEC_RERUN   -11	/* Job rerun */
#define JOB_EXEC_CHKP  	 -12	/* Job was checkpointed and killed */
#define JOB_EXEC_FAIL_PASSWORD -13 /* Job failed due to a bad password */
#define JOB_EXEC_RERUN_SIS_FAIL -14	/* Job rerun */
#define JOB_EXEC_QUERST  -15	/* requeue job for restart from checkpoint */
#define JOB_EXEC_FAILHOOK_RERUN -16	/* job exec failed due to a hook rejection, requeue job for later retry (usually returned by the "early" hooks" */
#define JOB_EXEC_FAILHOOK_DELETE -17	/* job exec failed due to a hook rejection, delete the job at end */
#define JOB_EXEC_HOOK_RERUN    -18 /* a hook requested for job to be requeued */
#define JOB_EXEC_HOOK_DELETE   -19 /* a hook requested for job to be deleted */
#define JOB_EXEC_RERUN_MS_FAIL -20 /* Mother superior connection failed */
#define JOB_EXEC_FAIL_SECURITY -21 /* Security breach in PBS directory */
#define JOB_EXEC_HOOKERROR	-22 /* job exec failed due to
				     * unexpected exception or
				     * hook execution timed out
				     */
#define JOB_EXEC_FAIL_KRB5     -23 /* Error no kerberos credentials supplied */
#define JOB_EXEC_UPDATE_ALPS_RESV_ID 1 /* Update ALPS reservation ID to parent mom as soon
					* as it is available.
					* This is neither a success nor a failure exit code,
					* so we are using a positive value
					*/
#define JOB_EXEC_KILL_NCPUS_BURST -24 /* job exec failed due to exceeding ncpus (burst) */
#define JOB_EXEC_KILL_NCPUS_SUM -25 /* job exec failed due to exceeding ncpus (sum) */
#define JOB_EXEC_KILL_VMEM -26 /* job exec failed due to exceeding vmem */
#define JOB_EXEC_KILL_MEM -27 /* job exec failed due to exceeding mem */
#define JOB_EXEC_KILL_CPUT -28 /* job exec failed due to exceeding cput */
#define JOB_EXEC_KILL_WALLTIME -29 /* job exec failed due to exceeding walltime */
#define JOB_EXEC_JOINJOB -30 /* Job exec failed due to join job error */

/*
 * Fake "random" number added onto the end of the staging
 * and execution directory when sandbox=private
 * used in jobdirname()
 */
#define FAKE_RANDOM	"x8z"

/* The default project assigned to jobs when project attribute is unset */
#define PBS_DEFAULT_PROJECT	"_pbs_project_default"

extern void  add_dest(job *);
extern int   depend_on_que(attribute *, void *, int);
extern int   depend_on_exec(job *);
extern int   depend_runone_remove_dependency(job *);
extern int   depend_runone_hold_all(job *);
extern int   depend_runone_release_all(job *);
extern int   depend_on_term(job *);
extern struct depend *find_depend(int type, attribute *pattr);
extern struct depend_job *find_dependjob(struct depend *pdep, char *name);
extern int send_depend_req(job *pjob, struct depend_job *pparent, int type, int op, int schedhint, void (*postfunc)(struct work_task *));
extern void post_runone(struct work_task *pwt);
extern job  *find_job(char *);
extern char *get_variable(job *, char *);
extern void  check_block(job *, char *);
extern char *lookup_variable(void *, int, char *);
extern void  issue_track(job *);
extern void  issue_delete(job *);
extern int   job_abt(job *, char *);
extern job  *job_alloc(void);
extern void  job_free(job *);
extern int   modify_job_attr(job *, svrattrl *, int, int *);
extern char *prefix_std_file(job *, int);
extern void  cat_default_std(job *, int, char *, char **);
extern int   set_objexid(void *, int, attribute *);
#if 0
extern int   site_check_user_map(job *, char *);
#endif
extern int   site_check_user_map(void *, int, char *);
extern int   site_allow_u(char *user, char *host);
extern void  svr_dequejob(job *);
extern int   svr_enquejob(job *, char *);
extern void  svr_evaljobstate(job *, char *, int *, int);
extern int   svr_setjobstate(job *, char, int);
extern int   state_char2int(char);
extern char	 state_int2char(int);
extern int   uniq_nameANDfile(char*, char*, char*);
extern long  determine_accruetype(job *);
extern int   update_eligible_time(long, job *);

#define	TOLERATE_NODE_FAILURES_ALL	"all"
#define	TOLERATE_NODE_FAILURES_JOB_START	"job_start"
#define	TOLERATE_NODE_FAILURES_NONE	"none"
extern int   do_tolerate_node_failures(job *);
int check_job_state(const job *pjob, char state);
int check_job_substate(const job *pjob, int substate);
char get_job_state(const job *pjob);
int get_job_state_num(const job *pjob);
long get_job_substate(const job *pjob);
char *get_jattr_str(const job *pjob, int attr_idx);
struct array_strings *get_jattr_arst(const job *pjob, int attr_idx);
pbs_list_head get_jattr_list(const job *pjob, int attr_idx);
long get_jattr_long(const job *pjob, int attr_idx);
long long get_jattr_ll(const job *pjob, int attr_idx);
svrattrl *get_jattr_usr_encoded(const job *pjob, int attr_idx);
svrattrl *get_jattr_priv_encoded(const job *pjob, int attr_idx);
void set_job_state(job *pjob, char val);
void set_job_substate(job *pjob, long val);
int set_jattr_str_slim(job *pjob, int attr_idx, char *val, char *rscn);
int set_jattr_l_slim(job *pjob, int attr_idx, long val, enum batch_op op);
int set_jattr_ll_slim(job *pjob, int attr_idx, long long val, enum batch_op op);
int set_jattr_b_slim(job *pjob, int attr_idx, long val, enum batch_op op);
int set_jattr_c_slim(job *pjob, int attr_idx, char val, enum batch_op op);
int set_jattr_generic(job *pjob, int attr_idx, char *val, char *rscn, enum batch_op op);
int is_jattr_set(const job *pjob, int attr_idx);
void free_jattr(job *pjob, int attr_idx);
void mark_jattr_not_set(job *pjob, int attr_idx);
void mark_jattr_set(job *pjob, int attr_idx);
attribute *get_jattr(const job *pjob, int attr_idx);

/*
 *	The filesystem related recovery/save routines are renamed
 *	with the suffix "_fs", and the database versions of them
 *	are suffixed "_db". This distinguishes between the two
 *	version. The "_fs" version will continue to be used by
 *	migration routine "svr_migrate_data" and by "mom". Rest of
 *	the server code will typically use only the "_db" routines.
 *	Since mom uses only the "_fs" versions, define the "_fs"
 *	versions to names with the suffix, so that the mom code
 *	remain unchanges and continues to use the "_fs" versions.
 */
#ifdef PBS_MOM

extern job *job_recov_fs(char *);
extern int job_save_fs(job *);

#define job_save  job_save_fs
#define job_recov job_recov_fs

#else

extern job *job_recov_db(char *, job *pjob);
extern int job_save_db(job *);

#define job_save  job_save_db
#define job_recov job_recov_db

extern char *get_job_credid(char *);
#endif

#ifdef	_BATCH_REQUEST_H
extern job  *chk_job_request(char *, struct batch_request *, int *, int *);
extern int   net_move(job *, struct batch_request *);
extern int   svr_chk_owner(struct batch_request *, job *);
extern int   svr_movejob(job *, char *, struct batch_request *);
extern struct batch_request *cpy_stage(struct batch_request *, job *, enum job_atr, int);

#ifdef	_RESERVATION_H
extern int   svr_chk_ownerResv(struct batch_request *, resc_resv *);
#endif	/* _RESERVATION_H */
#endif	/* _BATCH_REQUEST_H */

#ifdef	_QUEUE_H
extern int   svr_chkque(job *, pbs_queue *, char *, int mtype);
extern int   default_router(job *, pbs_queue *, long);
extern int   site_alt_router(job *, pbs_queue *, long);
extern int   site_acl_check(job *, pbs_queue *);
#endif	/* _QUEUE_H */

#ifdef	_WORK_TASK_H
extern int   issue_signal(job *, char *, void(*)(struct work_task *), void *);
extern void   on_job_exit(struct work_task *);
#endif	/* _WORK_TASK_H */

#ifdef _PBS_IFL_H
extern int   update_resources_list(job *, char *, int, char *, enum batch_op op, int, int);
#endif

extern int   Mystart_end_dur_wall(void*, int);
extern int   get_wall(job*);
extern int   get_softwall(job*);
extern int   get_used_wall(job*);
extern int   get_used_cput(job*);
extern int   get_cput(job*);
extern void  remove_deleted_resvs(void);
extern int   pbsd_init_job(job *pjob, int type);

extern void del_job_related_file(job *pjob, char *fsuffix);
#ifdef PBS_MOM
extern void del_job_dirs(job *pjob, char *taskdir);
extern void del_chkpt_files(job *pjob);
#endif

extern void get_jobowner(char *, char *);
extern struct batch_request *cpy_stage(struct batch_request *, job *, enum job_atr, int);
extern struct batch_request *cpy_stdfile(struct batch_request *, job *, enum job_atr);
extern int has_stage(job *);

#ifdef	__cplusplus
}
#endif
#endif	/* _PBS_JOB_H */
