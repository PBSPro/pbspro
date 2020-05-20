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
 * @file	pbs_rdel.c
 * @brief
 *  pbs_rdel - PBS command to delete reservations
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include <pbs_version.h>
#include <stdio.h>
#include <pbs_ifl.h>
#include <cmds.h>

/**
 * @brief
 *	The main function in C - entry point
 *
 * @param[in]  argc - argument count
 * @param[in]  argv - pointer to argument array
 * @param[in]  envp - pointer to environment values
 *
 * @return  int
 * @retval  0 - success
 * @retval  !0 - error
 */
int
main(int argc, char **argv, char **envp)
{
	int c;
	int errflg = 0;
	int any_failed = 0;

	char resv_id[PBS_MAXCLTJOBID];	/* from the command line */

	char resv_id_out[PBS_MAXCLTJOBID];
	char server_out[MAXSERVERNAME];

	/* destqueue=queue + '@' + server + \0 */
	char dest_queue[PBS_MAXQUEUENAME+PBS_MAXSERVERNAME+2+10] = {'\0'};


	/*test for real deal or just version and exit*/

	PRINT_VERSION_AND_EXIT(argc, argv);

#ifdef WIN32
	if (winsock_init()) {
		return 1;
	}
#endif


	while ((c = getopt(argc, argv, "q:")) != EOF)
		switch (c) {
			case 'q':
				if (optarg[0] == '\0') {
					fprintf(stderr, "pbs_rdel: illegal -q value\n");
					errflg++;
					break;
				}
				sprintf(dest_queue, "destqueue=%s", optarg);
				break;
			default :
				errflg++;
		}

	if (errflg || optind >= argc) {
		fprintf(stderr, "usage:\tpbs_rdel [-q dest] resv_identifier...\n");
		fprintf(stderr, "      \tpbs_rdel --version\n");
		exit(2);
	}


	if (CS_client_init() != CS_SUCCESS) {
		fprintf(stderr, "pbs_rdel: unable to initialize security library.\n");
		exit(1);
	}

	for (; optind < argc; optind++) {
		int connect;
		int stat = 0;

		strcpy(resv_id, argv[optind]);
		if (get_server(resv_id, resv_id_out, server_out)) {
			fprintf(stderr, "pbs_rdel: illegally formed reservation identifier: %s\n", resv_id);
			any_failed = 1;
			continue;
		}
		connect = cnt2server(server_out);

		if (connect <= 0) {
			fprintf(stderr, "pbs_rdel: cannot connect to server %s (errno=%d)\n",
				pbs_server, pbs_errno);
			any_failed = pbs_errno;
			continue;
		}

		stat = pbs_delresv(connect, resv_id_out, dest_queue);
		if (stat) {
			prt_job_err("pbs_rdel", connect, resv_id_out);
			any_failed = pbs_errno;
		}
		pbs_disconnect(connect);
	}
	CS_close_app();
	exit(any_failed);
}
