/*
 * Copyright (C) 1994-2019 Altair Engineering, Inc.
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
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */

#include <pbs_config.h>
#include <wchar.h>
#include <Python.h>
#include "pbs_ifl.h"
#include "pbs_internal.h"
#include "log.h"

/**
 * @brief get_py_progname
 * 	Find and return where python binary is located
 *
 * @param[in] dest - buffer to copy python path
 * @param[in] dest_sz - size of dest
 *
 * @return int
 * @retval 0 - Success
 * @retval 1 - Fail
 */
int
get_py_progname(char **dest, int dest_sz)
{
#ifdef PYTHON
	static char python_binpath[MAXPATHLEN + 1] = {'\0'};

	if (python_binpath[0] == '\0') {
#ifndef WIN32
		snprintf(python_binpath, MAXPATHLEN, "%s/python/bin/python3", pbs_conf.pbs_exec_path);
#else
		snprintf(python_binpath, MAXPATHLEN, "%s/python/python.exe", pbs_conf.pbs_exec_path);
		forward2back_slash(python_binpath);
#endif
		if (!file_exists(python_binpath)) {
#ifdef PYTHON_BIN_PATH
			snprintf(python_binpath, MAXPATHLEN, "%s", PYTHON_BIN_PATH);
			if (!file_exists(python_binpath))
#endif
			{
				log_err(-1, __func__, "Python executable not found!");
				return 1;
			}
		}
	}
	strncpy(*dest, python_binpath, dest_sz - 1);
	(*dest)[dest_sz] = '\0';
	return 0;
#else
	return 1;
#endif

}
/**
 * @brief set_py_progname
 * 	Find and tell Python interpreter where python binary is located
 *
 * @return int
 * @retval 0 - Success
 * @retval 1 - Fail
 */
int
set_py_progname(void)
{
#ifdef PYTHON
	char python_binpath[MAXPATHLEN + 1] = {'\0'};
	char *ptr = python_binpath;
	static wchar_t w_python_binpath[MAXPATHLEN + 1] = {'\0'};

	if (w_python_binpath[0] == '\0') {
		if (get_py_progname(&ptr, MAXPATHLEN + 1)) {
			log_err(-1, __func__, "Failed to find python binary path!");
			return 1;
		}
		mbstowcs(w_python_binpath, python_binpath, MAXPATHLEN + 1);
	}
	Py_SetProgramName(w_python_binpath);
	return 0;
#else
	return 0;
#endif
}
