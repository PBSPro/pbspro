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


#include "data_types.h"

// Copy Constructor
sched_exception::sched_exception(const sched_exception &e)
{
	message = e.get_message();
	error_code = e.get_error_code();
}

// Assignment Operator
sched_exception &sched_exception::operator =(const sched_exception &e)
{
	message = e.get_message();
	error_code = e.get_error_code();
	return (*this);
}

// Parametrized Constructor
sched_exception::sched_exception(const std::string &str, const enum sched_error_code err)
{
    message = str;
    error_code = err;
}

// Getter function for error_code
enum sched_error_code sched_exception::get_error_code() const
{
    return error_code;
}

// Getter function for message
const std::string sched_exception::get_message() const
{
    return message;
}

// Destructor
sched_exception::~sched_exception()
{
}

/*
 * @brief Overridden function to return char * of message
 *
 * @return const char *
 */
const char *sched_exception::what()
{
    return message.c_str();
}
