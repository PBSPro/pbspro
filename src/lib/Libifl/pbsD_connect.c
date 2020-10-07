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
 * @file	pbs_connect.c
 * @brief
 *	Open a connection with the pbs server.  At this point several
 *	things are stubbed out, and other things are hard-wired.
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <pwd.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pbs_ifl.h>
#include "libpbs.h"
#include "net_connect.h"
#include "dis.h"
#include "libsec.h"
#include "pbs_ecl.h"
#include "pbs_internal.h"
#include "log.h"
#include "auth.h"
#include "ifl_internal.h"

/**
 * @brief
 *	-returns the default server name.
 *
 * @return	string
 * @retval	dflt srvr name	success
 * @retval	NULL		error
 *
 */
char *
__pbs_default()
{
	char dflt_server[PBS_MAXSERVERNAME+1];
	struct pbs_client_thread_context *p;

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return NULL;

	p =  pbs_client_thread_get_context_data();

	if (pbs_loadconf(0) == 0)
		return NULL;

	if (p->th_pbs_defserver[0] == '\0') {
		/* The check for PBS_DEFAULT is done in pbs_loadconf() */
		if (pbs_conf.pbs_primary && pbs_conf.pbs_secondary) {
			strncpy(dflt_server, pbs_conf.pbs_primary, PBS_MAXSERVERNAME);
		} else if (pbs_conf.pbs_server_host_name) {
			strncpy(dflt_server, pbs_conf.pbs_server_host_name, PBS_MAXSERVERNAME);
		} else if (pbs_conf.pbs_server_name) {
			strncpy(dflt_server, pbs_conf.pbs_server_name, PBS_MAXSERVERNAME);
		} else {
			dflt_server[0] = '\0';
		}
		strcpy(p->th_pbs_defserver, dflt_server);
	}
	return (p->th_pbs_defserver);
}

/**
 * @brief
 *	-returns the server name.
 *
 * @param[in] server - server name
 * @param[out] server_name - server name
 * @param[in] port - port number
 *
 * @return	string
 * @retval	servr name	success
 *
 */
static char *
PBS_get_server(char *server, char *server_name, unsigned int *port)
{
	int   i;
	char *pc;
	unsigned int dflt_port = 0;
	char *p;

	for (i=0;i<PBS_MAXSERVERNAME+1;i++)
		server_name[i] = '\0';

	if (dflt_port == 0)
		dflt_port = pbs_conf.batch_service_port;

	/* first, get the "net.address[:port]" into 'server_name' */

	if ((server == NULL) || (*server == '\0')) {
		if ((p=pbs_default()) == NULL)
			return NULL;
		strcpy(server_name, p);
	} else {
		strncpy(server_name, server, PBS_MAXSERVERNAME);
	}

	/* now parse out the parts from 'server_name' */

	if ((pc = strchr(server_name, (int)':')) != NULL) {
		/* got a port number */
		*pc++ = '\0';
		*port = atoi(pc);
	} else {
		*port = dflt_port;
	}

	return server_name;
}

/**
 * @brief
 *	Return the IP address used in binding a socket to a host
 *	Attempts to find IPv4 address for the named host,  first address found
 *	is returned.
 *
 * @param[in]	host - The name of the host to whose address is needed
 * @param[out]	sap  - pointer to the sockaddr_in structure into which
 *						the address will be returned.
 *
 * @return	int
 * @retval  0	- success, address set in *sap
 * @retval -1	- error, *sap is left zero-ed
 */
static int
get_hostsockaddr(char *host, struct sockaddr_in *sap)
{
	struct addrinfo hints;
	struct addrinfo *aip, *pai;

	memset(sap, 0, sizeof(struct sockaddr));
	memset(&hints, 0, sizeof(struct addrinfo));
	/*
	 *	Why do we use AF_UNSPEC rather than AF_INET?  Some
	 *	implementations of getaddrinfo() will take an IPv6
	 *	address and map it to an IPv4 one if we ask for AF_INET
	 *	only.  We don't want that - we want only the addresses
	 *	that are genuinely, natively, IPv4 so we start with
	 *	AF_UNSPEC and filter ai_family below.
	 */
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if (getaddrinfo(host, NULL, &hints, &pai) != 0) {
		pbs_errno = PBSE_BADHOST;
		return -1;
	}
	for (aip = pai; aip != NULL; aip = aip->ai_next) {
		/* skip non-IPv4 addresses */
		if (aip->ai_family == AF_INET) {
			*sap = *((struct sockaddr_in *) aip->ai_addr);
			freeaddrinfo(pai);
			return 0;
		}
	}
	/* treat no IPv4 addresses as getaddrinfo() failure */
	pbs_errno = PBSE_BADHOST;
	freeaddrinfo(pai);
	return -1;
}

/**
 * @brief	This function establishes a network connection to the given server.
 *
 * @param[in]   server - The hostname of the pbs server to connect to.
 * @param[in]   port - Port number of the pbs server to connect to.
 * @param[in]   extend_data - a string to send as "extend" data
 *
 *
 * @return int
 * @retval >= 0	The physical server socket.
 * @retval -1	error encountered setting up the connection.
 */

static int
tcp_connect(char *hostname, int server_port, char *extend_data)
{
	int i;
	int sd;
	struct sockaddr_in server_addr;
	struct batch_reply	*reply;
	char errbuf[LOG_BUF_SIZE] = {'\0'};

		/* get socket	*/
#ifdef WIN32
		/* the following lousy hack is needed since the socket call needs */
		/* SYSTEMROOT env variable properly set! */
		if (getenv("SYSTEMROOT") == NULL) {
			setenv("SYSTEMROOT", "C:\\WINDOWS", 1);
			setenv("SystemRoot", "C:\\WINDOWS", 1);
		}
#endif
	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd == -1) {
		pbs_errno = PBSE_SYSTEM;
		return -1;
	}

	pbs_strncpy(pbs_server, hostname, sizeof(pbs_server)); /* set for error messages from commands */
	/* and connect... */

	if (get_hostsockaddr(hostname, &server_addr) != 0)
		return -1;

	server_addr.sin_port = htons(server_port);
	if (connect(sd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) != 0) {
		/* connect attempt failed */
		closesocket(sd);
		pbs_errno = errno;
		return -1;
	}

	/* setup connection level thread context */
	if (pbs_client_thread_init_connect_context(sd) != 0) {
		closesocket(sd);
		pbs_errno = PBSE_SYSTEM;
		return -1;
	}

	/*
	 * No need for global lock now on, since rest of the code
	 * is only communication on a connection handle.
	 * But we dont need to lock the connection handle, since this
	 * connection handle is not yet been returned to the client
	 */

	if (load_auths(AUTH_CLIENT)) {
		closesocket(sd);
		pbs_errno = PBSE_SYSTEM;
		return -1;
	}

	/* setup DIS support routines for following pbs_* calls */
	DIS_tcp_funcs();

	/* The following code was originally  put in for HPUX systems to deal
	 * with the issue where returning from the connect() call doesn't
	 * mean the connection is complete.  However, this has also been
	 * experienced in some Linux ppc64 systems like js-2. Decision was
	 * made to enable this harmless code for all architectures.
	 * FIX: Need to use the socket to send
	 * a message to complete the process.  For IFF authentication there is
	 * no leading authentication message needing to be sent on the client
	 * socket, so will send a "dummy" message and discard the replyback.
	 */
	if ((i = encode_DIS_ReqHdr(sd, PBS_BATCH_Connect, pbs_current_user)) ||
		(i = encode_DIS_ReqExtend(sd, extend_data))) {
		closesocket(sd);
		pbs_errno = PBSE_SYSTEM;
		return -1;
	}
	if (dis_flush(sd)) {
		closesocket(sd);
		pbs_errno = PBSE_SYSTEM;
		return -1;
	}

	pbs_errno = PBSE_NONE;
	reply = PBSD_rdrpy(sd);
	PBSD_FreeReply(reply);
	if (pbs_errno != PBSE_NONE) {
		closesocket(sd);
		return -1;
	}

	if (engage_client_auth(sd, hostname, server_port, errbuf, sizeof(errbuf)) != 0) {
		if (pbs_errno == PBSE_NONE)
			pbs_errno = PBSE_PERM;
		fprintf(stderr, "auth: error returned: %d\n", pbs_errno);
		if (errbuf[0] != '\0')
			fprintf(stderr, "auth: %s\n", errbuf);
		closesocket(sd);
		return -1;
	}

	pbs_tcp_timeout = PBS_DIS_TCP_TIMEOUT_VLONG;	/* set for 3 hours */

	/*
	 * Disable Nagle's algorithm on the TCP connection to server.
	 * Nagle's algorithm is hurting cmd-server communication.
	 */
	if (pbs_connection_set_nodelay(sd) == -1) {
		closesocket(sd);
		pbs_errno = PBSE_SYSTEM;
		return -1;
	}

	return sd;
}

/**
 * @brief	Get the array of connections to all servers
 *
 * @param[in]	parentfd - fd that identifies a particular set of server connections
 * 						(pass -1 if connections not established yet)
 *
 * @return	void
 * @retval	!NULL - success
 * @retval	NULL - error
 *
 * @par Side Effects:
 *	None
 *
 * @par MT-safe: Yes
 */
void *
get_conn_servers(int parentfd)
{
	svr_conns_list_t *conn_list = NULL;
	svr_conn_t *msvr_conns = NULL;

	conn_list = pthread_getspecific(psi_key);
	if (conn_list == NULL || parentfd == -1) {	/* Create a new connection set */
		int num_svrs;
		int i;
		svr_conns_list_t *new_conns = NULL;

		new_conns = malloc(sizeof(svr_conns_list_t));
		if (new_conns == NULL) {
			pbs_errno = PBSE_SYSTEM;
			return NULL;
		}
		new_conns->next = NULL;

		num_svrs = get_num_servers();
		msvr_conns = calloc(num_svrs, sizeof(svr_conn_t));
		if (msvr_conns == NULL) {
			free(new_conns);
			pbs_errno = PBSE_SYSTEM;
			return NULL;
		}

		for (i = 0; i < num_svrs; i++) {
			strcpy(msvr_conns[i].name, pbs_conf.psi[i].name);
			msvr_conns[i].port = pbs_conf.psi[i].port;
			msvr_conns[i].sd = -1;
			msvr_conns[i].state = SVR_CONN_STATE_DOWN;
		}
		new_conns->conn_arr = msvr_conns;
		new_conns->next = conn_list;
		conn_list = new_conns;

		pthread_setspecific(psi_key, conn_list);
	} else if (parentfd != -1) {
		svr_conns_list_t *iter_conns = NULL;

		/* Find the set of connections associated with the parent fd */
		for (iter_conns = conn_list; iter_conns != NULL; iter_conns = iter_conns->next) {
			if (iter_conns->conn_arr[0].sd == parentfd) {
				msvr_conns = iter_conns->conn_arr;
				break;
			}
		}
	}

	return msvr_conns;
}

/**
 * @brief	Deallocate the connection set associated with the fd given
 *
 * @param[int]	parentfd - parent fd of the connection set
 *
 * @return	void
 */
void
dealloc_conn_list_single(int parentfd)
{
	svr_conns_list_t *iter_conns;
	svr_conns_list_t *conn_list;
	svr_conns_list_t *prev = NULL;

	conn_list = pthread_getspecific(psi_key);
	for (iter_conns = conn_list; iter_conns != NULL; prev = iter_conns, iter_conns = iter_conns->next) {
		if (iter_conns->conn_arr[0].sd == parentfd) {
			if (prev != NULL)
				prev->next = iter_conns->next;
			else
				conn_list = NULL;
			free(iter_conns);
			pthread_setspecific(psi_key, conn_list);
			break;
		}
	}
}


/**
 * @brief	Helper function for connect_to_servers to connect to a particular server
 *
 * @param[in]		idx - array index for the server to connect to
 * @param[in,out]	conn_arr - array of svr_conn_t
 * @param[in]		extend_data - any additional data relevant for connection
 *
 * @return	int
 * @retval	-1 for error
 * @retval	fd of connection
 */
static int
connect_to_server(int idx, svr_conn_t *conn_arr, char *extend_data)
{
	int sd = conn_arr[idx].sd;
	struct sockaddr_in my_sockaddr;

	/* bind to pbs_public_host_name if given  */
	if (pbs_conf.pbs_public_host_name) {
		if (get_hostsockaddr(pbs_conf.pbs_public_host_name, &my_sockaddr) != 0)
			return -1; /* pbs_errno was set */
		/* my address will be in my_sockaddr,  bind the socket to it */
		my_sockaddr.sin_port = 0;
		if (bind(sd, (struct sockaddr *)&my_sockaddr, sizeof(my_sockaddr)) != 0) {
			return -1;
		}
	}

	if (conn_arr[idx].state != SVR_CONN_STATE_UP) {
		if ((sd = tcp_connect(conn_arr[idx].name, conn_arr[idx].port, extend_data)) != -1) {
			conn_arr[idx].state = SVR_CONN_STATE_UP;
			conn_arr[idx].sd = sd;
		} else
			conn_arr[idx].state = SVR_CONN_STATE_DOWN;
	}

	return sd;
}

/**
 * @brief	To connect to all the servers
 *
 * @param[in]	svrhost - valid host name of one of the servers
 * @param[in]	port - port of the server to connect to (considered if server_name is not NULL)
 * @param[in]	extend_data
 *
 * @return int
 * @retval >0 - success
 * @retval -1 - error
 */
static int
connect_to_servers(char *svrhost, uint port, char *extend_data)
{
	int i;
	int fd = -1;
	int ret_fd = -1;
	int num_conf_servers = get_num_servers();
	svr_conn_t *svr_connections = get_conn_servers(-1);

	if (svr_connections == NULL)
		return -1;

	if (svrhost != NULL) {
		struct sockaddr_in tmp_sockaddr;
		char *dflt_server = NULL;

		/* Check that this host is reachable */
		if (get_hostsockaddr(svrhost, &tmp_sockaddr) != 0) {
			pbs_errno = PBSE_BADHOST;
			return -1;
		}

		/* Is this different than our default cluster? */
		dflt_server = pbs_default();
		if (dflt_server != NULL) {
			struct sockaddr_in dflt_sockaddr;

			if (get_hostsockaddr(dflt_server, &dflt_sockaddr) == 0) {
				char ipstr1[INET_ADDRSTRLEN];
				char ipstr2[INET_ADDRSTRLEN];
				void *ipaddr1;
				void *ipaddr2;


				ipaddr1 = &(dflt_sockaddr.sin_addr);
				ipaddr2 = &(tmp_sockaddr.sin_addr);
				inet_ntop(AF_INET, ipaddr1, ipstr1, sizeof(ipstr1));
				inet_ntop(AF_INET, ipaddr2, ipstr2, sizeof(ipstr2));
				if (strcmp(ipstr1, ipstr2) != 0) {
					/* The client is trying to reach a different cluster than what's known
					 * So, just reach out to the one host provided and reply back instead
					 * of connecting to the PBS_SERVER_INSTANCES of the default cluster
					 */
					pbs_strncpy(svr_connections[0].name, svrhost, sizeof(svr_connections[0].name));
					svr_connections[0].port = port;
					return connect_to_server(0, svr_connections, extend_data);
				}
			}
		}
	}

	/* Try to connect to all servers in the cluster */
	for (i = 0; i < num_conf_servers; i++) {
		fd = connect_to_server(i, svr_connections, extend_data);

		if (ret_fd == -1 && fd != -1)
			ret_fd = fd;
	}

	return ret_fd;
}

/**
 * @brief	Makes a PBS_BATCH_Connect request to 'server'.
 *
 * @param[in]   server - the hostname of the pbs server to connect to.
 * @param[in]   extend_data - a string to send as "extend" data.
 *
 * @return int
 * @retval >= 0	index to the internal connection table representing the
 *		connection made.
 * @retval -1	error encountered setting up the connection.
 */
int
__pbs_connect_extend(char *server, char *extend_data)
{
	char server_name[PBS_MAXSERVERNAME + 1];
	unsigned int server_port;

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return -1;

	if (pbs_loadconf(0) == 0)
		return -1;

	server = PBS_get_server(server, server_name, &server_port);
	if (server == NULL) {
		pbs_errno = PBSE_NOSERVER;
		return -1;
	}

	return connect_to_servers(server_name, server_port, extend_data);
}

/**
 * @brief
 *	Set no-delay option (disable nagles algoritm) on connection
 *
 * @param[in]   connect - connection index
 *
 * @return int
 * @retval  0	Succcess
 * @retval -1	Failure (bad index, or failed to set)
 *
 */
int
pbs_connection_set_nodelay(int connect)
{
	int opt;
	pbs_socklen_t optlen;

	if (connect < 0)
		return -1;
	optlen = sizeof(opt);
	if (getsockopt(connect, IPPROTO_TCP, TCP_NODELAY, &opt, &optlen) == -1)
		return -1;

	if (opt == 1)
		return 0;

	opt = 1;
	return setsockopt(connect, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

/**
 * @brief	A wrapper progarm to pbs_connect_extend() but this one not
 *			passing any 'extend' data to the connection.
 *
 * @param[in] server - server - the hostname of the pbs server to connect to.
 *
 * @retval int	- return value of pbs_connect_extend().
 */
int
__pbs_connect(char *server)
{
	return (pbs_connect_extend(server, NULL));
}

/**
 * @brief	Helper function for __pbs_disconnect
 *
 * @param[in]	connect - connection to disconnect
 *
 * @return	int
 * @retval	0	success
 * @retval	-1	error
 */
static int
disconnect_from_server(int connect)
{
	char x;

	if (connect < 0)
		return 0;

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return -1;

	/*
	 * Use only connection handle level lock since this is
	 * just communication with server
	 */
	if (pbs_client_thread_lock_connection(connect) != 0)
		return -1;

	/*
	 * check again to ensure that another racing thread
	 * had not already closed the connection
	 */
	if (get_conn_chan(connect) == NULL)
		return 0;

	/* send close-connection message */

	DIS_tcp_funcs();
	if ((encode_DIS_ReqHdr(connect, PBS_BATCH_Disconnect, pbs_current_user) == 0) &&
		(dis_flush(connect) == 0)) {
		for (;;) {	/* wait for server to close connection */
#ifdef WIN32
			if (recv(connect, &x, 1, 0) < 1)
#else
			if (read(connect, &x, 1) < 1)
#endif
				break;
		}
	}

	CS_close_socket(connect);
	closesocket(connect);
	dis_destroy_chan(connect);

	/* unlock the connection level lock */
	if (pbs_client_thread_unlock_connection(connect) != 0)
		return -1;

	/*
	 * this is only a per thread work, so outside lock and unlock
	 * connection needs the thread level connect context so this should be
	 * called after unlocking
	 */
	if (pbs_client_thread_destroy_connect_context(connect) != 0)
		return -1;

	destroy_connection(connect);

	return 0;
}

/**
 * @brief
 *	-send close connection batch request
 *
 * @param[in] connect - socket descriptor
 *
 * @return	int
 * @retval	0	success
 * @retval	-1	error
 *
 */
int
__pbs_disconnect(int connect)
{
	svr_conn_t *svr_conns = NULL;

	if (connect <= 0)
		return -1;

	/* See if we should disconnect from all servers */
	svr_conns = get_conn_servers(connect);
	if (svr_conns != NULL && connect == svr_conns[0].sd) {
		int i;

		for (i = 0; i < get_num_servers(); i++) {
			if (disconnect_from_server(svr_conns[i].sd) != 0)
				return -1;

			svr_conns[i].sd = -1;
			svr_conns[i].state = SVR_CONN_STATE_DOWN;
		}
	} else {
		/* fd doesn't belong to a multi-server setup, just disconnect and exit */
		disconnect_from_server(connect);
		return 0;
	}

	/* Destroy the connection cache associated with this set of connections */
	dealloc_conn_list_single(connect);

	return 0;
}

/**
 * @brief
 *	-return the number of max connections.
 *
 * @return	int
 * @retval	0	success
 * @retval	!0	error
 *
 */
int
pbs_query_max_connections()
{
	return (NCONNECTS - 1);
}

/*
 *	pbs_connect_noblk() - Open a connection with a pbs server.
 *		Do not allow TCP to block us if Server host is down
 *
 *	At this point, this does not attempt to find a fail_over Server
 */

/**
 * @brief
 *	Open a connection with a pbs server.
 *	Do not allow TCP to block us if Server host is down
 *	At this point, this does not attempt to find a fail_over Server
 *
 * @param[in]   server - specifies the server to which to connect
 * @param[in]   tout - timeout value for select
 *
 * @return int
 * @retval >= 0	index to the internal connection table representing the
 *		connection made.
 * @retval -1	error encountered in getting index
 */
int
pbs_connect_noblk(char *server, int tout)
{
	int sock;
	int i;
	pbs_socklen_t l;
	int n;
	struct timeval tv;
	fd_set fdset;
	struct batch_reply *reply;
	char server_name[PBS_MAXSERVERNAME+1];
	unsigned int server_port;
	struct addrinfo *aip, *pai;
	struct addrinfo hints;
	struct sockaddr_in *inp;
	short int connect_err = 0;
	char errbuf[LOG_BUF_SIZE] = {'\0'};

#ifdef WIN32
	int     non_block = 1;
#else
	int nflg;
	int oflg;
#endif

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return -1;

	if (pbs_loadconf(0) == 0)
		return -1;

	/* get server host and port	*/

	server = PBS_get_server(server, server_name, &server_port);
	if (server == NULL) {
		pbs_errno = PBSE_NOSERVER;
		return -1;
	}

	/* get socket	*/

#ifdef WIN32
	/* the following lousy hack is needed since the socket call needs */
	/* SYSTEMROOT env variable properly set! */
	if (getenv("SYSTEMROOT") == NULL) {
		setenv("SYSTEMROOT", "C:\\WINDOWS", 1);
		setenv("SystemRoot", "C:\\WINDOWS", 1);
	}
#endif
	sock = socket(AF_INET, SOCK_STREAM, 0);
	/* set socket non-blocking */
#ifdef WIN32
	if (ioctlsocket(sock, FIONBIO, &non_block) == SOCKET_ERROR)
#else
	oflg = fcntl(sock, F_GETFL) & ~O_ACCMODE;
	nflg = oflg | O_NONBLOCK;
	if (fcntl(sock, F_SETFL, nflg) == -1)
#endif
		goto err;

	/* and connect... */

	strcpy(pbs_server, server);    /* set for error messages from commands */
	memset(&hints, 0, sizeof(struct addrinfo));
	/*
	 *      Why do we use AF_UNSPEC rather than AF_INET?  Some
	 *      implementations of getaddrinfo() will take an IPv6
	 *      address and map it to an IPv4 one if we ask for AF_INET
	 *      only.  We don't want that - we want only the addresses
	 *      that are genuinely, natively, IPv4 so we start with
	 *      AF_UNSPEC and filter ai_family below.
	 */
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if (getaddrinfo(server, NULL, &hints, &pai) != 0) {
		closesocket(sock);
		pbs_errno = PBSE_BADHOST;
		return -1;
	}
	for (aip = pai; aip != NULL; aip = aip->ai_next) {
		/* skip non-IPv4 addresses */
		if (aip->ai_family == AF_INET) {
			inp = (struct sockaddr_in *) aip->ai_addr;
			break;
		}
	}
	if (aip == NULL) {
		/* treat no IPv4 addresses as getaddrinfo() failure */
		closesocket(sock);
		pbs_errno = PBSE_BADHOST;
		freeaddrinfo(pai);
		return -1;
	} else
		inp->sin_port = htons(server_port);
	if (connect(sock,
		aip->ai_addr,
		aip->ai_addrlen) < 0) {
		connect_err = 1;
	}
	if (connect_err == 1)
	{
		/* connect attempt failed */
		pbs_errno = SOCK_ERRNO;
		switch (pbs_errno) {
#ifdef WIN32
			case WSAEWOULDBLOCK:
#else
			case EINPROGRESS:
			case EWOULDBLOCK:
#endif
				while (1) {
					FD_ZERO(&fdset);
					FD_SET(sock, &fdset);
					tv.tv_sec = tout;
					tv.tv_usec = 0;
					n = select(sock+1, NULL, &fdset, NULL, &tv);
					if (n > 0) {
						pbs_errno = 0;
						l = sizeof(pbs_errno);
						(void)getsockopt(sock,
							SOL_SOCKET, SO_ERROR,
							&pbs_errno, &l);
						if (pbs_errno == 0)
							break;
						else
							goto err;
					} if ((n < 0) &&
#ifdef WIN32
						(SOCK_ERRNO == WSAEINTR)
#else
						(SOCK_ERRNO == EINTR)
#endif
						) {
						continue;
					} else {
						goto err;
					}
				}
				break;

			default:
err:
				closesocket(sock);
				freeaddrinfo(pai);
				return -1;	/* cannot connect */

		}
	}
	freeaddrinfo(pai);

	/* reset socket blocking */
#ifdef WIN32
	non_block = 0;
	if (ioctlsocket(sock, FIONBIO, &non_block) == SOCKET_ERROR)
#else
	if (fcntl(sock, F_SETFL, oflg) < 0)
#endif
		goto err;

	/*
	 * multiple threads cant get the same connection id above,
	 * so no need to lock this piece of code
	 */
	/* setup connection level thread context */
	if (pbs_client_thread_init_connect_context(sock) != 0) {
		closesocket(sock);
		/* pbs_errno set by the pbs_connect_init_context routine */
		return -1;
	}
	/*
	 * even though the following is communication with server on
	 * a connection handle, it does not need to be lock since
	 * this connection handle has not be returned back yet to the client
	 * so others threads cannot use it
	 */

	if (load_auths(AUTH_CLIENT)) {
		closesocket(sock);
		return -1;
	}

	/* setup DIS support routines for following pbs_* calls */
	DIS_tcp_funcs();

	/* The following code was originally  put in for HPUX systems to deal
	 * with the issue where returning from the connect() call doesn't
	 * mean the connection is complete.  However, this has also been
	 * experienced in some Linux ppc64 systems like js-2. Decision was
	 * made to enable this harmless code for all architectures.
	 * FIX: Need to use the socket to send
	 * a message to complete the process.  For IFF authentication there is
	 * no leading authentication message needing to be sent on the client
	 * socket, so will send a "dummy" message and discard the replyback.
	 */
	if ((i = encode_DIS_ReqHdr(sock, PBS_BATCH_Connect, pbs_current_user)) ||
		(i = encode_DIS_ReqExtend(sock, NULL))) {
		pbs_errno = PBSE_SYSTEM;
		return -1;
	}
	if (dis_flush(sock)) {
		pbs_errno = PBSE_SYSTEM;
		return -1;
	}
	reply = PBSD_rdrpy(sock);
	PBSD_FreeReply(reply);

	if (engage_client_auth(sock, server, server_port, errbuf, sizeof(errbuf)) != 0) {
		if (pbs_errno == 0)
			pbs_errno = PBSE_PERM;
		fprintf(stderr, "auth: error returned: %d\n", pbs_errno);
		if (errbuf[0] != '\0')
			fprintf(stderr, "auth: %s\n", errbuf);
		closesocket(sock);
		pbs_errno = PBSE_PERM;
		return -1;
	}

	/* setup DIS support routines for following pbs_* calls */
	DIS_tcp_funcs();
	pbs_tcp_timeout = PBS_DIS_TCP_TIMEOUT_VLONG;	/* set for 3 hours */

	return sock;
}
