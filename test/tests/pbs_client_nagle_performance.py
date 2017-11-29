# coding: utf-8

# Copyright (C) 1994-2016 Altair Engineering, Inc.
# For more information, contact Altair at www.altair.com.
#
# This file is part of the PBS Professional ("PBS Pro") software.
#
# Open Source License Information:
#
# PBS Pro is free software. You can redistribute it and/or modify it under the
# terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
# details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
# Commercial License Information:
#
# The PBS Pro software is licensed under the terms of the GNU Affero General
# Public License agreement ("AGPL"), except where a separate commercial license
# agreement for PBS Pro version 14 or later has been executed in writing with
# Altair.
#
# Altair’s dual-license business model allows companies, individuals, and
# organizations to create proprietary derivative works of PBS Pro and
# distribute them - whether embedded or bundled with other software - under
# a commercial license agreement.
#
# Use of Altair’s trademarks, including but not limited to "PBS™",
# "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
# trademark licensing policies.

import os

from ptl.utils.pbs_testsuite import *


class TestClientNagles(PBSTestSuite):

    """
    Testing the effect of Nagles algorithm on CLI Performance
    """
    time_command = 'time'

    def setUp(self):
        """
            Base class method overridding
            builds absolute path of commands to execute
        """
        PBSTestSuite.setUp(self)
        self.time_command = self.du.which(exe="time")
        if self.time_command == "time":
            self.skipTest("Time command not found")

    def compute_qdel_time(self):
        """
        Computes qdel time in secs"
        return :
              -1 on qdel fail
        """
        command = self.time_command
        command += " -f \"%e\" "
        command += os.path.join(
            self.server.client_conf['PBS_EXEC'],
            'bin',
            'qdel `')
        command += os.path.join(
            self.server.client_conf['PBS_EXEC'],
            'bin',
            'qselect`')

        # compute elapse time without -E option
        qdel_perf = self.du.run_cmd(self.server.hostname,
                                    command,
                                    as_script=True,
                                    runas=TEST_USER1,
                                    logerr=False)
        if qdel_perf['rc'] != 0:
            return -1

        return float(qdel_perf['err'][0])

    def submit_jobs(self, user, num_jobs):
        """
        Submit specified number of simple jobs
        Arguments :
             user - user under which to submit jobs
             num_jobs - number of jobs to submit
        """
        job = Job(user)
        job.set_sleep_time(1)
        for _ in range(num_jobs):
            self.server.submit(job)

    @timeout(600)
    def test_qdel_nagle_perf(self):
        """
        Submit 500 jobs, measure qdel performace before/after adding managers
        """

        # Adding to managers ensures that packets are larger than 1023 bytes
        # that triggers Nagle's algorithm which slows down the communication.
        # Effect on TCP seems irreversible till server is restarted, so in
        # this test case we restart server so that any effects from earlier
        # test cases/runs do not interfere

        # Baseline qdel performance with scheduling false and managers unset
        # Restart server to ensure no effect from earlier tests/operations
        self.server.manager(MGR_CMD_SET, SERVER, {'scheduling': 'False'})
        self.server.manager(MGR_CMD_UNSET, SERVER, 'managers')
        self.server.restart()

        self.submit_jobs(TEST_USER1, 500)
        qdel_perf = self.compute_qdel_time()
        self.assertTrue((qdel_perf != -1), "qdel command failed")

        # Add to the managers list so that TCP packets are now larger and
        # triggers Nagle's
        manager = TEST_USER1.name + '@' + self.server.hostname
        self.server.manager(MGR_CMD_SET, SERVER, {
                            'managers': (INCR, manager)}, sudo=True)

        # Remeasure the qdel performance
        self.submit_jobs(TEST_USER1, 500)
        qdel_perf2 = self.compute_qdel_time()
        self.assertTrue((qdel_perf2 != -1), "qdel command failed")

        self.logger.info("qdel performance: " + str(qdel_perf))
        self.logger.info(
            "qdel performance after setting manager: " + str(qdel_perf2))

        # Check that the two timings are pretty close to each other
        self.assertTrue((qdel_perf2 - qdel_perf) < 5,
                        "qdel performance differs too much!")
