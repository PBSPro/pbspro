# coding: utf-8

# Copyright (C) 1994-2020 Altair Engineering, Inc.
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

import textwrap
from tests.functional import *


def get_hook_body_reverse_node_state():
    hook_body = """
    import pbs
    e = pbs.event()
    pbs.logmsg(pbs.LOG_DEBUG, "pbs.__file__:" + pbs.__file__)
    # this is backwards as it's a reverse lookup.
    for value, key in pbs.REVERSE_RESV_STATE.items():
        pbs.logmsg(pbs.LOG_DEBUG, "key:%s value:%s" % (key, value))
    e.accept()
    """
    hook_body = textwrap.dedent(hook_body)
    return hook_body


class TestResvBeginHook(TestFunctional):
    """
    Tests to verify the reservation begin hook for a confirm standing/advance/
    degraded reservation once the reservation begins.
    """

    advance_resv_hook_script = """
import pbs
e=pbs.event()

pbs.logmsg(pbs.LOG_DEBUG, 'Reservation Begin Hook name - %s' % e.hook_name)

if e.type == pbs.RESV_BEGIN:
    pbs.logmsg(pbs.LOG_DEBUG, 'Reservation ID - %s' % e.resv.resvid)
"""

    standing_resv_hook_script = """
import pbs
e=pbs.event()

pbs.logmsg(pbs.LOG_DEBUG, 'Reservation Begin Hook name - %s' % e.hook_name)

if e.type == pbs.RESV_BEGIN:
    pbs.logmsg(pbs.LOG_DEBUG, 'Reservation occurrence - %s' %
    e.resv.reserve_index)
"""

    def setUp(self):
        """
        Create a reservation begin hook and set the server log level.
        """
        super(TestResvBeginHook, self).setUp()
        self.hook_name = 'resvbegin_hook'
        attrs = {'event': 'resv_begin'}
        self.server.create_hook(self.hook_name, attrs)

        self.server.manager(MGR_CMD_SET, SERVER, {'log_events': 2047})

    @tags('hooks')
    def test_delete_advance_resv(self):
        """
        Testcase to submit and confirm advance reservation, delete the same
        and verify the resvbegin hook did not run.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.advance_resv_hook_script)

        offset = 10
        duration = 30
        rid = self.server.submit_resv(offset, duration)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        self.server.delete(rid)
        msg = 'Hook;Server@%s;Reservation ID - %s' % \
              (self.server.shortname, rid)
        self.server.log_match(msg, tail=True, interval=1, max_attempts=10,
                              existence=False)

    @tags('hooks')
    def test_delete_degraded_resv(self):
        """
        Testcase to submit and confirm an advance reservation, turn the mom
        off, delete the degraded reservation and verify the resvbegin
        hook did not run.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.advance_resv_hook_script)

        offset = 10
        duration = 30
        rid = self.server.submit_resv(offset, duration)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        self.mom.stop()

        attrs['reserve_state'] = (MATCH_RE, 'RESV_DEGRADED|10')
        self.server.expect(RESV, attrs, id=rid)

        self.server.delete(rid)
        msg = 'Hook;Server@%s;Reservation ID - %s' % \
              (self.server.shortname, rid)
        self.server.log_match(msg, tail=True, interval=1, max_attempts=10,
                              existence=False)

    @tags('hooks')
    def test_server_down_case_1(self):
        """
        Testcase to submit and confirm an advance reservation, turn the server
        off, turn the server on, delete the reservation after start and verify
        the resvbegin hook ran.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.advance_resv_hook_script)

        offset = 10
        duration = 300
        rid = self.server.submit_resv(offset, duration)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        self.server.stop()

        self.server.start()

        time.sleep(11)

        self.server.delete(rid)

        msg = 'Hook;Server@%s;Reservation ID - %s' % \
              (self.server.shortname, rid)
        self.server.log_match(msg, tail=True, interval=1, max_attempts=10)

    @tags('hooks')
    @timeout(300)
    def test_server_down_case_2(self):
        """
        Testcase to submit and confirm an advance reservation, turn the
        server off, wait for the reservation duration to finish, turn the
        server on and verify the resvbegin hook never ran.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.advance_resv_hook_script)

        offset = 10
        duration = 30
        wait_time = offset + duration + 5
        rid = self.server.submit_resv(offset, duration)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        self.server.stop()

        self.logger.info('wait for %s seconds till the reservation would end' %
                         (wait_time))
        time.sleep(wait_time)

        self.server.start()

        msg = 'Hook;Server@%s;Reservation ID - %s' % \
              (self.server.shortname, rid)
        self.server.log_match(msg, tail=True, interval=2, max_attempts=10,
                              existence=False)

    @tags('hooks')
    @timeout(30)
    def test_begin_advance_resv(self):
        """
        Testcase to submit and confirm an advance reservation, wait for it
        to begin and verify the reservation begin hook.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.advance_resv_hook_script)

        offset = 10
        duration = 30
        rid = self.server.submit_resv(offset, duration)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        attrs['reserve_state'] = (MATCH_RE, 'RESV_RUNNING|5')
        self.server.expect(RESV, attrs, id=rid, offset=10)

        # Don't need to wait.  Let teardown clear the reservation
        msg = 'Hook;Server@%s;Reservation ID - %s' % \
              (self.server.shortname, rid)
        self.server.log_match(msg, tail=True, interval=2, max_attempts=30)

    @tags('hooks')
    def test_set_attrs(self):
        """
        Testcase to submit and confirm an advance reservation, delete the
        reservation and verify the read permissions in the resvbegin hook.
        """

        hook_script = textwrap.dedent("""\
            import pbs
            e=pbs.event()

            pbs.logmsg(pbs.LOG_DEBUG,
                       'Reservation begin Hook name - %s' % e.hook_name)

            if e.type == pbs.RESV_BEGIN:
                pbs.logmsg(pbs.LOG_DEBUG, 'e.resv = %s' % e.resv.__dict__)
                e.resv.resources_used.walltime = 10
                pbs.logmsg(pbs.LOG_DEBUG, 'Reservation ID - %s' %
                e.resv.resources_used.walltime)
        """)

        self.server.import_hook(self.hook_name, hook_script)

        offset = 10
        duration = 30
        rid = self.server.submit_resv(offset, duration)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        time.sleep(15)

        self.server.delete(rid)
        msg = 'Svr;Server@%s;PBS server internal error (15011) in Error ' \
              'evaluating Python script, attribute '"'resources_used'"' is ' \
              'part of a readonly object' % self.server.shortname
        self.server.log_match(msg, tail=True, max_attempts=30, interval=2)

    @tags('hooks')
    @timeout(300)
    def test_delete_resv_after_first_occurrence(self):
        """
        Testcase to submit and confirm a standing reservation for two
        occurrences, wait for the first occurrence to begin and verify
        the begin hook for the same, delete before the second occurrence and
        verify the resvbegin hook for the latter didn't run.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.standing_resv_hook_script)

        offset = 10
        duration = 30
        rid = self.server.submit_resv(offset, duration,
                                      rrule='FREQ=MINUTELY;COUNT=2',
                                      conf=self.conf)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        attrs['reserve_state'] = (MATCH_RE, 'RESV_RUNNING|5')
        self.server.expect(RESV, attrs, id=rid, offset=10)

        self.logger.info('wait till 30 seconds until the reservation begins')
        time.sleep(30)

        msg = 'Hook;Server@%s;Reservation occurrence - 1' % \
              self.server.shortname
        self.server.log_match(msg, tail=True, interval=2, max_attempts=30)
        self.logger.info('Reservation begin hook ran for first occurrence of '
                         'a standing reservation')

        self.logger.info('delete during first occurence')

        self.server.delete(rid)
        msg = 'Hook;Server@%s;Reservation occurrence - 2' % \
              self.server.shortname
        self.server.log_match(msg, tail=True, interval=2, max_attempts=30,
                              existence=False)

    @tags('hooks')
    @timeout(300)
    def test_begin_resv_occurrences(self):
        """
        Testcase to submit and confirm a standing reservation for two
        occurrences, wait for the first occurrence to begin and verify
        the begin hook for the same, wait for the second occurrence to
        start and end, verify the resvbegin hook for the latter.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.standing_resv_hook_script)

        offset = 10
        duration = 30
        rid = self.server.submit_resv(offset, duration,
                                      rrule='FREQ=MINUTELY;COUNT=2',
                                      conf=self.conf)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        attrs['reserve_state'] = (MATCH_RE, 'RESV_RUNNING|5')
        self.server.expect(RESV, attrs, id=rid, offset=10)

        msg = 'Hook;Server@%s;Reservation occurrence - 1' % \
              self.server.shortname
        self.server.log_match(msg, tail=True, interval=2, max_attempts=30)
        self.logger.info('Reservation begin hook ran for first occurrence of a'
                         ' standing reservation')

        self.logger.info('Sleep for 30 seconds as this is a '
                         'minutely occurrence')
        time.sleep(30)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_RUNNING|5'),
                 'reserve_index': 2}
        self.server.expect(RESV, attrs, id=rid, attrop=PTL_AND)

        msg = 'Hook;Server@%s;Reservation occurrence - 2' % \
              self.server.shortname
        self.server.log_match(msg, tail=True, interval=2, max_attempts=30)
        self.logger.info('Reservation begin hook ran for second occurrence of '
                         'a standing reservation')

    @tags('hooks')
    @timeout(300)
    def test_delete_resv_occurrence_with_jobs(self):
        """
        Testcase to submit and confirm a standing reservation for two
        occurrences, submit some jobs to it, wait for the first
        occurrence to begin and verify the begin hook for the same,
        delete the second occurrence and verify the resvbegin hook
        for the latter.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.standing_resv_hook_script)

        offset = 10
        duration = 30
        rid = self.server.submit_resv(offset, duration,
                                      rrule='FREQ=MINUTELY;COUNT=2',
                                      conf=self.conf)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        attrs['reserve_state'] = (MATCH_RE, 'RESV_RUNNING|5')
        self.server.expect(RESV, attrs, id=rid, offset=10)

        job_attrs = {
            'Resource_List.walltime': 5,
            'Resource_List.select': '1:ncpus=1',
            'queue': rid.split('.')[0]
        }

        for _ in range(10):
            self.server.submit(Job(TEST_USER, job_attrs))

        self.logger.info('Sleep for 30 seconds for the reservation occurrence '
                         'to begin')
        time.sleep(30)

        msg = 'Hook;Server@%s;Reservation occurrence - 1' % \
              self.server.shortname
        self.server.log_match(msg, tail=True, interval=2, max_attempts=30)
        self.logger.info('Reservation begin hook ran for first occurrence of '
                         'a standing reservation')

        self.logger.info(
            'wait for 10 seconds till the next occurrence is submitted')
        time.sleep(10)

        self.server.delete(rid)
        msg = 'Hook;Server@%s;Reservation occurrence - 2' % \
              self.server.shortname
        self.server.log_match(msg, tail=True, interval=2, max_attempts=30)

    @tags('hooks')
    def test_unconfirmed_resv_with_node(self):
        """
        Testcase to set the node attributes such that the number of ncpus is 1,
        submit and confirm a reservation on the same node, submit another
        reservation on the same node and verify the reservation begin hook
        as the latter one stays in unconfirmed state.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.advance_resv_hook_script)

        node_attrs = {'resources_available.ncpus': 1}
        self.server.manager(MGR_CMD_SET, NODE, node_attrs,
                            id=self.mom.shortname)
        offset = 10
        duration = 10
        rid = self.server.submit_resv(offset, duration)

        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)

        new_rid = self.server.submit_resv(offset, duration)

        msg = 'Hook;Server@%s;Reservation ID - %s' % \
              (self.server.shortname, new_rid)
        self.server.log_match(msg, tail=True, max_attempts=10,
                              existence=False)

    @tags('hooks')
    @timeout(240)
    def test_scheduler_down(self):
        """
        Testcase to turn off the scheduler and submit a reservation,
        the same will be in unconfirmed state and upon ending the
        resvbegin hook shall not run.
        """
        self.server.import_hook(self.hook_name,
                                TestResvBeginHook.advance_resv_hook_script)

        self.scheduler.stop()

        offset = 10
        duration = 30
        rid = self.server.submit_resv(offset, duration)

        self.logger.info('wait for 30 seconds till the reservation begins ')
        time.sleep(30)

        msg = 'Hook;Server@%s;Reservation ID - %s' % \
              (self.server.shortname, rid)
        self.server.log_match(msg, tail=True, max_attempts=10,
                              existence=False)

    # Test Reverser
    @tags("hooks")
    def test_check_reservation_state_lookup(self):
        """
        Test: check for the existence and values of the
        pbs.REVERSE_RESV_STATE dictionary

        run a hook that converts reservation state change ints into a string,
        then search for it in the server log.
        """

        self.add_pbs_python_path_to_sys_path()
        import pbs
        self.server.import_hook(self.hook_name,
                                get_hook_body_reverse_node_state())
        start_time = int(time.time())

        duration = 30
        offset = 10
        rid = self.server.submit_resv(offset, duration)
        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)
        attrs = {'reserve_state': (MATCH_RE, 'RESV_RUNNING|5')}

        for value, key in pbs.REVERSE_RESV_STATE.items():
            self.server.log_match("key:%s value:%s" % (key, value),
                                  starttime=start_time)

    @tags('hooks')
    @timeout(30)
    def test_multiple_hooks(self):
        """Define multiple hooks for the resv_begin event and make sure
        both get run.

        """
        test_hook_script_1 = textwrap.dedent("""\
        import pbs
        e=pbs.event()

        pbs.logmsg(pbs.LOG_DEBUG,
                   'Reservation Begin Hook name - %s' % e.hook_name)

        if e.type == pbs.RESV_BEGIN:
            pbs.logmsg(pbs.LOG_DEBUG,
                       'Test 1 Reservation ID - %s' % e.resv.resvid)
        """)

        test_hook_script_2 = textwrap.dedent("""\
        import pbs
        e=pbs.event()

        pbs.logmsg(pbs.LOG_DEBUG,
                   'Reservation Begin Hook name - %s' % e.hook_name)

        if e.type == pbs.RESV_BEGIN:
            pbs.logmsg(pbs.LOG_DEBUG,
                       'Test 2 Reservation ID - %s' % e.resv.resvid)
        """)

        attrs = {'event': 'resv_begin'}
        self.server.create_hook("test_hook_1", attrs)
        self.server.create_hook("test_hook_2", attrs)
        self.server.import_hook("test_hook_1", test_hook_script_1)
        self.server.import_hook("test_hook_2", test_hook_script_2)

        offset = 10
        duration = 30
        rid = self.server.submit_resv(offset, duration)
        attrs = {'reserve_state': (MATCH_RE, 'RESV_CONFIRMED|2')}
        self.server.expect(RESV, attrs, id=rid)
        attrs['reserve_state'] = (MATCH_RE, 'RESV_RUNNING|5')
        self.server.expect(RESV, attrs, id=rid, offset=10)

        msg = 'Hook;Server@%s;Test 1 Reservation ID - %s' % \
              (self.server.shortname, rid)
        self.server.log_match(msg, tail=True, interval=2, max_attempts=30)

        msg = 'Hook;Server@%s;Test 2 Reservation ID - %s' % \
              (self.server.shortname, rid)
        self.server.log_match(msg, tail=True, interval=2, max_attempts=30)
