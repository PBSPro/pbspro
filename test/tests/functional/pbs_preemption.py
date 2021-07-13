# coding: utf-8

# Copyright (C) 1994-2021 Altair Engineering, Inc.
# For more information, contact Altair at www.altair.com.
#
# This file is part of both the OpenPBS software ("OpenPBS")
# and the PBS Professional ("PBS Pro") software.
#
# Open Source License Information:
#
# OpenPBS is free software. You can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# OpenPBS is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
# License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Commercial License Information:
#
# PBS Pro is commercially licensed software that shares a common core with
# the OpenPBS software.  For a copy of the commercial license terms and
# conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
# Altair Legal Department.
#
# Altair's dual-license business model allows companies, individuals, and
# organizations to create proprietary derivative works of OpenPBS and
# distribute them - whether embedded or bundled with other software -
# under a commercial license agreement.
#
# Use of Altair's trademarks, including but not limited to "PBS™",
# "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
# subject to Altair's trademark licensing policies.


from tests.functional import *


class TestPreemption(TestFunctional):
    """
    Contains tests for scheduler's preemption functionality
    """
    chk_script = """#!/bin/bash
kill $1
exit 0
"""
    chk_script_fail = """#!/bin/bash
exit 1
"""

    def setUp(self):
        TestFunctional.setUp(self)

        a = {'resources_available.ncpus': 1}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom.shortname)

        # create express queue
        a = {'queue_type': 'execution',
             'started': 'True',
             'enabled': 'True',
             'Priority': 200}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, "expressq")
        if len(self.moms) == 2:
            self.mom1 = self.moms.keys()[0]
            self.mom2 = self.moms.keys()[1]
            # Since some tests need multi-node setup and majority don't,
            # delete the second node so that single node tests don't fail.
            # Tests needing multi-node setup will create the second node
            # explicity.
            self.server.manager(MGR_CMD_DELETE, NODE, id=self.mom2)

    def submit_jobs(self):
        """
        Function to submit two normal job and one high priority job
        """
        j1 = Job(TEST_USER)
        jid1 = self.server.submit(j1)
        self.server.expect(JOB, {ATTR_state: 'R'}, id=jid1)
        time.sleep(1)
        j2 = Job(TEST_USER)
        jid2 = self.server.submit(j2)
        self.server.expect(JOB, {ATTR_state: 'R'}, id=jid2)

        j3 = Job(TEST_USER)
        j3.set_attributes({ATTR_q: 'expressq'})
        jid3 = self.server.submit(j3)

        return jid1, jid2, jid3

    def submit_and_preempt_jobs(self, preempt_order='R', order=None,
                                job_array=False, extra_attrs=None):
        """
        This function will set the prempt order, submit jobs,
        preempt jobs and do log_match()
        """
        if preempt_order[-1] == 'R':
            job_state = 'Q'
            preempted_by = 'requeuing'
        elif preempt_order[-1] == 'C':
            job_state = 'Q'
            preempted_by = 'checkpointing'
        elif preempt_order[-1] == 'S':
            job_state = 'S'
            preempted_by = 'suspension'
        elif preempt_order[-1] == 'D':
            job_state = ''
            preempted_by = 'deletion'

        # construct preempt_order with a number inbetween.  We use 50
        # since that will cause a different preempt_order to be used for the
        # first 50% and a different for the second 50%
        if order == 1:  # first half
            po = '"' + preempt_order + ' 50 S"'
        elif order == 2:  # second half
            po = '"S 50 ' + preempt_order + '"'
        else:
            po = preempt_order

        # set preempt order
        self.server.manager(MGR_CMD_SET, SCHED, {'preempt_order': po})

        lpattrs = {ATTR_l + '.select': '1:ncpus=1', ATTR_l + '.walltime': 40}
        if job_array is True:
            lpattrs[ATTR_J] = '1-3'
        if extra_attrs is not None:
            lpattrs.update(extra_attrs)

        # submit a job to regular queue
        j1 = Job(TEST_USER, lpattrs)
        jid1 = self.server.submit(j1)
        if job_array is True:
            run_state = 'B'
        else:
            run_state = 'R'
        self.server.expect(JOB, {'job_state': run_state}, id=jid1)

        if job_array is True:
            jids1 = j1.create_subjob_id(jid1, 1)
            self.server.expect(JOB, {'job_state': 'R'}, id=jids1)

        if order == 2:
            self.logger.info('Sleep 30s until the job is over 50% done')
            time.sleep(30)

        # submit a job to high priority queue
        j2 = Job(TEST_USER, {'queue': 'expressq'})
        jid2 = self.server.submit(j2)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid2)

        if job_array is True:
            jid = jids1
        else:
            jid = jid1

        if preempt_order[-1] != 'D':
            self.server.expect(JOB, {'job_state': job_state}, id=jid)
        elif job_array is True:
            self.server.expect(JOB, {'job_state': 'X'}, id=jids1)
        else:
            self.server.expect(JOB, 'queue', op=UNSET, id=jid)

        self.scheduler.log_match(jid + ";Job preempted by " + preempted_by)

    def test_preempt_suspend(self):
        """
        Test that a job is preempted by suspension
        """
        self.submit_and_preempt_jobs(preempt_order='S')

    def test_preempt_suspend_ja(self):
        """
        Test that a subjob is preempted by suspension
        """
        self.submit_and_preempt_jobs(preempt_order='S', job_array=True)

    def test_preempt_checkpoint(self):
        """
        Test that a job is preempted with checkpoint
        """
        self.mom.add_checkpoint_abort_script(body=self.chk_script)
        self.submit_and_preempt_jobs(preempt_order='C')

    def test_preempt_checkpoint_requeue(self):
        """
        Test that when checkpoint fails, a job is correctly requeued
        """
        # no checkpoint script, should requeue
        self.submit_and_preempt_jobs(preempt_order='CR')
        self.server.cleanup_jobs()

        # checkpoint script fails, should requeue
        self.mom.add_checkpoint_abort_script(body=self.chk_script_fail)
        self.submit_and_preempt_jobs(preempt_order='CR')

    def test_preempt_requeue(self):
        """
        Test that a job is preempted by requeue
        """
        self.submit_and_preempt_jobs(preempt_order='R')

    @skipOnCpuSet
    def test_preempt_requeue_exclhost(self):
        """
        Test that a job is preempted by requeue on node
        where attribute share is set to force_exclhost
        """
        # set node share attribute to force_exclhost
        a = {'resources_available.ncpus': '1',
             'sharing': 'force_exclhost'}
        self.mom.create_vnodes(attrib=a, num=0)
        start_time = time.time()
        self.submit_and_preempt_jobs(preempt_order='R')
        self.scheduler.log_match(
            "Failed to run: Resource temporarily unavailable (15044)",
            existence=False, starttime=start_time,
            max_attempts=5)

    def test_preempt_requeue_ja(self):
        """
        Test that a subjob is preempted by requeue
        """
        self.submit_and_preempt_jobs(preempt_order='R', job_array=True)

    def test_preempt_delete(self):
        """
        Test preempt via delete correctly deletes a job
        """
        self.submit_and_preempt_jobs(preempt_order='D')

    def test_preempt_delete_ja(self):
        """
        Test preempt via delete correctly deletes a subjob
        """

        self.submit_and_preempt_jobs(preempt_order='D', job_array=True)

    def test_preempt_checkpoint_delete(self):
        """
        Test that when checkpoint fails, a job is correctly deleted
        """
        self.mom.add_checkpoint_abort_script(body=self.chk_script_fail)
        self.submit_and_preempt_jobs(preempt_order='CD')

    def test_preempt_rerunable_false(self):
        # in CLI mode Rerunnable requires a 'n' value.  It's different with API
        m = self.server.get_op_mode()

        self.server.set_op_mode(PTL_CLI)
        a = {'Rerunable': 'n'}
        self.submit_and_preempt_jobs(preempt_order='RD', extra_attrs=a)

        self.server.set_op_mode(m)

    def test_preempt_checkpoint_false(self):
        # in CLI mode Checkpoint requires a 'n' value.  It's different with API
        m = self.server.get_op_mode()
        self.server.set_op_mode(PTL_CLI)
        self.mom.add_checkpoint_abort_script(body=self.chk_script)
        a = {'Checkpoint': 'n'}
        self.submit_and_preempt_jobs(preempt_order='CD', extra_attrs=a)

        self.server.set_op_mode(m)

    def test_preempt_order_requeue_first(self):
        """
        Test that a low priority job is requeued if preempt_order is in
        the form of 'R 50 S' and the job is in the first 50% of its run time
        """
        self.submit_and_preempt_jobs(preempt_order='R', order=1)

    def test_preempt_order_requeue_second(self):
        """
        Test that a low priority job is requeued if preempt_order is in
        the form of 'S 50 R' and the job is in the second 50% of its run time
        """
        self.submit_and_preempt_jobs(preempt_order='R', order=2)

    def test_preempt_requeue_never_run(self):
        """
        Test that a job is preempted by requeue and the scheduler does not
        report the job as can never run
        """
        start_time = time.time()
        self.submit_and_preempt_jobs(preempt_order='R')
        self.scheduler.log_match(
            ";Job will never run", existence=False, starttime=start_time,
            max_attempts=5)

    def test_preempt_multiple_jobs(self):
        """
        Test that multiple jobs are preempted by one large high priority job
        """
        a = {'resources_available.ncpus': 10}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom.shortname)

        for _ in range(10):
            a = {'Resource_List.select': '1:ncpus=1',
                 'Resource_List.walltime': 40}
            j = Job(TEST_USER, a)
            self.server.submit(j)

        self.server.expect(JOB, {'job_state=R': 10})
        a = {'Resource_List.select': '1:ncpus=10',
             'Resource_List.walltime': 40,
             'queue': 'expressq'}
        hj = Job(TEST_USER, a)
        hjid = self.server.submit(hj)

        self.server.expect(JOB, {'job_state=S': 10})
        self.server.expect(JOB, {'job_state': 'R'}, id=hjid)

    def test_qalter_preempt_targets_to_none(self):
        """
        Test that a job requesting preempt targets set to two different queues
        can be altered to set preempt_targets as NONE
        """

        # create an addition queue
        a = {'queue_type': 'execution',
             'started': 'True',
             'enabled': 'True'}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, "workq2")

        self.server.manager(MGR_CMD_SET, SCHED, {'scheduling': 'False'})
        # submit a job in expressq with preempt targets set to workq, workq2
        a = {'Resource_List.preempt_targets': 'queue=workq,queue=workq2'}
        j = Job(TEST_USER, a)
        jid = self.server.submit(j)

        self.server.alterjob(jobid=jid,
                             attrib={'Resource_List.preempt_targets': 'None'})
        self.server.expect(JOB, id=jid,
                           attrib={'Resource_List.preempt_targets': 'None'})

    def test_preempt_sort_when_set(self):
        """
        This test is for preempt_sort when it is set to min_time_since_start
        """
        a = {ATTR_rescavail + '.ncpus': 2}
        self.server.manager(MGR_CMD_SET, NODE, a, self.mom.shortname)

        a = {'preempt_sort': 'min_time_since_start'}
        self.server.manager(MGR_CMD_SET, SCHED, a)

        jid1, jid2, jid3 = self.submit_jobs()
        self.server.expect(JOB, {ATTR_state: 'R'}, id=jid1)
        self.server.expect(JOB, {ATTR_state: 'S'}, id=jid2)
        self.server.expect(JOB, {ATTR_state: 'R'}, id=jid3)

    def test_preempt_retry(self):
        """
        Test that jobs can be successfully preempted after a previously failed
        attempt at preemption.
        """
        # in CLI mode Rerunnable requires a 'n' value.  It's different with API
        m = self.server.get_op_mode()

        self.server.set_op_mode(PTL_CLI)

        a = {'resources_available.ncpus': 2}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom.shortname)

        abort_script = """#!/bin/bash
exit 3
"""
        self.mom.add_checkpoint_abort_script(body=abort_script)
        # submit two jobs to regular queue
        attrs = {'Resource_List.select': '1:ncpus=1', 'Rerunable': 'n'}
        j1 = Job(TEST_USER, attrs)
        jid1 = self.server.submit(j1)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid1)

        time.sleep(2)

        j2 = Job(TEST_USER, attrs)
        jid2 = self.server.submit(j2)

        self.server.expect(JOB, {'job_state': 'R'}, id=jid2)

        # set preempt order
        self.server.manager(MGR_CMD_SET, SCHED, {'preempt_order': 'CR'})

        # submit a job to high priority queue
        a = {ATTR_q: 'expressq'}
        j3 = Job(TEST_USER, a)
        jid3 = self.server.submit(j3)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid1)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid2)
        self.server.expect(JOB, {'job_state': 'Q'}, id=jid3)

        self.server.log_match(jid1 + ';Job failed to be preempted')
        self.server.log_match(jid2 + ';Job failed to be preempted')

        # Allow jobs to be requeued.
        attrs = {'Rerunable': 'y'}
        self.server.alterjob(jid1, attrs)
        self.server.alterjob(jid2, attrs)

        self.server.manager(MGR_CMD_SET, SERVER, {'scheduling': 'True'})
        self.server.expect(JOB, {'job_state': 'R'}, id=jid1)
        self.server.expect(JOB, {'job_state': 'Q'}, id=jid2)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid3)
        self.server.set_op_mode(m)

    def test_vnode_resource_contention(self):
        """
        Test to make sure that preemption happens when the resource in
        contention is vnode.
        """
        vn4 = self.mom.shortname + '[4]'
        a = {'resources_available.ncpus': 2}
        self.mom.create_vnodes(attrib=a, num=11, usenatvnode=False)

        a = {'Resource_List.select': '1:ncpus=2+1:ncpus=2'}
        for _ in range(5):
            j = Job(TEST_USER, attrs=a)
            jid = self.server.submit(j)
            self.server.expect(JOB, {'job_state': 'R'}, id=jid)

        # Randomly select a vnode with running jobs on it. Request this
        # vnode in the high priority job later.
        self.server.expect(NODE, {'state': 'job-busy'}, id=vn4)

        a = {ATTR_q: 'expressq', 'Resource_List.vnode': vn4}
        hj = Job(TEST_USER, attrs=a)
        hjid = self.server.submit(hj)
        self.server.expect(JOB, {'job_state': 'R'}, id=hjid)

        # Since high priority job consumed only one ncpu, vnode[4]'s
        # node state should be free now
        self.server.expect(NODE, {'state': 'free'}, id=vn4)

    @requirements(num_moms=2)
    def test_host_resource_contention(self):
        """
        Test to make sure that preemption happens when the resource in
        contention is host.
        """
        # Skip test if number of mom provided is not equal to two
        if len(self.moms) != 2:
            self.skipTest("test requires two MoMs as input, " +
                          "use -p moms=<mom1>:<mom2>")
        else:
            self.server.manager(MGR_CMD_CREATE, NODE, id=self.mom2)

        a = {'resources_available.ncpus': 2}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom1)
        a = {'resources_available.ncpus': 3}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom2)

        a = {'Resource_List.select': '1:ncpus=2'}
        j1 = Job(TEST_USER, attrs=a)
        jid1 = self.server.submit(j1)
        j2 = Job(TEST_USER, attrs=a)
        jid2 = self.server.submit(j2)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid1)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid2)

        # Stat job to check which job is running on mom1
        pjid = jid2
        job_stat = self.server.status(JOB, id=jid1)
        ehost = job_stat[0]['exec_host'].partition('/')[0]
        if ehost == self.mom1:
            pjid = jid1

        # Submit a express queue job requesting the host
        a = {ATTR_q: 'expressq', 'Resource_List.host': self.mom1}
        hj = Job(TEST_USER, attrs=a)
        hjid = self.server.submit(hj)
        self.server.expect(JOB, {'job_state': 'R'}, id=hjid)
        self.server.expect(JOB, {'job_state': 'S'}, id=pjid)

        # Submit another express queue job requesting the host,
        # this job will stay queued
        a = {ATTR_q: 'expressq', 'Resource_List.host': self.mom1,
             'Resource_List.ncpus': 2}
        hj2 = Job(TEST_USER, attrs=a)
        hjid2 = self.server.submit(hj2)
        self.server.expect(JOB, {'job_state': 'Q'}, id=hjid2)
        comment = "Not Running: Insufficient amount of resource: host"
        self.server.expect(JOB, {'comment': comment}, id=hjid2)

    def test_preempt_queue_restart(self):
        """
        Test that a queue which has preempt_targets set to another queue
        recovers successfully before the target queue during server restart
        """
        # create an addition queue
        a = {'queue_type': 'execution',
             'started': 'True',
             'enabled': 'True'}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, "workq2")

        # create an addition queue
        a = {'queue_type': 'execution',
             'started': 'True',
             'enabled': 'True'}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, "workq3")

        a = {'resources_default.preempt_targets': 'queue=workq3'}
        self.server.manager(MGR_CMD_SET, QUEUE, a, "workq2")
        self.server.expect(QUEUE, a, id='workq2')
        self.server.manager(MGR_CMD_SET, QUEUE, a, "workq3")

        self.server.restart()

        try:
            self.server.expect(QUEUE, a, id='workq2', max_attempts=1)
        except PtlExpectError:
            self.server.stop()
            reset_db = 'echo y | ' + \
                os.path.join(self.server.pbs_conf['PBS_EXEC'],
                             'sbin', 'pbs_server') + ' -t create'
            self.du.run_cmd(cmd=reset_db, sudo=True, as_script=True)
            self.fail('TC failed as workq2 recovery failed')

    def test_insufficient_server_rassn_select_resc(self):
        """
        Set a rassn_select resource (like ncpus or mem) ons server and
        check if scheduler is able to preempt a lower priority job when
        resource in contention is this rassn_select resource.
        """

        a = {ATTR_rescavail + ".ncpus": "8"}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom.shortname)

        # Make resource ncpu available on server
        a = {ATTR_rescavail + ".ncpus": 4}
        self.server.manager(MGR_CMD_SET, SERVER, a)

        a = {ATTR_l + '.select': '1:ncpus=3'}
        j = Job(TEST_USER, attrs=a)
        jid_low = self.server.submit(j)
        self.server.expect(JOB, {ATTR_state: 'R'}, id=jid_low)

        a = {ATTR_l + '.select': '1:ncpus=3', ATTR_q: 'expressq'}
        j = Job(TEST_USER, attrs=a)
        jid_high = self.server.submit(j)

        self.server.expect(JOB, {ATTR_state: 'R'}, id=jid_high)

    def test_preemption_priority_escalation(self):
        """
        Test that scheduler does not try preempting a job that escalates its
        preemption priority when preempted.
        """
        # create an addition queue
        a = {'queue_type': 'execution',
             'started': 'True',
             'enabled': 'True'}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, "workq2")

        a = {'resources_available.ncpus': 8}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom.shortname)

        a = {'max_run_res_soft.ncpus': "[u:" + str(TEST_USER) + "=4]"}
        self.server.manager(MGR_CMD_SET, QUEUE, a, 'workq')

        a = {'max_run_res_soft.ncpus': "[u:" + str(TEST_USER2) + "=2]"}
        self.server.manager(MGR_CMD_SET, SERVER, a)
        p = "express_queue, normal_jobs, server_softlimits, queue_softlimits"
        a = {'preempt_prio': p}
        self.server.manager(MGR_CMD_SET, SCHED, a)
        self.server.manager(MGR_CMD_SET, SCHED, {'log_events':  2047})

        # Submit 4 jobs requesting 1 ncpu each in workq
        a = {ATTR_l + '.select': '1:ncpus=1'}
        jid_list = []
        for _ in range(4):
            j = Job(TEST_USER, a)
            jid = self.server.submit(j)
            jid_list.append(jid)

        # Submit 5th job that will make all the job in workq to go over its
        # softlimits
        a = {ATTR_l + '.select': '1:ncpus=1'}
        j = Job(TEST_USER, a)
        jid = self.server.submit(j)
        jid_list.append(jid)
        self.server.expect(JOB, {'job_state=R': 5})

        # Submit a job in workq2 which requests for 3 ncpus, this job will
        # make user2 go over its soft limits
        a = {ATTR_l + '.select': '1:ncpus=3', ATTR_q: 'workq2'}
        j = Job(TEST_USER2, a)
        jid = self.server.submit(j)
        jid_list.append(jid)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid)
        # Submit a job in workq2 which requests for 1 ncpus, this job will
        # not preempt because if it does then all TEST_USER jobs will move
        # from being over queue softlimits to normal.
        a = {ATTR_l + '.select': '1:ncpus=1', ATTR_q: 'workq2'}
        j = Job(TEST_USER2, a)
        jid = self.server.submit(j)
        jid_list.append(jid)
        self.server.expect(JOB, {'job_state': 'Q'}, id=jid)
        msg = ";Preempting job will escalate its priority"
        for job_id in jid_list[0:-2]:
            self.scheduler.log_match(job_id + msg)

    def test_preemption_priority_escalation_2(self):
        """
        Test that scheduler does not try preempting a job that escalates its
        preemption priority when preempted. But in this case ensure that the
        job whose preemption priority gets escalated is one of the running
        jobs that scheduler is yet to preempt in simulated universe.
        """
        # create an addition queue
        a = {'queue_type': 'execution',
             'started': 'True',
             'enabled': 'True'}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, "workq2")

        a = {'resources_available.ncpus': 10}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom.shortname)

        a = {'type': 'long', 'flag': 'nh'}
        self.server.manager(MGR_CMD_CREATE, RSC, a, id='foo')

        a = {'resources_available.foo': 10}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom.shortname)
        self.scheduler.add_resource('foo')

        a = {'max_run_res_soft.ncpus': "[u:PBS_GENERIC=5]"}
        self.server.manager(MGR_CMD_SET, QUEUE, a, 'workq')
        # Set a soft limit on resource foo to 0 so that all jobs requesting
        # this resource are over soft limits.
        a = {'max_run_res_soft.foo': "[u:PBS_GENERIC=0]"}
        self.server.manager(MGR_CMD_SET, QUEUE, a, 'workq')

        p = "express_queue, normal_jobs, queue_softlimits"
        a = {'preempt_prio': p}
        self.server.manager(MGR_CMD_SET, SCHED, a)
        self.server.manager(MGR_CMD_SET, SCHED, {'log_events':  2047})

        # Submit 4 jobs requesting 1 ncpu each in workq
        jid_list = []
        for index in range(4):
            a = {ATTR_l + '.select': '1:ncpus=1:foo=2'}
            if (index == 2):
                # Since this job is not requesting foo, preempting one job
                # from this queue will escalate its preemption priority to
                # normal and scheduler will not attempt to preempt it.
                a = {ATTR_l + '.select': '1:ncpus=1'}
            j = Job(TEST_USER, a)
            jid = self.server.submit(j)
            jid_list.append(jid)
            time.sleep(1)

        # Submit 5th job that will make all the job in workq to go over its
        # softlimits because if resource ncpus
        a = {ATTR_l + '.select': '1:ncpus=2:foo=2'}
        j = Job(TEST_USER, a)
        jid = self.server.submit(j)
        jid_list.append(jid)
        self.server.expect(JOB, {'job_state=R': 5})

        # Submit a job in workq2 which requests for 8 ncpus and 3 foo resource
        a = {ATTR_l + '.select': '1:ncpus=8:foo=3', ATTR_q: 'workq2'}
        j = Job(TEST_USER, a)
        jid = self.server.submit(j)
        jid_list.append(jid)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid_list[5])
        self.server.expect(JOB, {'job_state': 'R'}, id=jid_list[2])
        self.server.expect(JOB, {'job_state': 'R'}, id=jid_list[0])
        self.server.expect(JOB, {'job_state': 'S'}, id=jid_list[1])
        self.server.expect(JOB, {'job_state': 'S'}, id=jid_list[3])
        self.server.expect(JOB, {'job_state': 'S'}, id=jid_list[4])

    def test_preempt_requeue_resc(self):
        """
        Test that scheduler will preempt jobs for resources with rrtros
        set for other resources
        """
        a = {'resources_available.ncpus': 2}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom.shortname)

        a = {'type': 'long', 'flag': 'q'}
        self.server.manager(MGR_CMD_CREATE, RSC, a, id='foo')

        self.server.manager(MGR_CMD_SET, SCHED, {'preempt_order': 'R'})

        a = {'resources_available.foo': 2,
             ATTR_restrict_res_to_release_on_suspend: 'ncpus'}
        self.server.manager(MGR_CMD_SET, SERVER, a)
        self.scheduler.add_resource('foo')

        a = {'Resource_List.foo': 1}
        jid1 = self.server.submit(Job(attrs=a))
        jid2 = self.server.submit(Job(attrs=a))

        self.server.expect(JOB, {'job_state=R': 2})
        a = {'Resource_List.foo': 1,
             'queue': 'expressq'}
        hjid = self.server.submit(Job(attrs=a))

        self.server.expect(JOB, {'job_state': 'R'}, id=hjid)
        self.server.expect(JOB, {'job_state=Q': 1})

    def test_preempt_wrong_cull(self):
        """
        Test to make sure that if a preemptor cannot run because
        it misses a non-consumable on a node, preemption candidates
        are not incorrectly removed from consideration
        if and because they "do not request the relevant resource".
        Deciding on their utility should be left to the check
        to see whether the nodes they occupy are useful.
        """
        attr = {'type': 'string_array', 'flag': 'h'}
        self.server.manager(MGR_CMD_CREATE, RSC, attr, id='app')
        self.scheduler.add_resource('app')

        a = {'resources_available.ncpus': 1, 'resources_available.app': 'appA'}
        self.mom.create_vnodes(a, num=1,
                               usenatvnode=False, vname='vna')
        b = {'resources_available.ncpus': 1, 'resources_available.app': 'appB'}
        self.mom.create_vnodes(b, num=1,
                               usenatvnode=False, additive=True, vname='vnb')

        # set the preempt_order to kill/requeue only -- try old and new syntax
        self.server.manager(MGR_CMD_SET, SCHED, {'preempt_order': 'R'})

        # create express queue
        a = {'queue_type': 'execution',
             'started': 'True',
             'enabled': 'True',
             'Priority': 200}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, "hipri")

        # create normal queue
        a = {'queue_type': 'execution',
             'started': 'True',
             'enabled': 'True',
             'Priority': 1}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, "lopri")

        # submit job 1
        a = {'Resource_List.select': '1:ncpus=1:vnode=vna[0]', ATTR_q: 'lopri'}
        j1 = Job(TEST_USER, attrs=a)
        jid1 = self.server.submit(j1)

        self.server.expect(JOB, {'job_state': 'R'}, id=jid1)

        # submit job 2
        a = {'Resource_List.select': '1:ncpus=1:app=appA', ATTR_q: 'hipri'}
        j2 = Job(TEST_USER, attrs=a)
        jid2 = self.server.submit(j2)

        self.server.expect(JOB, {'job_state': 'R'}, id=jid2)

    @requirements(num_moms=2)
    def test_chunk_level_host_resource_contention(self):
        """
        Test to make sure that preemption happens when the resource in
        contention is host requested inside a chunk.
        """
        # Skip test if number of mom provided is not equal to two
        if len(self.moms) != 2:
            self.skipTest("test requires two MoMs as input, " +
                          "use -p moms=<mom1>:<mom2>")
        else:
            self.server.manager(MGR_CMD_CREATE, NODE, id=self.mom2)

        a = {'resources_available.ncpus': 2}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom1)
        a = {'resources_available.ncpus': 3}
        self.server.manager(MGR_CMD_SET, NODE, a, id=self.mom2)

        a = {'Resource_List.select': '1:ncpus=2'}
        j1 = Job(TEST_USER, attrs=a)
        jid1 = self.server.submit(j1)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid1)

        # Stat job to check which job is running on mom1
        pjid = jid1
        job_stat = self.server.status(JOB, id=jid1)
        ehost = job_stat[0]['exec_host'].partition('/')[0]

        # Submit a express queue job requesting the host
        a = {ATTR_q: 'expressq',
             'Resource_List.select': '1:ncpus=2:host=' + ehost}
        hj = Job(TEST_USER, attrs=a)
        hjid = self.server.submit(hj)
        self.server.expect(JOB, {'job_state': 'R'}, id=hjid)
        self.server.expect(JOB, {'job_state': 'S'}, id=pjid)

        # Submit another express queue job requesting the host,
        # this job will stay queued
        a = {ATTR_q: 'expressq', 'Resource_List.host': self.mom1,
             'Resource_List.ncpus': 2}
        hj2 = Job(TEST_USER, attrs=a)
        hjid2 = self.server.submit(hj2)
        self.server.expect(JOB, {'job_state': 'Q'}, id=hjid2)
        comment = "Not Running: Insufficient amount of resource: host"
        self.server.expect(JOB, {'comment': comment}, id=hjid2)

    def test_chunk_level_vnode_resource_contention(self):
        """
        Test to make sure that preemption happens when the resource in
        contention is vnode requested inside a chunk.
        """

        a = {'resources_available.ncpus': 2}
        self.mom.create_vnodes(attrib=a, num=11, usenatvnode=False)

        a = {'Resource_List.select': '1:ncpus=2+1:ncpus=2'}
        for _ in range(5):
            j = Job(TEST_USER, attrs=a)
            jid = self.server.submit(j)
            self.server.expect(JOB, {'job_state': 'R'}, id=jid)

        # Select a vnode with running jobs on it. Request this
        # vnode in the high priority job later.
        vn4 = self.mom.shortname + '[4]'
        self.server.expect(NODE, {'state': 'job-busy'}, id=vn4)

        a = {ATTR_q: 'expressq',
             'Resource_List.select': '1:ncpus=1:vnode=' + vn4}
        hj = Job(TEST_USER, attrs=a)
        hjid = self.server.submit(hj)
        self.server.expect(JOB, {'job_state': 'R'}, id=hjid)
        self.server.expect(JOB, {'job_state=R': 5})
        self.server.expect(JOB, {'job_state=S': 1})
