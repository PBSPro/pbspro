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


import collections
import copy
import logging
import math
import re
import sys
import time
import traceback
from datetime import datetime, timedelta, tzinfo
from subprocess import PIPE, Popen

from ptl.lib.pbs_testlib import (EQ, JOB, NODE, SET, BatchUtils, ResourceResv,
                                 Server, PbsAttribute)
from ptl.utils.pbs_dshutils import DshUtils

"""
Analyze ``server``, ``scheduler``, ``MoM``, and ``accounting`` logs.

- Scheduler log analysis:

    Extraction of per cycle information including:
        cycle start time
        cycle duration
        time to query objects from server
        number of jobs considered
        number of jobs run
        number of jobs that failed to run
        Number of jobs preempted
        Number of jobs that failed to preempt
        Number of jobs calendared
        time to determine that a job can run
        time to determine that a job can not run
        time spent calendaring
        time spent in scheduler solver
    Summary of all cycles information

- Server log analysis:
    job submit rate
    number of jobs ended
    number of jobs run
    job run rate
    job submit rate
    job end rate
    job wait time distribution
    PBS versions
    node up rate
    wait time

- Mom log analysis:
    job submit rate
    number of jobs ended
    number of jobs run
    job run rate
    job submit rate
    job end rate
    PBS versions

- Accounting log analysis:
    job submit rate
    number of jobs ended
    number of jobs run
    job run rate
    job submit rate
    job end rate
    job size (cpu and node) distribution
    job wait time distribution
    utilization
"""

tm_re = r'(?P<datetime>\d\d/\d\d/\d{4}\s\d\d:\d\d:\d\d(\.\d{6})?)'
job_re = r';(?P<jobid>[\d\[\d*\]]+)\.'
fail_re = r';(?P<jobid>[\d\[\[]+)\.'

# Server metrics
NUR = 'node_up_rate'

# Scheduler metrics
NC = 'num_cycles'
mCD = 'cycle_duration_min'
MCD = 'cycle_duration_max'
mCT = 'min_cycle_time'
MCT = 'max_cycle_time'
CDA = 'cycle_duration_mean'
CD25 = 'cycle_duration_25p'
CDA = 'cycle_duration_mean'
CD50 = 'cycle_duration_median'
CD75 = 'cycle_duration_75p'
CST = 'cycle_start_time'
CD = 'cycle_duration'
QD = 'query_duration'
NJC = 'num_jobs_considered'
NJFR = 'num_jobs_failed_to_run'
SST = 'scheduler_solver_time'
NJCAL = 'num_jobs_calendared'
NJFP = 'num_jobs_failed_to_preempt'
NJP = 'num_jobs_preempted'
T2R = 'time_to_run'
T2D = 'time_to_discard'
TiS = 'time_in_sched'
TTC = 'time_to_calendar'

# Scheduling Estimated Start Time
EST = 'estimates'
EJ = 'estimated_jobs'
Eat = 'estimated'
DDm = 'drift_duration_min'
DDM = 'drift_duration_max'
DDA = 'drift_duration_mean'
DD50 = 'drift_duration_median'
ND = 'num_drifts'
NJD = 'num_jobs_drifted'
NJND = 'num_jobs_no_drift'
NEST = 'num_estimates'
JDD = 'job_drift_duration'
ESTR = 'estimated_start_time_range'
ESTA = 'estimated_start_time_accuracy'
JST = 'job_start_time'
ESTS = 'estimated_start_time_summary'
Ds15mn = 'drifted_sub_15mn'
Ds1hr = 'drifted_sub_1hr'
Ds3hr = 'drifted_sub_3hr'
Do3hr = 'drifted_over_3hr'

# Accounting metrics
JWTm = 'job_wait_time_min'
JWTM = 'job_wait_time_max'
JWTA = 'job_wait_time_mean'
JWT25 = 'job_wait_time_25p'
JWT50 = 'job_wait_time_median'
JWT75 = 'job_wait_time_75p'
JRTm = 'job_run_time_min'
JRT25 = 'job_run_time_25p'
JRT50 = 'job_run_time_median'
JRTA = 'job_run_time_mean'
JRT75 = 'job_run_time_75p'
JRTM = 'job_run_time_max'
JNSm = 'job_node_size_min'
JNS25 = 'job_node_size_25p'
JNS50 = 'job_node_size_median'
JNSA = 'job_node_size_mean'
JNS75 = 'job_node_size_75p'
JNSM = 'job_node_size_max'
JCSm = 'job_cpu_size_min'
JCS25 = 'job_cpu_size_25p'
JCS50 = 'job_cpu_size_median'
JCSA = 'job_cpu_size_mean'
JCS75 = 'job_cpu_size_75p'
JCSM = 'job_cpu_size_max'
CPH = 'cpu_hours'
NPH = 'node_hours'
USRS = 'unique_users'
UNCPUS = 'utilization_ncpus'
UNODES = 'utilization_nodes'

# Generic metrics
VER = 'pbs_version'
JID = 'job_id'
JRR = 'job_run_rate'
JSR = 'job_submit_rate'
JER = 'job_end_rate'
JTR = 'job_throughput'
NJQ = 'num_jobs_queued'
NJR = 'num_jobs_run'
NJE = 'num_jobs_ended'
DUR = 'duration'
RI = 'custom_interval'
IT = 'init_time'
CF = 'custom_freq'
CFC = 'custom_freq_counts'
CG = 'custom_groups'

PARSER_OK_CONTINUE = 0
PARSER_OK_STOP = 1
PARSER_ERROR_CONTINUE = 2
PARSER_ERROR_STOP = 3


class PBSLogUtils(object):

    """
    Miscellaneous utilities to process log files
    """

    logger = logging.getLogger(__name__)
    du = DshUtils()

    @classmethod
    def convert_date_time(cls, dt=None, fmt=None):
        """
        convert a date time string of the form given by fmt into
        number of seconds since epoch (with possible microseconds).
        it considers the current system's timezone to convert
        the datetime to epoch time

        :param dt: the datetime string to convert
        :type dt: str or None
        :param fmt: Format to which datetime is to be converted
        :type fmt: str
        :returns: timestamp in seconds since epoch,
                or None if conversion fails
        """
        if dt is None:
            return None

        micro = False
        if fmt is None:
            if '.' in dt:
                micro = True
                fmt = "%m/%d/%Y %H:%M:%S.%f"
            else:
                fmt = "%m/%d/%Y %H:%M:%S"

        try:
            # Get datetime object
            t = datetime.strptime(dt, fmt)
            # Get epoch-timestamp assuming local timezone
            tm = t.timestamp()
        except ValueError:
            cls.logger.debug("could not convert date time: " + str(dt))
            return None

        if micro is True:
            return tm
        else:
            return int(tm)

    def get_num_lines(self, log, hostname=None, sudo=False):
        """
        Get the number of lines of particular log

        :param log: the log file name
        :type log: str
        """
        f = self.open_log(log, hostname, sudo=sudo)
        nl = sum([1 for _ in f])
        f.close()
        return nl

    def open_log(self, log, hostname=None, sudo=False, start=None,
                 num_records=None):
        """
        :param log: the log file name to read from
        :type log: str
        :param hostname: the hostname from which to read the file
        :type hostname: str or None
        :param sudo: Whether to access log file as a privileged user.
        :type sudo: boolean
        :returns: A file instance
        """
        readcmd = ['cat', log]
        taillogs = 10000
        tailcmd = [self.du.which(hostname, 'tail')]
        headcmd = [self.du.which(hostname, 'head')]
        if sudo:
            tailcmd = self.du.sudo_cmd + tailcmd
            headcmd = self.du.sudo_cmd + headcmd
        if hostname:
            tailcmd = ['ssh', hostname] + tailcmd
            headcmd = ['ssh', hostname] + headcmd

        if start:
            i = 0
            while(True):
                i += 1
                taillogs = 10000 * i
                args = tailcmd + ['-n', str(taillogs), log]
                args2 = headcmd + ['-n', '1']
                process_tail = Popen(args, stdout=PIPE,
                                     shell=False)
                process_head = Popen(args2, stdin=process_tail.stdout,
                                     stdout=PIPE, shell=False)
                process_tail.stdout.close()
                line = process_head.communicate()[0]
                line = line.decode("utf-8")
                ts = line.split(';')[0]
                epoch = self.convert_date_time(ts)
                readcmd = tailcmd + ['-n', str(taillogs), log]
                if start > epoch:
                    break
                elif taillogs > num_records:
                    readcmd = ['cat', log]
                    break
        try:
            if hostname is None or self.du.is_localhost(hostname):
                if sudo:
                    cmd = self.du.sudo_cmd + readcmd
                    self.logger.info('running ' + " ".join(cmd))
                    p = Popen(cmd, stdout=PIPE)
                    f = p.stdout
                else:
                    cmd = readcmd
                    p = Popen(cmd, stdout=PIPE)
                    f = p.stdout
            else:
                cmd = ['ssh', hostname]
                if sudo:
                    cmd += self.du.sudo_cmd
                cmd += readcmd
                self.logger.debug('running ' + " ".join(cmd))
                p = Popen(cmd, stdout=PIPE)
                f = p.stdout
        except Exception:
            self.logger.error(traceback.print_exc())
            self.logger.error('Problem processing file ' + log)
            f = None

        return f

    def get_timestamps(self, logfile=None, hostname=None, num=None,
                       sudo=False):
        """
        Helper function to parse logfile

        :returns: Each timestamp in a list as number of seconds since epoch
        """
        if logfile is None:
            return

        records = self.open_log(logfile, hostname, sudo=sudo)
        if records is None:
            return

        rec_times = []
        tm_tag = re.compile(tm_re)
        num_rec = 0
        for record in records:
            num_rec += 1
            if num is not None and num_rec > num:
                break

            if type(record) == bytes:
                record = record.decode("utf-8")

            m = tm_tag.match(record)
            if m:
                rec_times.append(
                    self.convert_date_time(m.group('datetime')))
        records.close()
        return rec_times

    def match_msg(self, lines, msg, allmatch=False, regexp=False,
                  starttime=None, endtime=None):
        """
        Returns (x,y) where x is the matching line y, or None if
        nothing is found.

        :param allmatch: If True (False by default), return a list
                         of matching tuples.
        :type allmatch: boolean
        :param regexp: If True, msg is a Python regular expression.
                       Defaults to False.
        :type regexp: bool
        :param starttime: If set ignore matches that occur before
                          specified time
        :param endtime: If set ignore matches that occur after
                        specified time
        """
        linecount = 0
        ret = []
        if lines:
            for l in lines:
                # l.split(';', 1)[0] gets the time stamp string
                dt_str = l.split(';', 1)[0]
                if starttime is not None:
                    tm = self.convert_date_time(dt_str)
                    if tm is None or tm < starttime:
                        continue
                if endtime is not None:
                    tm = self.convert_date_time(dt_str)
                    if tm is None or tm > endtime:
                        continue
                if ((regexp and re.search(msg, l)) or
                        (not regexp and l.find(msg) != -1)):
                    m = (linecount, l)
                    if allmatch:
                        ret.append(m)
                    else:
                        return m
                linecount += 1
        if len(ret) > 0:
            return ret
        return None

    @staticmethod
    def convert_resv_date_time(date_time):
        """
        Convert reservation datetime to seconds
        """
        try:
            t = time.strptime(date_time, "%a %b %d %H:%M:%S %Y")
        except Exception:
            t = time.localtime()
        return int(time.mktime(t))

    @staticmethod
    def convert_hhmmss_time(tm):
        """
        Convert datetime in hhmmss format to seconds
        """
        if ':' not in tm:
            return tm

        hms = tm.split(':')
        return int(int(hms[0]) * 3600 + int(hms[1]) * 60 + int(hms[2]))

    def get_rate(self, in_list=[]):
        """
        :returns: The frequency of occurrences of array l
                  The array is expected to be sorted
        """
        if len(in_list) > 0:
            duration = in_list[len(in_list) - 1] - in_list[0]
            if duration > 0:
                tm_factor = [1, 60, 60, 24]
                _rate = float(len(in_list)) / float(duration)
                index = 0
                while _rate < 1 and index < len(tm_factor):
                    index += 1
                    _rate *= tm_factor[index]
                _rate = "%.2f" % (_rate)
                if index == 0:
                    _rate = str(_rate) + '/s'
                elif index == 1:
                    _rate = str(_rate) + '/mn'
                elif index == 2:
                    _rate = str(_rate) + '/hr'
                else:
                    _rate = str(_rate) + '/day'
            else:
                _rate = str(len(in_list)) + '/s'
            return _rate
        return 0

    def in_range(self, tm, start=None, end=None):
        """
        :param tm: time to check within a provided range
        :param start: Lower limit for the time range
        :param end: Higer limit for the time range
        :returns: True if time is in the range else return False
        """
        if start is None and end is None:
            return True

        if start is None and end is not None:
            if tm <= end:
                return True
            else:
                return False

        if start is not None and end is None:
            if tm >= start:
                return True
            else:
                return False
        else:
            if tm >= start and tm <= end:
                return True
        return False

    @staticmethod
    def _duration(val=None):
        if val is not None:
            return str(timedelta(seconds=int(float(val))))

    @staticmethod
    def get_day(tm=None):
        """
        :param tm: Time for which to get a day
        """
        if tm is None:
            tm = time.time()
        return time.strftime("%Y%m%d", time.localtime(tm))

    @staticmethod
    def percentile(N, percent):
        """
        Find the percentile of a list of values.

        :param N: A list of values. Note N MUST BE already sorted.
        :type N: List
        :param percent: A float value from 0.0 to 1.0.
        :type percent: Float
        :returns: The percentile of the values
        """
        if not N:
            return None
        k = (len(N) - 1) * percent
        f = math.floor(k)
        c = math.ceil(k)
        if f == c:
            return N[int(k)]
        d0 = N[int(f)] * (c - k)
        d1 = N[int(c)] * (k - f)
        return d0 + d1

    @staticmethod
    def process_intervals(intervals, groups, frequency=60):
        """
        Process the intervals
        """
        info = {}
        if not intervals:
            return info

        val = [x - intervals[i - 1] for i, x in enumerate(intervals) if i > 0]
        info[RI] = ", ".join([str(v) for v in val])
        if intervals:
            info[IT] = intervals[0]
        if frequency is not None:
            _cf = []
            j = 0
            i = 1
            while i < len(intervals):
                if (intervals[i] - intervals[j]) > frequency:
                    _cf.append(((intervals[j], intervals[i - 1]), i - j))
                    j = i
                i += 1
            if i != j + 1:
                _cf.append(((intervals[j], intervals[i - 1]), i - j))
            else:
                _cf.append(((intervals[j], intervals[j]), 1))
            info[CFC] = _cf
            info[CF] = frequency
        if groups:
            info[CG] = groups
        return info

    def get_log_files(self, hostname, path, start, end, sudo=False):
        """
        :param hostname: Hostname of the machine
        :type hostname: str
        :param path: Path for the log file
        :type path: str
        :param start: Start time for the log file
        :param end: End time for the log file
        :returns: list of log file(s) found or an empty list
        """
        paths = []
        if self.du.isdir(hostname, path, sudo=sudo):
            logs = self.du.listdir(hostname, path, sudo=sudo)
            for f in sorted(logs):
                if start is not None or end is not None:
                    tm = self.get_timestamps(f, hostname, num=1, sudo=sudo)
                    if not tm:
                        continue
                    d1 = time.strftime("%Y%m%d", time.localtime(tm[0]))
                    if start is not None:
                        d2 = time.strftime("%Y%m%d", time.localtime(start))
                        if d1 < d2:
                            continue
                    if end is not None:
                        d2 = time.strftime("%Y%m%d", time.localtime(end))
                        if d1 > d2:
                            continue
                paths.append(f)
        elif self.du.isfile(hostname, path, sudo=sudo):
            paths = [path]

        return paths


class PBSLogAnalyzer(object):
    """
    Utility to analyze the PBS logs
    """
    logger = logging.getLogger(__name__)
    logutils = PBSLogUtils()

    generic_tag = re.compile(tm_re + ".*")
    node_type_tag = re.compile(tm_re + ".*" + "Type 58 request.*")
    queue_type_tag = re.compile(tm_re + ".*" + "Type 20 request.*")
    job_type_tag = re.compile(tm_re + ".*" + "Type 51 request.*")
    job_exit_tag = re.compile(tm_re + ".*" + job_re + ";Exit_status.*")

    def __init__(self, schedlog=None, serverlog=None,
                 momlog=None, acctlog=None, genericlog=None,
                 hostname=None, show_progress=False):

        self.hostname = hostname
        self.schedlog = schedlog
        self.serverlog = serverlog
        self.acctlog = acctlog
        self.momlog = momlog
        self.genericlog = genericlog
        self.show_progress = show_progress

        self._custom_tag = None
        self._custom_freq = None
        self._custom_id = False
        self._re_interval = []
        self._re_group = {}

        self.num_conditional_matches = 0
        self.re_conditional = None
        self.num_conditionals = 0
        self.prev_records = []

        self.info = {}

        self.scheduler = None
        self.server = None
        self.mom = None
        self.accounting = None

        if schedlog:
            self.scheduler = PBSSchedulerLog(schedlog, hostname, show_progress)

        if serverlog:
            self.server = PBSServerLog(serverlog, hostname, show_progress)
        if momlog:
            self.mom = PBSMoMLog(momlog, hostname, show_progress)

        if acctlog:
            self.accounting = PBSAccountingLog(acctlog, hostname,
                                               show_progress)

    def set_custom_match(self, pattern, frequency=None):
        """
        Set the custome matching

        :param pattern: Matching pattern
        :type pattern: str
        :param frequency: Frequency of match
        :type frequency: int
        """
        self._custom_tag = re.compile(tm_re + ".*" + pattern + ".*")
        self._custom_freq = frequency

    def set_conditional_match(self, conditions):
        """
        Set the conditional match

        :param conditions: Conditions for macthing
        """
        if not isinstance(conditions, list):
            return False
        self.re_conditional = conditions
        self.num_conditionals = len(conditions)
        self.prev_records = ['' for n in range(self.num_conditionals)]
        self.info['matches'] = []

    def analyze_scheduler_log(self, filename=None, start=None, end=None,
                              hostname=None, summarize=True):
        """
        Analyze the scheduler log

        :param filename: Scheduler log file name
        :type filename: str or None
        :param start: Time from which log to be analyzed
        :param end: Time till which log to be analyzed
        :param hostname: Hostname of the machine
        :type hostname: str or None
        :param summarize: Summarize data parsed if True else not
        :type summarize: bool
        """
        if self.scheduler is None:
            self.scheduler = PBSSchedulerLog(filename, hostname=hostname)
        return self.scheduler.analyze(filename, start, end, hostname,
                                      summarize)

    def analyze_server_log(self, filename=None, start=None, end=None,
                           hostname=None, summarize=True):
        """
        Analyze the server log
        """
        if self.server is None:
            self.server = PBSServerLog(filename, hostname=hostname)

        return self.server.analyze(filename, start, end, hostname,
                                   summarize)

    def analyze_accounting_log(self, filename=None, start=None, end=None,
                               hostname=None, summarize=True):
        """
        Analyze the accounting log
        """
        if self.accounting is None:
            self.accounting = PBSAccountingLog(filename, hostname=hostname)

        return self.accounting.analyze(filename, start, end, hostname,
                                       summarize=summarize, sudo=True)

    def analyze_mom_log(self, filename=None, start=None, end=None,
                        hostname=None, summarize=True):
        """
        Analyze the mom log
        """
        if self.mom is None:
            self.mom = PBSMoMLog(filename, hostname=hostname)

        return self.mom.analyze(filename, start, end, hostname, summarize)

    def parse_conditional(self, rec, start, end):
        """
        Match a sequence of regular expressions against multiple
        consecutive lines in a generic log. Calculate the number
        of conditional matching lines.

        Example usage: to find the number of times the scheduler
        stat'ing the server causes the scheduler to miss jobs ending,
        which could possibly indicate a race condition between the
        view of resources assigned to nodes and the actual jobs
        running, one would call this function by setting
        re_conditional to
        ``['Type 20 request received from Scheduler', 'Exit_status']``
        Which can be read as counting the number of times that the
        Type 20 message is preceded by an ``Exit_status`` message
        """
        match = True
        for rc in range(self.num_conditionals):
            if not re.search(self.re_conditional[rc], self.prev_records[rc]):
                match = False
        if match:
            self.num_conditional_matches += 1
            self.info['matches'].extend(self.prev_records)
        for i in range(self.num_conditionals - 1, -1, -1):
            self.prev_records[i] = self.prev_records[i - 1]
        self.prev_records[0] = rec
        return PARSER_OK_CONTINUE

    def parse_custom_tag(self, rec, start, end):
        m = self._custom_tag.match(rec)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            if ((start is None and end is None) or
                    self.logutils.in_range(tm, start, end)):
                self._re_interval.append(tm)
                for k, v in m.groupdict().items():
                    if k in self._re_group:
                        self._re_group[k].append(v)
                    else:
                        self._re_group[k] = [v]
            elif end is not None and tm > end:
                return PARSER_OK_STOP

        return PARSER_OK_CONTINUE

    def parse_block(self, rec, start, end):
        m = self.generic_tag.match(rec)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            if ((start is None and end is None) or
                    self.logutils.in_range(tm, start, end)):
                print(rec, end=' ')

    def comp_analyze(self, rec, start, end):
        if self.re_conditional is not None:
            return self.parse_conditional(rec, start, end)
        elif self._custom_tag is not None:
            return self.parse_custom_tag(rec, start, end)
        elif start is not None or end is not None:
            return self.parse_block(rec, start, end)

    def analyze(self, path=None, start=None, end=None, hostname=None,
                summarize=True, sudo=False):
        """
        Parse any log file. This method is not ``context-specific``
        to each log file type.

        :param path: name of ``file/dir`` to parse
        :type path: str or None
        :param start: optional record time at which to start analyzing
        :param end: optional record time after which to stop analyzing
        :param hostname: name of host on which to operate. Defaults to
                         localhost
        :type hostname: str or None
        :param summarize: if True, summarize data parsed. Defaults to
                          True.
        :type summarize: bool
        :param sudo: If True, access log file(s) as privileged user.
        :type sudo: bool
        """
        if hostname is None and self.hostname is not None:
            hostname = self.hostname

        for f in self.logutils.get_log_files(hostname, path, start, end,
                                             sudo=sudo):
            self._log_parser(f, start, end, hostname, sudo=sudo)

        if summarize:
            return self.summary()

    def _log_parser(self, filename, start, end, hostname=None, sudo=False):
        num_records = self.logutils.get_num_lines(filename, hostname,
                                                  sudo=sudo)
        if filename is not None:
            records = self.logutils.open_log(filename, hostname, sudo=sudo,
                                             start=start,
                                             num_records=num_records)
        else:
            return None

        if records is None:
            return None

        num_line = 0
        last_rec = None
        if self.show_progress:
            perc_range = list(range(10, 110, 10))
            perc_records = [num_records * x / 100 for x in perc_range]
            sys.stderr.write('Parsing ' + filename + ': |0%')
            sys.stderr.flush()

        for rec in records:
            rec = rec.decode("utf-8")
            num_line += 1
            if self.show_progress and (num_line > perc_records[0]):
                sys.stderr.write('-' + str(perc_range[0]) + '%')
                sys.stderr.flush()
                perc_range.remove(perc_range[0])
                perc_records.remove(perc_records[0])
            last_rec = rec
            rv = self.comp_analyze(rec, start, end)
            if (rv in (PARSER_OK_STOP, PARSER_ERROR_STOP) or
                    (self.show_progress and len(perc_records) == 0)):
                break
        if self.show_progress:
            sys.stderr.write('-100%|\n')
            sys.stderr.flush()
        records.close()

        if last_rec is not None:
            self.epilogue(last_rec)

    def analyze_logs(self, schedlog=None, serverlog=None, momlog=None,
                     acctlog=None, genericlog=None, start=None, end=None,
                     hostname=None, showjob=False):
        """
        Analyze logs
        """
        if hostname is None and self.hostname is not None:
            hostname = self.hostname

        if schedlog is None and self.schedlog is not None:
            schedlog = self.schedlog
        if serverlog is None and self.serverlog is not None:
            serverlog = self.serverlog
        if momlog is None and self.momlog is not None:
            momlog = self.momlog
        if acctlog is None and self.acctlog is not None:
            acctlog = self.acctlog
        if genericlog is None and self.genericlog is not None:
            genericlog = self.genericlog

        cycles = None
        sjr = {}

        if schedlog:
            self.analyze_scheduler_log(schedlog, start, end, hostname,
                                       summarize=False)
            cycles = self.scheduler.cycles

        if serverlog:
            self.analyze_server_log(serverlog, start, end, hostname,
                                    summarize=False)
            sjr = self.server.server_job_run

        if momlog:
            self.analyze_mom_log(momlog, start, end, hostname,
                                 summarize=False)

        if acctlog:
            self.analyze_accounting_log(acctlog, start, end, hostname,
                                        summarize=False)

        if genericlog:
            self.analyze(genericlog, start, end, hostname, sudo=True,
                         summarize=False)

        if cycles is not None and len(sjr.keys()) != 0:
            for cycle in cycles:
                for jid, tm in cycle.sched_job_run.items():
                    # skip job arrays: scheduler runs a subjob
                    # but we don't keep track of which Considering job to run
                    # message it is associated with because the consider
                    # message doesn't show the subjob
                    if '[' in jid:
                        continue
                    if jid in sjr:
                        for tm in sjr[jid]:
                            if tm > cycle.start and tm < cycle.end:
                                cycle.inschedduration[jid] = \
                                    tm - cycle.consider[jid]

        return self.summary(showjob)

    def epilogue(self, line):
        pass

    def summary(self, showjob=False, writer=None):

        info = {}

        if self._custom_tag is not None:
            self.info = self.logutils.process_intervals(self._re_interval,
                                                        self._re_group,
                                                        self._custom_freq)
            return self.info

        if self.re_conditional is not None:
            self.info['num_conditional_matches'] = self.num_conditional_matches
            return self.info

        if self.scheduler is not None:
            info['scheduler'] = self.scheduler.summary(self.scheduler.cycles,
                                                       showjob)
        if self.server is not None:
            info['server'] = self.server.summary()

        if self.accounting is not None:
            info['accounting'] = self.accounting.summary()

        if self.mom is not None:
            info['mom'] = self.mom.summary()

        return info


class PBSServerLog(PBSLogAnalyzer):
    """
    :param filename: Server log filename
    :type filename: str or None
    :param hostname: Hostname of the machine
    :type hostname: str or None
    """
    tm_tag = re.compile(tm_re)
    server_run_tag = re.compile(tm_re + ".*" + job_re + ".*;Job Run at.*")
    server_nodeup_tag = re.compile(tm_re + ".*Node;.*;node up.*")
    server_enquejob_tag = re.compile(tm_re + ".*" + job_re +
                                     ".*enqueuing into.*state Q .*")
    server_endjob_tag = re.compile(tm_re + ".*" + job_re +
                                   ".*;Exit_status.*")

    def __init__(self, filename=None, hostname=None, show_progress=False):

        self.server_job_queued = {}
        self.server_job_run = {}
        self.server_job_end = {}
        self.records = None
        self.nodeup = []
        self.enquejob = []
        self.record_tm = []
        self.jobsrun = []
        self.jobsend = []
        self.wait_time = []
        self.run_time = []

        self.hostname = hostname

        self.info = {}
        self.version = []

        self.filename = filename
        self.show_progress = show_progress

    def parse_runjob(self, line):
        """
        Parse server log for run job records.
        For each record keep track of the job id, and time in a
        dedicated array
        """
        m = self.server_run_tag.match(line)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.jobsrun.append(tm)
            jobid = str(m.group('jobid'))
            if jobid in self.server_job_run:
                self.server_job_run[jobid].append(tm)
            else:
                self.server_job_run[jobid] = [tm]
            if jobid in self.server_job_queued:
                self.wait_time.append(tm - self.server_job_queued[jobid])

    def parse_endjob(self, line):
        """
        Parse server log for run job records.
        For each record keep track of the job id, and time in a
        dedicated array
        """
        m = self.server_endjob_tag.match(line)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.jobsend.append(tm)
            jobid = str(m.group('jobid'))
            if jobid in self.server_job_end:
                self.server_job_end[jobid].append(tm)
            else:
                self.server_job_end[jobid] = [tm]
            if jobid in self.server_job_run:
                self.run_time.append(tm - self.server_job_run[jobid][-1:][0])

    def parse_nodeup(self, line):
        """
        Parse server log for nodes that are up
        """
        m = self.server_nodeup_tag.match(line)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.nodeup.append(tm)

    def parse_enquejob(self, line):
        """
        Parse server log for enqued jobs
        """
        m = self.server_enquejob_tag.match(line)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.enquejob.append(tm)
            jobid = str(m.group('jobid'))
            self.server_job_queued[jobid] = tm

    def comp_analyze(self, rec, start=None, end=None):
        m = self.tm_tag.match(rec)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.record_tm.append(tm)
            if not self.logutils.in_range(tm, start, end):
                if end and tm > end:
                    return PARSER_OK_STOP
                return PARSER_OK_CONTINUE

        if 'pbs_version=' in rec:
            version = rec.split('pbs_version=')[1].strip()
            if version not in self.version:
                self.version.append(version)
        self.parse_enquejob(rec)
        self.parse_nodeup(rec)
        self.parse_runjob(rec)
        self.parse_endjob(rec)

        return PARSER_OK_CONTINUE

    def summary(self):
        self.info[JSR] = self.logutils.get_rate(self.enquejob)
        self.info[NJE] = len(self.server_job_end.keys())
        self.info[NJQ] = len(self.enquejob)
        self.info[NUR] = self.logutils.get_rate(self.nodeup)
        self.info[JRR] = self.logutils.get_rate(self.jobsrun)
        self.info[JER] = self.logutils.get_rate(self.jobsend)
        if len(self.server_job_end) > 0:
            tjr = self.jobsend[-1] - self.enquejob[0]
            self.info[JTR] = str(len(self.server_job_end) / tjr) + '/s'
        if len(self.wait_time) > 0:
            wt = sorted(self.wait_time)
            wta = float(sum(self.wait_time)) / len(self.wait_time)
            self.info[JWTm] = self.logutils._duration(min(wt))
            self.info[JWTM] = self.logutils._duration(max(wt))
            self.info[JWTA] = self.logutils._duration(wta)
            self.info[JWT25] = self.logutils._duration(
                self.logutils.percentile(wt, .25))
            self.info[JWT50] = self.logutils._duration(
                self.logutils.percentile(wt, .5))
            self.info[JWT75] = self.logutils._duration(
                self.logutils.percentile(wt, .75))
        njr = 0
        for v in self.server_job_run.values():
            njr += len(v)
        self.info[NJR] = njr
        self.info[VER] = ",".join(self.version)

        if len(self.run_time) > 0:
            rt = sorted(self.run_time)
            self.info[JRTm] = self.logutils._duration(min(rt))
            self.info[JRT25] = self.logutils._duration(
                self.logutils.percentile(rt, 0.25))
            self.info[JRT50] = self.logutils._duration(
                self.logutils.percentile(rt, 0.50))
            self.info[JRTA] = self.logutils._duration(
                str(sum(rt) / len(rt)))
            self.info[JRT75] = self.logutils._duration(
                self.logutils.percentile(rt, 0.75))
            self.info[JRTM] = self.logutils._duration(max(rt))
        return self.info


class JobEstimatedStartTimeInfo(object):
    """
    Information regarding Job estimated start time
    """

    def __init__(self, jobid):
        self.jobid = jobid
        self.started_at = None
        self.estimated_at = []
        self.num_drifts = 0
        self.num_estimates = 0
        self.drift_time = 0

    def add_estimate(self, tm):
        """
        Add a job's new estimated start time
        If the new estimate is now later than any preivous one, we
        add that difference to the drift time. If the new drift time
        is pulled earlier it is not added to the drift time.

        drift time is a measure of ``"negative perception"`` that
        comes along a job being estimated to run at a later date than
        earlier ``"advertised"``.
        """
        if self.estimated_at:
            prev_tm = self.estimated_at[len(self.estimated_at) - 1]
            if tm > prev_tm:
                self.num_drifts += 1
                self.drift_time += tm - prev_tm

        self.estimated_at.append(tm)
        self.num_estimates += 1

    def __repr__(self):
        estimated_at_str = [str(t) for t in self.estimated_at]
        return " ".join([str(self.jobid), 'started: ', str(self.started_at),
                         'estimated: ', ",".join(estimated_at_str)])

    def __str__(self):
        return self.__repr__()


class PBSSchedulerLog(PBSLogAnalyzer):

    tm_tag = re.compile(tm_re)
    startcycle_tag = re.compile(tm_re + ".*Starting Scheduling.*")
    endcycle_tag = re.compile(tm_re + ".*Leaving [(the )]*[sS]cheduling.*")
    alarm_tag = re.compile(tm_re + ".*alarm.*")
    considering_job_tag = re.compile(tm_re + ".*" + job_re +
                                     ".*;Considering job to run.*")
    sched_job_run_tag = re.compile(tm_re + ".*" + job_re + ".*;Job run.*")
    estimated_tag = re.compile(tm_re + ".*" + job_re +
                               ".*;Job is a top job and will run at "
                               "(?P<est_tm>.*)")
    run_failure_tag = re.compile(tm_re + ".*" + fail_re + ".*;Failed to run.*")
    calendarjob_tag = re.compile(
        tm_re +
        ".*" +
        job_re +
        ".*;Job is a top job.*")
    preempt_failure_tag = re.compile(tm_re + ".*;Job failed to be preempted.*")
    preempt_tag = re.compile(tm_re + ".*" + job_re + ".*;Job preempted.*")
    record_tag = re.compile(tm_re + ".*")

    def __init__(self, filename=None, hostname=None, show_progress=False):

        self.filename = filename
        self.hostname = hostname
        self.show_progress = show_progress

        self.record_tm = []
        self.version = []

        self.cycle = None
        self.cycles = []

        self.estimated_jobs = {}
        self.estimated_parsing_enabled = False
        self.parse_estimated_only = False

        self.info = {}
        self.summary_info = {}

    def _parse_line(self, line):
        """
        Parse scheduling cycle Starting, Leaving, and alarm records
        From each record, keep track of the record time in a
        dedicated array
        """
        m = self.startcycle_tag.match(line)

        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            # if cycle was interrupted assume previous cycle ended now
            if self.cycle is not None and self.cycle.end == -1:
                self.cycle.end = tm
            self.cycle = PBSCycleInfo()
            self.cycles.append(self.cycle)
            self.cycle.start = tm
            self.cycle.end = -1
            return PARSER_OK_CONTINUE

        m = self.endcycle_tag.match(line)
        if m is not None and self.cycle is not None:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.cycle.end = tm
            self.cycle.duration = tm - self.cycle.start
            if (self.cycle.lastjob is not None and
                    self.cycle.lastjob not in self.cycle.sched_job_run and
                    self.cycle.lastjob not in self.cycle.calendared_jobs):
                self.cycle.cantrunduration[self.cycle.lastjob] = (
                    tm - self.cycle.consider[self.cycle.lastjob])
            return PARSER_OK_CONTINUE

        m = self.alarm_tag.match(line)
        if m is not None and self.cycle is not None:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.cycle.end = tm
            return PARSER_OK_CONTINUE

        m = self.considering_job_tag.match(line)
        if m is not None and self.cycle is not None:
            self.cycle.num_considered += 1
            jid = str(m.group('jobid'))
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.cycle.consider[jid] = tm
            self.cycle.political_order.append(jid)
            if (self.cycle.lastjob is not None and
                    self.cycle.lastjob not in self.cycle.sched_job_run and
                    self.cycle.lastjob not in self.cycle.calendared_jobs):
                self.cycle.cantrunduration[self.cycle.lastjob] = (
                    tm - self.cycle.consider[self.cycle.lastjob])
            self.cycle.lastjob = jid
            if self.cycle.queryduration == 0:
                self.cycle.queryduration = tm - self.cycle.start
            return PARSER_OK_CONTINUE

        m = self.sched_job_run_tag.match(line)
        if m is not None and self.cycle is not None:
            jid = str(m.group('jobid'))
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.cycle.sched_job_run[jid] = tm
            # job arrays require special handling because the considering
            # job to run message does not have the subjob index but only []
            if '[' in jid:
                subjid = jid
                if subjid not in self.cycle.consider:
                    jid = jid.split('[')[0] + '[]'
                    self.cycle.consider[subjid] = self.cycle.consider[jid]
                self.cycle.runduration[subjid] = tm - self.cycle.consider[jid]
            # job rerun due to preemption failure aren't considered, skip
            elif jid in self.cycle.consider:
                self.cycle.runduration[jid] = tm - self.cycle.consider[jid]
            return PARSER_OK_CONTINUE

        m = self.run_failure_tag.match(line)
        if m is not None:
            if self.cycle is not None:
                jid = str(m.group('jobid'))
                tm = self.logutils.convert_date_time(m.group('datetime'))
                self.cycle.run_failure[jid] = tm
            return PARSER_OK_CONTINUE

        m = self.preempt_failure_tag.match(line)
        if m is not None:
            if self.cycle is not None:
                self.cycle.num_preempt_failure += 1
            return PARSER_OK_CONTINUE

        m = self.preempt_tag.match(line)
        if m is not None:
            if self.cycle is not None:
                jid = str(m.group('jobid'))
                if self.cycle.lastjob in self.cycle.preempted_jobs:
                    self.cycle.preempted_jobs[self.cycle.lastjob].append(jid)
                else:
                    self.cycle.preempted_jobs[self.cycle.lastjob] = [jid]
                self.cycle.num_preempted += 1
            return PARSER_OK_CONTINUE

        m = self.calendarjob_tag.match(line)
        if m is not None:
            if self.cycle is not None:
                jid = str(m.group('jobid'))
                tm = self.logutils.convert_date_time(m.group('datetime'))
                self.cycle.calendared_jobs[jid] = tm
                if jid in self.cycle.consider:
                    self.cycle.calendarduration[jid] = \
                        (tm - self.cycle.consider[jid])
                elif '[' in jid:
                    arrjid = re.sub("(\[\d+\])", '[]', jid)
                    if arrjid in self.cycle.consider:
                        self.cycle.consider[jid] = self.cycle.consider[arrjid]
                        self.cycle.calendarduration[jid] = \
                            (tm - self.cycle.consider[arrjid])
            return PARSER_OK_CONTINUE

    def get_cycles(self, start=None, end=None):
        """
        Get the scheduler cycles

        :param start: Start time
        :param end: End time
        :returns: Scheduling cycles
        """
        if start is None and end is None:
            return self.cycles

        cycles = []
        if end is None:
            end = time.time()
        for c in self.cycles:
            if c.start >= start and c.end < end:
                cycles.append(c)
        return cycles

    def comp_analyze(self, rec, start, end):
        if self.estimated_parsing_enabled:
            rv = self.estimated_info_parsing(rec)
            if self.parse_estimated_only:
                return rv
        return self.scheduler_parsing(rec, start, end)

    def scheduler_parsing(self, rec, start, end):
        m = self.tm_tag.match(rec)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            self.record_tm.append(tm)
            if self.logutils.in_range(tm, start, end):
                rv = self._parse_line(rec)
                if rv in (PARSER_OK_STOP, PARSER_ERROR_STOP):
                    return rv
            if 'pbs_version=' in rec:
                version = rec.split('pbs_version=')[1].strip()
                if version not in self.version:
                    self.version.append(version)
        elif end is not None and tm > end:
            PARSER_OK_STOP

        return PARSER_OK_CONTINUE

    def estimated_info_parsing(self, line):
        """
        Parse Estimated start time information for a job
        """
        m = self.sched_job_run_tag.match(line)
        if m is not None:
            jid = str(m.group('jobid'))
            tm = self.logutils.convert_date_time(m.group('datetime'))
            if jid in self.estimated_jobs:
                self.estimated_jobs[jid].started_at = tm
            else:
                ej = JobEstimatedStartTimeInfo(jid)
                ej.started_at = tm
                self.estimated_jobs[jid] = ej

        m = self.estimated_tag.match(line)
        if m is not None:
            jid = str(m.group('jobid'))
            try:
                tm = self.logutils.convert_date_time(m.group('est_tm'),
                                                     "%a %b %d %H:%M:%S %Y")
            except Exception:
                logging.error('error converting time: ' +
                              str(m.group('est_tm')))
                return PARSER_ERROR_STOP

            if jid in self.estimated_jobs:
                self.estimated_jobs[jid].add_estimate(tm)
            else:
                ej = JobEstimatedStartTimeInfo(jid)
                ej.add_estimate(tm)
                self.estimated_jobs[jid] = ej

        return PARSER_OK_CONTINUE

    def epilogue(self, line):
        # if log ends in the middle of a cycle there is no 'Leaving cycle'
        # message, in this case the last cycle duration is computed as
        # from start to the last record in the log file
        if self.cycle is not None and self.cycle.end <= 0:
            m = self.record_tag.match(line)
            if m:
                self.cycle.end = self.logutils.convert_date_time(
                    m.group('datetime'))

    def summarize_estimated_analysis(self, estimated_jobs=None):
        """
        Summarize estimated job analysis
        """
        if estimated_jobs is None and self.estimated_jobs is not None:
            estimated_jobs = self.estimated_jobs

        einfo = {EJ: []}
        sub15mn = 0
        sub1hr = 0
        sub3hr = 0
        sup3hr = 0
        total_drifters = 0
        total_nondrifters = 0
        drift_times = []
        for e in estimated_jobs.values():
            info = {}
            if len(e.estimated_at) > 0:
                info[JID] = e.jobid
                e_sorted = sorted(e.estimated_at)
                info[Eat] = e.estimated_at
                if e.started_at is not None:
                    info[JST] = e.started_at
                    e_diff = e_sorted[len(e_sorted) - 1] - e_sorted[0]
                    e_accuracy = (e.started_at -
                                  e.estimated_at[len(e.estimated_at) - 1])
                    info[ESTR] = e_diff
                    info[ESTA] = e_accuracy

                info[NEST] = e.num_estimates
                info[ND] = e.num_drifts
                info[JDD] = e.drift_time
                drift_times.append(e.drift_time)

                if e.drift_time > 0:
                    total_drifters += 1
                    if e.drift_time < 15 * 60:
                        sub15mn += 1
                    elif e.drift_time < 3600:
                        sub1hr += 1
                    elif e.drift_time < 3 * 3600:
                        sub3hr += 1
                    else:
                        sup3hr += 1
                else:
                    total_nondrifters += 1
                einfo[EJ].append(info)

        info = {}
        info[Ds15mn] = sub15mn
        info[Ds1hr] = sub1hr
        info[Ds3hr] = sub3hr
        info[Do3hr] = sup3hr
        info[NJD] = total_drifters
        info[NJND] = total_nondrifters
        if drift_times:
            info[DDm] = min(drift_times)
            info[DDM] = max(drift_times)
            info[DDA] = (sum(drift_times) / len(drift_times))
            info[DD50] = sorted(drift_times)[len(drift_times) / 2]
        einfo[ESTS] = info

        return einfo

    def summary(self, cycles=None, showjobs=False):
        """
        Scheduler log summary
        """
        if self.estimated_parsing_enabled:
            self.info[EST] = self.summarize_estimated_analysis()
            if self.parse_estimated_only:
                return self.info

        if cycles is None and self.cycles is not None:
            cycles = self.cycles

        num_cycle = 0
        run = 0
        failed = 0
        total_considered = 0
        run_tm = []
        cycle_duration = []
        min_duration = None
        max_duration = None
        mint = maxt = None
        calendarduration = 0
        schedsolvertime = 0

        for c in cycles:
            c.summary(showjobs)
            self.info[num_cycle] = c.info
            run += len(c.sched_job_run.keys())
            run_tm.extend(list(c.sched_job_run.values()))
            failed += len(c.run_failure.keys())
            total_considered += c.num_considered

            if max_duration is None or c.duration > max_duration:
                max_duration = c.duration
                maxt = time.strftime("%Y-%m-%d %H:%M:%S",
                                     time.localtime(c.start))

            if min_duration is None or c.duration < min_duration:
                min_duration = c.duration
                mint = time.strftime("%Y-%m-%d %H:%M:%S",
                                     time.localtime(c.start))

            cycle_duration.append(c.duration)
            num_cycle += 1
            calendarduration += sum(c.calendarduration.values())
            schedsolvertime += c.scheduler_solver_time

        run_rate = self.logutils.get_rate(sorted(run_tm))

        sorted_cd = sorted(cycle_duration)

        self.summary_info[NC] = len(cycles)
        self.summary_info[NJR] = run
        self.summary_info[NJFR] = failed
        self.summary_info[JRR] = run_rate
        self.summary_info[NJC] = total_considered
        self.summary_info[mCD] = self.logutils._duration(min_duration)
        self.summary_info[MCD] = self.logutils._duration(max_duration)
        self.summary_info[CD25] = self.logutils._duration(
            self.logutils.percentile(sorted_cd, .25))
        if len(sorted_cd) > 0:
            self.summary_info[CDA] = self.logutils._duration(
                sum(sorted_cd) / len(sorted_cd))
        self.summary_info[CD50] = self.logutils._duration(
            self.logutils.percentile(sorted_cd, .5))
        self.summary_info[CD75] = self.logutils._duration(
            self.logutils.percentile(sorted_cd, .75))

        if mint is not None:
            self.summary_info[mCT] = mint
        if maxt is not None:
            self.summary_info[MCT] = maxt
        self.summary_info[DUR] = self.logutils._duration(sum(cycle_duration))
        self.summary_info[TTC] = self.logutils._duration(calendarduration)
        self.summary_info[SST] = self.logutils._duration(schedsolvertime)
        self.summary_info[VER] = ",".join(self.version)

        self.info['summary'] = dict(self.summary_info.items())
        return self.info


class PBSCycleInfo(object):

    def __init__(self):

        self.info = {}

        """
        Time between end and start of a cycle, which may be on alarm,
        or signal, not only Leaving - Starting
        """
        self.duration = 0
        " Time of a Starting scheduling cycle message "
        self.start = 0
        " Time of a Leaving scheduling cycle message "
        self.end = 0
        " Time at which Considering job to run message "
        self.consider = {}
        " Number of jobs considered "
        self.num_considered = 0
        " Time at which job run message in scheduler. This includes time to "
        " start the job by the server "
        self.sched_job_run = {}
        """
        number of jobs added to the calendar, i.e.,
        number of backfilling jobs
        """
        self.calendared_jobs = {}
        " Time between Considering job to run to Job run message "
        self.runduration = {}
        " Time to determine that job couldn't run "
        self.cantrunduration = {}
        " List of jobs preempted in order to run high priority job"
        self.preempted_jobs = {}
        """
        Time between considering job to run to server logging
        'Job Run at request...
        """
        self.inschedduration = {}
        " Total time spent in scheduler solver, insched + cantrun + calendar"
        self.scheduler_solver_time = 0
        " Error 15XXX in the sched log corresponds to a failure to run"
        self.run_failure = {}
        " Job failed to be preempted"
        self.num_preempt_failure = 0
        " Job preempted by "
        self.num_preempted = 0
        " Time between start of cycle and first job considered to run "
        self.queryduration = 0
        " The order in which jobs are considered "
        self.political_order = []
        " Time to calendar "
        self.calendarduration = {}

        self.lastjob = None

    def summary(self, showjobs=False):
        """
        Summary regarding cycle
        """
        self.info[CST] = time.strftime(
            "%Y-%m-%d %H:%M:%S", time.localtime(self.start))
        self.info[CD] = PBSLogUtils._duration(self.end - self.start)
        self.info[QD] = PBSLogUtils._duration(self.queryduration)
        # number of jobs considered may be different than length of
        # the consider dictionary due to job arrays being considered once
        # per subjob using the parent array job id
        self.info[NJC] = self.num_considered
        self.info[NJR] = len(self.sched_job_run.keys())
        self.info[NJFR] = len(self.run_failure)
        self.scheduler_solver_time = (sum(self.inschedduration.values()) +
                                      sum(self.cantrunduration.values()) +
                                      sum(self.calendarduration.values()))
        self.info[SST] = self.scheduler_solver_time
        self.info[NJCAL] = len(self.calendared_jobs.keys())
        self.info[NJFP] = self.num_preempt_failure
        self.info[NJP] = self.num_preempted
        self.info[TTC] = sum(self.calendarduration.values())

        if showjobs:
            for j in self.consider.keys():
                s = {JID: j}
                if j in self.runduration:
                    s[T2R] = self.runduration[j]
                if j in self.cantrunduration:
                    s[T2D] = self.cantrunduration[j]
                if j in self.inschedduration:
                    s[TiS] = self.inschedduration[j]
                if j in self.calendarduration:
                    s[TTC] = self.calendarduration[j]
                if 'jobs' in self.info:
                    self.info['jobs'].append(s)
                else:
                    self.info['jobs'] = [s]


class PBSMoMLog(PBSLogAnalyzer):

    """
    Container and Parser of a PBS ``MoM`` log
    """
    tm_tag = re.compile(tm_re)
    mom_run_tag = re.compile(tm_re + ".*" + job_re + ".*;Started, pid.*")
    mom_end_tag = re.compile(tm_re + ".*" + job_re +
                             ".*;delete job request received.*")
    mom_enquejob_tag = re.compile(tm_re + ".*;Type 5 .*")

    def __init__(self, filename=None, hostname=None, show_progress=False):

        self.filename = filename
        self.hostname = hostname
        self.show_progress = show_progress

        self.start = []
        self.end = []
        self.queued = []

        self.info = {}
        self.version = []

    def comp_analyze(self, rec, start, end):
        m = self.mom_run_tag.match(rec)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            if ((start is None and end is None) or
                    self.logutils.in_range(tm, start, end)):
                self.start.append(tm)
                return PARSER_OK_CONTINUE
            elif end is not None and tm > end:
                return PARSER_OK_STOP

        m = self.mom_end_tag.match(rec)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            if ((start is None and end is None) or
                    self.logutils.in_range(tm, start, end)):
                self.end.append(tm)
                return PARSER_OK_CONTINUE
            elif end is not None and tm > end:
                return PARSER_OK_STOP

        m = self.mom_enquejob_tag.match(rec)
        if m:
            tm = self.logutils.convert_date_time(m.group('datetime'))
            if ((start is None and end is None) or
                    self.logutils.in_range(tm, start, end)):
                self.queued.append(tm)
                return PARSER_OK_CONTINUE
            elif end is not None and tm > end:
                return PARSER_OK_STOP

        if 'pbs_version=' in rec:
            version = rec.split('pbs_version=')[1].strip()
            if version not in self.version:
                self.version.append(version)

        return PARSER_OK_CONTINUE

    def summary(self):
        """
        Mom log summary
        """
        run_rate = self.logutils.get_rate(self.start)
        queue_rate = self.logutils.get_rate(self.queued)
        end_rate = self.logutils.get_rate(self.end)

        self.info[NJQ] = len(self.queued)
        self.info[NJR] = len(self.start)
        self.info[NJE] = len(self.end)
        self.info[JRR] = run_rate
        self.info[JSR] = queue_rate
        self.info[JER] = end_rate
        self.info[VER] = ",".join(self.version)

        return self.info


class PBSAccountingLog(PBSLogAnalyzer):

    """
    Container and Parser of a PBS accounting log
    """

    tm_tag = re.compile(tm_re)

    record_tag = re.compile(r"""
                        (?P<date>\d\d/\d\d/\d{4,4})[\s]+
                        (?P<time>\d\d:\d\d:\d\d);
                        (?P<type>[A-Z]);
                        (?P<id>[0-9\[\]].*);
                        (?P<msg>.*)
                        """, re.VERBOSE)

    S_sub_record_tag = re.compile(r"""
                        .*user=(?P<user>[\w\d]+)[\s]+
                        .*qtime=(?P<qtime>[0-9]+)[\s]+
                        .*start=(?P<start>[0-9]+)[\s]+
                        .*exec_host=(?P<exechost>[\[\],\-\=\/\.\w/*\d\+]+)[\s]+
                        .*Resource_List.ncpus=(?P<ncpus>[0-9]+)[\s]+
                        .*
                        """, re.VERBOSE)

    E_sub_record_tag = re.compile(r"""
                        .*user=(?P<user>[\w\d]+)[\s]+
                        .*qtime=(?P<qtime>[0-9]+)[\s]+
                        .*start=(?P<start>[0-9]+)[\s]+
                        .*exec_host=(?P<exechost>[\[\],\-\=\/\.\w/*\d\+]+)[\s]+
                        .*Resource_List.ncpus=(?P<ncpus>[0-9]+)[\s]+
                        .*resources_used.walltime=(?P<walltime>[0-9:]+)
                        .*
                        """, re.VERBOSE)

    __E_sub_record_tag = re.compile(r"""
                        .*user=(?P<user>[\w\d]+)[\s]+
                        .*qtime=(?P<qtime>[0-9]+)[\s]+
                        .*start=(?P<start>[0-9]+)[\s]+
                        .*exec_host=(?P<exechost>[\[\],\-\=\/\.\w/*\d\+]+)[\s]+
                        .*Resource_List.ncpus=(?P<ncpus>[0-9]+)[\s]+
                        .*resources_used.walltime=(?P<walltime>[0-9:]+)
                        .*
                        """, re.VERBOSE)

    sub_record_tag = re.compile(r"""
                .*qtime=(?P<qtime>[0-9]+)[\s]+
                .*start=(?P<start>[0-9]+)[\s]+
                .*exec_host=(?P<exechost>[\[\],\-\=\/\.\w/*\d\+]+)[\s]+
                .*exec_vnode=(?P<execvnode>[\(\)\[\],:\-\=\/\.\w/*\d\+]+)[\s]+
                .*Resource_List.ncpus=(?P<ncpus>[\d]+)[\s]+
                .*
                """, re.VERBOSE)

    logger = logging.getLogger(__name__)
    utils = BatchUtils()

    def __init__(self, filename=None, hostname=None, show_progress=False):

        self.filename = filename
        self.hostname = hostname
        self.show_progress = show_progress

        self.record_tm = []

        self.entries = {}
        self.queue = []
        self.start = []
        self.end = []
        self.wait_time = []
        self.run_time = []
        self.job_node_size = []
        self.job_cpu_size = []
        self.used_cph = 0
        self.nodes_cph = 0
        self.used_nph = 0
        self.jobs_started = []
        self.jobs_ended = []
        self.users = {}
        self.tmp_wait_time = {}

        self.duration = 0

        self.utilization_parsing = False
        self.running_jobs_parsing = False
        self.job_info_parsing = False
        self.accounting_workload_parsing = False

        self._total_ncpus = 0
        self._num_nodes = 0
        self._running_jobids = []
        self._server = None

        self.running_jobs = {}
        self.job_start = {}
        self.job_end = {}
        self.job_nodes = {}
        self.job_cpus = {}
        self.job_rectypes = {}

        self.job_attrs = {}
        self.parser_errors = 0

        self.info = {}

    def enable_running_jobs_parsing(self):
        """
        Enable parsing for running jobs
        """
        self.running_jobs_parsing = True

    def enable_utilization_parsing(self, hostname=None, nodesfile=None,
                                   jobsfile=None):
        """
        Enable utilization parsing

        :param hostname: Hostname of the machine
        :type hostname: str or None
        :param nodesfile: optional file containing output of
                          pbsnodes -av
        :type nodesfile: str or None
        :param jobsfile: optional file containing output of
                         qstat -f
        :type jobsfile: str or None
        """
        self.utilization_parsing = True
        self.process_nodes_data(hostname, nodesfile, jobsfile)

    def enable_job_info_parsing(self):
        """
        Enable job information parsing
        """
        self.job_info_res = {}
        self.job_info_parsing = True

    def enable_accounting_workload_parsing(self):
        """
        Enable accounting workload parsing
        """
        self.accounting_workload_parsing = True

    def process_nodes_data(self, hostname=None, nodesfile=None, jobsfile=None):
        """
        Get job and node information by stat'ing and parsing node
        data from the server.
        Compute the number of nodes and populate a list of running
        job ids on those nodes.

        :param hostname: The host to query
        :type hostname: str or None
        :param nodesfile: optional file containing output of
                          pbsnodes -av
        :type nodesfile: str or None
        :param jobsfile: optional file containing output of
                         qstat -f
        :type jobsfile: str or None

        The node data is needed to compute counts of nodes and cpus
        The job data is needed to compute the amount of resources
        requested
        """
        if nodesfile or jobsfile:
            self._server = Server(snapmap={NODE: nodesfile, JOB: jobsfile})
        else:
            self._server = Server(hostname)

        ncpus = self._server.counter(NODE, 'resources_available.ncpus',
                                     grandtotal=True, level=logging.DEBUG)

        if 'resources_available.ncpus' in ncpus:
            self._total_ncpus = ncpus['resources_available.ncpus']

        self._num_nodes = len(self._server.status(NODE))

        jobs = self._server.status(NODE, 'jobs')
        running_jobids = []
        for cur_job in jobs:
            if 'jobs' not in cur_job:
                continue
            job = cur_job['jobs']
            jlist = job.split(',')
            for j in jlist:
                running_jobids.append(j.split('/')[0].strip())
        self._running_jobids = list(set(running_jobids))

    def comp_analyze(self, rec, start, end, **kwargs):
        if self.job_info_parsing:
            return self.job_info(rec)
        else:
            return self.accounting_parsing(rec, start, end)

    def accounting_parsing(self, rec, start, end):
        """
        Parsing accounting log
        """
        r = self.record_tag.match(rec.decode("utf-8"))
        if not r:
            return PARSER_ERROR_CONTINUE

        tm = self.logutils.convert_date_time(r.group('date') +
                                             ' ' + r.group('time'))
        if ((start is None and end is None) or
                self.logutils.in_range(tm, start, end)):
            self.record_tm.append(tm)
            rec_type = r.group('type')
            jobid = r.group('id')

            if not self.accounting_workload_parsing and rec_type == 'S':
                # Precompute metrics about the S record just in case
                # it does not have an E record. The differences are
                # resolved after all records are processed
                if jobid in self._running_jobids:
                    self._running_jobids.remove(jobid)
                m = self.S_sub_record_tag.match(r.group('msg'))
                if m:
                    self.users[jobid] = m.group('user')
                    qtime = int(m.group('qtime'))
                    starttime = int(m.group('start'))
                    ncpus = int(m.group('ncpus'))
                    self.job_cpus[jobid] = ncpus

                    if starttime != 0 and qtime != 0:
                        self.tmp_wait_time[jobid] = starttime - qtime
                        self.job_start[jobid] = starttime
                    ehost = m.group('exechost')
                    self.job_nodes[jobid] = ResourceResv.get_hosts(ehost)
            elif rec_type == 'E':
                if self.accounting_workload_parsing:
                    try:
                        msg = r.group('msg').split()
                        attrs = dict([l.split('=', 1) for l in msg])
                    except Exception:
                        self.parser_errors += 1
                        return PARSER_OK_CONTINUE
                    for k in attrs.keys():
                        attrs[k] = PbsAttribute.decode_value(attrs[k])
                    running_time = (int(attrs['end']) - int(attrs['start']))
                    attrs['running_time'] = str(running_time)
                    attrs['schedselect'] = attrs['Resource_List.select']
                    if 'euser' not in attrs:
                        attrs['euser'] = 'unknown_user'

                    attrs['id'] = r.group('id')
                    self.job_attrs[r.group('id')] = attrs

                m = self.E_sub_record_tag.match(r.group('msg'))
                if m:
                    if jobid not in self.users:
                        self.users[jobid] = m.group('user')
                    ehost = m.group('exechost')
                    self.job_nodes[jobid] = ResourceResv.get_hosts(ehost)
                    ncpus = int(m.group('ncpus'))
                    self.job_cpus[jobid] = ncpus
                    self.job_end[jobid] = tm

                    qtime = int(m.group('qtime'))
                    starttime = int(m.group('start'))
                    if starttime != 0 and qtime != 0:
                        # jobs enqueued prior to start of time range
                        # considered should be reset to start of time
                        # range. Only matters when computing
                        # utilization
                        if (self.utilization_parsing and
                                qtime < self.record_tm[0]):
                            qtime = self.record_tm[0]
                            if starttime < self.record_tm[0]:
                                starttime = self.record_tm[0]
                        self.wait_time.append(starttime - qtime)
                        if m.group('walltime'):
                            try:
                                walltime = self.logutils.convert_hhmmss_time(
                                    m.group('walltime').strip())
                                self.run_time.append(walltime)
                            except Exception:
                                pass
                        else:
                            walltime = tm - starttime
                            self.run_time.append(walltime)

                        if self.utilization_parsing:
                            self.used_cph += ncpus * (walltime / 60)
                            if self.utils:
                                self.used_nph += (len(self.job_nodes[jobid]) *
                                                  (walltime / 60))
            elif rec_type == 'Q':
                self.queue.append(tm)
            elif rec_type == 'D':
                if jobid not in self.job_end:
                    self.job_end[jobid] = tm

        elif end is not None and tm > end:
            return PARSER_OK_STOP

        return PARSER_OK_CONTINUE

    def epilogue(self, line):
        if self.running_jobs_parsing or self.accounting_workload_parsing:
            return

        if len(self.record_tm) > 0:
            last_record_tm = self.record_tm[len(self.record_tm) - 1]
            self.duration = last_record_tm - self.record_tm[0]
            self.info[DUR] = self.logutils._duration(self.duration)

        self.jobs_started = list(self.job_start.keys())
        self.jobs_ended = list(self.job_end.keys())
        self.job_node_size = [len(n) for n in self.job_nodes.values()]
        self.job_cpu_size = list(self.job_cpus.values())
        self.start = sorted(self.job_start.values())
        self.end = sorted(self.job_end.values())

        # list of jobs that have not yet ended, those are jobs that
        # have an S record but no E record. We port back the precomputed
        # metrics from the S record into the data to "publish"
        sjobs = set(self.jobs_started).difference(self.jobs_ended)
        for job in sjobs:
            if job in self.tmp_wait_time:
                self.wait_time.append(self.tmp_wait_time[job])
            if job in self.job_nodes:
                self.job_node_size.append(len(self.job_nodes[job]))
            if job in self.job_cpus:
                self.job_cpu_size.append(self.job_cpus[job])
            if self.utilization_parsing:
                if job in self.job_start:
                    if job in self.job_cpus:
                        self.used_cph += self.job_cpus[job] * \
                            ((last_record_tm - self.job_start[
                             job]) / 60)
                    if job in self.job_nodes:
                        self.used_nph += len(self.job_nodes[job]) * \
                            ((last_record_tm - self.job_start[
                             job]) / 60)

        # Process jobs currently running, those may have an S record
        # that is older than the time window considered or not.
        # If they have an S record, then they were already processed
        # by the S record routine, otherwise, they are processed here
        if self.utilization_parsing:
            first_record_tm = self.record_tm[0]
            a = {'job_state': (EQ, 'R'),
                 'Resource_List.ncpus': (SET, ''),
                 'exec_host': (SET, ''),
                 'stime': (SET, '')}
            alljobs = self._server.status(JOB, a)
            for job in alljobs:
                # the running_jobids is populated from the node's jobs
                # attribute. If a job id is not in the running jobids
                # list, then its S record was already processed
                if job['id'] not in self._running_jobids:
                    continue

                if ('job_state' not in job or
                        'Resource_List.ncpus' not in job or
                        'exec_host' not in job or 'stime' not in job):
                    continue
                # split to catch a customer tweak
                stime = int(job['stime'].split()[0])
                if stime < first_record_tm:
                    stime = first_record_tm
                self.used_cph += int(job['Resource_List.ncpus']) * \
                    (last_record_tm - stime)
                nodes = len(self.utils.parse_exechost(
                    job['exec_host']))
                self.used_nph += nodes * (last_record_tm - stime)

    def job_info(self, rec):
        """
        PBS Job information
        """
        m = self.record_tag.match(rec)
        if m:
            d = {}
            if m.group('type') == 'E':
                if getattr(self, 'jobid', None) != m.group('id'):
                    return PARSER_OK_CONTINUE
                if not hasattr(self, 'job_info_res'):
                    self.job_info_res = {}
                for a in m.group('msg').split():
                    (k, v) = a.split('=', 1)
                    d[k] = v
                self.job_info_res[m.group('id')] = d

        return PARSER_OK_CONTINUE

    def summary(self):
        """
        Accounting log summary
        """
        if self.running_jobs_parsing or self.accounting_workload_parsing:
            return

        run_rate = self.logutils.get_rate(self.start)
        queue_rate = self.logutils.get_rate(self.queue)
        end_rate = self.logutils.get_rate(self.end)

        self.info[NJQ] = len(self.queue)
        self.info[NJR] = len(self.start)
        self.info[NJE] = len(self.end)
        self.info[JRR] = run_rate
        self.info[JSR] = queue_rate
        self.info[JER] = end_rate
        if len(self.wait_time) > 0:
            wt = sorted(self.wait_time)
            wta = float(sum(self.wait_time)) / len(self.wait_time)
            self.info[JWTm] = self.logutils._duration(min(wt))
            self.info[JWTM] = self.logutils._duration(max(wt))
            self.info[JWTA] = self.logutils._duration(wta)
            self.info[JWT25] = self.logutils._duration(
                self.logutils.percentile(wt, .25))
            self.info[JWT50] = self.logutils._duration(
                self.logutils.percentile(wt, .5))
            self.info[JWT75] = self.logutils._duration(
                self.logutils.percentile(wt, .75))

        if len(self.run_time) > 0:
            rt = sorted(self.run_time)
            self.info[JRTm] = self.logutils._duration(min(rt))
            self.info[JRT25] = self.logutils._duration(
                self.logutils.percentile(rt, 0.25))
            self.info[JRT50] = self.logutils._duration(
                self.logutils.percentile(rt, 0.50))
            self.info[JRTA] = self.logutils._duration(
                str(sum(rt) / len(rt)))
            self.info[JRT75] = self.logutils._duration(
                self.logutils.percentile(rt, 0.75))
            self.info[JRTM] = self.logutils._duration(max(rt))

        if len(self.job_node_size) > 0:
            js = sorted(self.job_node_size)
            self.info[JNSm] = min(js)
            self.info[JNS25] = self.logutils.percentile(js, 0.25)
            self.info[JNS50] = self.logutils.percentile(js, 0.50)
            self.info[JNSA] = str("%.2f" % (float(sum(js)) / len(js)))
            self.info[JNS75] = self.logutils.percentile(js, 0.75)
            self.info[JNSM] = max(js)

        if len(self.job_cpu_size) > 0:
            js = sorted(self.job_cpu_size)
            self.info[JCSm] = min(js)
            self.info[JCS25] = self.logutils.percentile(js, 0.25)
            self.info[JCS50] = self.logutils.percentile(js, 0.50)
            self.info[JCSA] = str("%.2f" % (float(sum(js)) / len(js)))
            self.info[JCS75] = self.logutils.percentile(js, 0.75)
            self.info[JCSM] = max(js)

        if self.utilization_parsing:
            ncph = self._total_ncpus * self.duration
            nph = self._num_nodes * self.duration
            if ncph > 0:
                self.info[UNCPUS] = str("%.2f" %
                                        (100 * float(self.used_cph) / ncph) +
                                        '%')
            if nph > 0:
                self.info[UNODES] = str("%.2f" %
                                        (100 * float(self.used_nph) / nph) +
                                        '%')
            self.info[CPH] = self.used_cph
            self.info[NPH] = self.used_nph

        self.info[USRS] = len(set(self.users.values()))

        return self.info
