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


"""
PBS hook for managing cgroups on Linux execution hosts.
This hook contains the handlers required for PBS to support
cgroups on Linux hosts that support them (kernel 2.6.28 and higher)

This hook services the following events:
- exechost_periodic
- exechost_startup
- execjob_attach
- execjob_begin
- execjob_end
- execjob_epilogue
- execjob_launch
- execjob_resize
- execjob_abort
- execjob_postsuspend
- execjob_preresume
"""

# NOTES:
#
# When soft_limit is true for memory, memsw represents the hard limit.
#
# The resources value in sched_config must contain entries for mem and
# vmem if those subsystems are enabled in the hook configuration file. The
# amount of resource requested will not be avaiable to the hook if they
# are not present.

# if we are not on a Linux system then the hook should always do nothing
# and accept, and certainly not try Linux-only module imports
import platform

# Module imports
#
# This one is needed to log messages
import pbs

if platform.system() != 'Linux':
    pbs.logmsg(pbs.EVENT_DEBUG,
               'Cgroup hook not on supported OS '
               '-- just accepting event')
    pbs.event().accept()

# we're on Linux, but does the kernel support cgroups?
rel = list(map(int, (platform.release().split('-')[0].split('.'))))
pbs.logmsg(pbs.EVENT_DEBUG4,
           'Cgroup hook: detected Linux kernel version %d.%d.%d' %
           (rel[0], rel[1], rel[2]))

supported = False
if rel[0] > 2:
    supported = True
elif rel[0] == 2:
    if rel[1] > 6:
        supported = True
    elif rel[1] == 6:
        if rel[2] >= 28:
            supported = True

if not supported:
    pbs.logmsg(pbs.EVENT_DEBUG,
               'Cgroup hook: kernel %s.%s.%s < 2.6.28; not supported'
               % (rel[0], rel[1], rel[2]))
    pbs.event.accept()
else:
    # Now we know that at least the hook might do something useful
    # so import other modules (some of them Linux-specific)
    # Note the else is needed to be PEP8-compliant:  the imports must
    # not be unindented since they are not top-of-file
    import sys
    import os
    import stat
    import errno
    import signal
    import subprocess
    import re
    import glob
    import time
    import string
    import traceback
    import copy
    import operator
    import fnmatch
    import math
    import types
    try:
        import json
    except Exception:
        import simplejson as json
    multiprocessing = None
    try:
        # will fail in Python 2.5
        import multiprocessing
    except Exception:
        # but we can use isinstance(multiprocessing, types.ModuleType) later
        # to find out if it worked
        pass
    import fcntl
    import pwd

    PYTHON2 = sys.version_info[0] < 3

    try:
        bytearray
        BYTEARRAY_EXISTS = True
    except NameError:
        BYTEARRAY_EXISTS = False

# Define some globals that get set in main
PBS_EXEC = ''
PBS_HOME = ''
PBS_MOM_HOME = ''
PBS_MOM_JOBS = ''

# ============================================================================
# Derived error classes
# ============================================================================


class AdminError(Exception):
    """
    Base class for errors fixable only by administrative action.
    """
    pass


class ProcessingError(Exception):
    """
    Base class for errors in processing, unknown cause.
    """
    pass


class UserError(Exception):
    """
    Base class for errors fixable by the user.
    """
    pass


class JobValueError(Exception):
    """
    Errors in PBS job resource values.
    """
    pass


class CgroupBusyError(ProcessingError):
    """
    Errors when the cgroup is busy.
    """
    pass


class CgroupConfigError(AdminError):
    """
    Errors in configuring cgroup.
    """
    pass


class CgroupLimitError(AdminError):
    """
    Errors in configuring cgroup.
    """
    pass


class CgroupProcessingError(ProcessingError):
    """
    Errors processing cgroup.
    """
    pass


class TimeoutError(ProcessingError):
    """
    Timeout encountered.
    """
    pass


# ============================================================================
# Utility functions
# ============================================================================

#
# FUNCTION stringified_output
#
def stringified_output(out):
    if PYTHON2:
        if isinstance(out, str):
            return(out)
        elif isinstance(out, unicode):
            return(out.encode('utf-8'))
        else:
            return(str(out))
    else:
        if isinstance(out, str):
            return(out)
        elif isinstance(out, (bytes, bytearray)):
            return(out.decode('utf-8'))
        else:
            return(str(out))


#
# FUNCTION caller_name
#
def caller_name():
    """
    Return the name of the calling function or method.
    """
    return str(sys._getframe(1).f_code.co_name)


#
# FUNCTION systemd_escape
#
def systemd_escape(buf):
    """
    Escape strings for usage in system unit names
    Some distros don't provide the systemd-escape command
    """
    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
    if not isinstance(buf, str):
        raise ValueError('Not a basetype string')
    ret = ''
    for i, char in enumerate(buf):
        if i < 1 and char == '.':
            if PYTHON2:
                ret += '\\x' + '.'.encode('hex')
            else:
                ret += '\\x' + b'.'.hex()
        elif char.isalnum() or char in '_.':
            ret += char
        elif char == '/':
            ret += '-'
        else:
            # Will turn non-ASCII into UTF-8 hex sequence on both Py2/3
            if PYTHON2:
                hexval = char.encode('hex')
            else:
                hexval = char.encode('utf-8').hex()
            for j in range(0, len(hexval), 2):
                ret += '\\x' + hexval[j:j + 2]
    return ret


#
# FUNCTION convert_size
#
def convert_size(value, units='b'):
    """
    Convert a string containing a size specification (e.g. "1m") to a
    string using different units (e.g. "1024k").

    This function only interprets a decimal number at the start of the string,
    stopping at any unrecognized character and ignoring the rest of the string.

    When down-converting (e.g. MB to KB), all calculations involve integers and
    the result returned is exact. When up-converting (e.g. KB to MB) floating
    point numbers are involved. The result is rounded up. For example:

    1023MB -> GB yields 1g
    1024MB -> GB yields 1g
    1025MB -> GB yields 2g  <-- This value was rounded up

    Pattern matching or conversion may result in exceptions.
    """
    logs = {'b': 0, 'k': 10, 'm': 20, 'g': 30,
            't': 40, 'p': 50, 'e': 60, 'z': 70, 'y': 80}
    try:
        new = units[0].lower()
        if new not in logs:
            raise ValueError('Invalid unit value')
        result = re.match(r'([-+]?\d+)([bkmgtpezy]?)',
                          str(value).lower())
        if not result:
            raise ValueError('Unrecognized value')
        val, old = result.groups()
        if int(val) < 0:
            raise ValueError('Value may not be negative')
        if old not in logs:
            old = 'b'
        factor = logs[old] - logs[new]
        val = float(val)
        val *= 2 ** factor
        if (val - int(val)) > 0.0:
            val += 1.0
        val = int(val)
        # pbs.size() does not like units following zero
        if val <= 0:
            return '0'
        return str(val) + new
    except Exception:
        return None


#
# FUNCTION size_as_int
#
def size_as_int(value):
    """
    Convert a size string to an integer representation of size in bytes
    """
    in_bytes = convert_size(value, units='b')
    if in_bytes is not None:
        return int(convert_size(value).rstrip(string.ascii_lowercase))
    else:
        pbs.logmsg(pbs.EVENT_ERROR, "size_as_int: Value %s "
                   "does not convert to int, returning None" % value)
        return None


#
# FUNCTION convert_time
#
def convert_time(value, units='s'):
    """
    Converts a integer value for time into the value of the return unit

    A valid decimal number, with optional sign, may be followed by a character
    representing a scaling factor.  Scaling factors may be either upper or
    lower case. Examples include:
    250ms
    40s
    +15min

    Valid scaling factors are:
    ns  = 10**-9
    us  = 10**-6
    ms  = 10**-3
    s   =      1
    min =     60
    hr  =   3600

    Pattern matching or conversion may result in exceptions.
    """
    multipliers = {'': 1, 'ns': 10 ** -9, 'us': 10 ** -6,
                   'ms': 10 ** -3, 's': 1, 'min': 60, 'hr': 3600}
    new = units.lower()
    if new not in multipliers:
        raise ValueError('Invalid unit value')
    result = re.match(r'([-+]?\d+)\s*([a-zA-Z]+)',
                      str(value).lower())
    if not result:
        raise ValueError('Unrecognized value')
    num, factor = result.groups()
    # Check to see if there was not unit of time specified
    if factor is None:
        factor = ''
    # Check to see if the unit is valid
    if str.lower(factor) not in multipliers:
        raise ValueError('Time unit not recognized')
    # Convert the value to seconds
    value = float(num) * float(multipliers[str.lower(factor)])
    if units != 's':
        value = value / multipliers[new]
    # _pbs_v1.validate_input breaks with very small time values
    # because Python converts them to values like 1e-05
    if value < 0.001:
        value = 0.0
    return value


def decode_list(data):
    """
    json hook to convert lists from non string type to str
    """
    ret = []
    for item in data:
        if isinstance(item, str):
            pass
        if isinstance(item, list):
            item = decode_list(item)
        elif isinstance(item, dict):
            item = decode_dict(item)
        elif PYTHON2:
            if isinstance(item, unicode):
                item = item.encode('utf-8')
            elif BYTEARRAY_EXISTS and isinstance(item, bytearray):
                item = str(item)
        elif isinstance(item, (bytes, bytearray)):
            item = str(item, 'utf-8')
        ret.append(item)
    return ret


def decode_dict(data):
    """
    json hook to convert dictionaries from non string type to str
    """
    ret = {}
    for key, value in list(data.items()):
        # first the key
        if isinstance(key, str):
            pass
        elif PYTHON2:
            if isinstance(key, unicode):
                key = key.encode('utf-8')
            elif BYTEARRAY_EXISTS and isinstance(key, bytearray):
                key = str(key)
        elif isinstance(key, (bytes, bytearray)):
            key = str(key, 'utf-8')

        # now the value
        if isinstance(value, str):
            pass
        if isinstance(value, list):
            value = decode_list(value)
        elif isinstance(value, dict):
            value = decode_dict(value)
        elif PYTHON2:
            if isinstance(value, unicode):
                value = value.encode('utf-8')
            elif BYTEARRAY_EXISTS and isinstance(key, bytearray):
                value = str(value)
        elif isinstance(value, (bytes, bytearray)):
            value = str(value, 'utf-8')

        # add stringified (key, value) pair to result
        ret[key] = value
    return ret


def merge_dict(base, new):
    """
    Merge together two multilevel dictionaries where new
    takes precedence over base
    """
    if not isinstance(base, dict):
        raise ValueError('base must be type dict')
    if not isinstance(new, dict):
        raise ValueError('new must be type dict')
    newkeys = list(new.keys())
    merged = {}
    for key in base:
        if key in newkeys and isinstance(base[key], dict):
            # Take it off the list of keys to copy
            newkeys.remove(key)
            merged[key] = merge_dict(base[key], new[key])
        else:
            merged[key] = copy.deepcopy(base[key])
    # Copy the remaining unique keys from new
    for key in newkeys:
        merged[key] = copy.deepcopy(new[key])
    return merged


def expand_list(old):
    """
    Convert condensed list format (with ranges) to an expanded Python list.
    The input string is a comma separated list of digits and ranges.
    Examples include:
    0-3,8-11
    0,2,4,6
    2,5-7,10
    """
    new = []
    if isinstance(old, list):
        old = ",".join(list(map(str, old)))
    stripped = old.strip()
    if not stripped:
        return new
    for entry in stripped.split(','):
        if '-' in entry[1:]:
            start, end = entry.split('-', 1)
            for i in range(int(start), int(end) + 1):
                new.append(i)
        else:
            new.append(int(entry))
    return new


def find_files(path, pattern='*', kind='',
               follow_links=False, follow_mounts=True):
    """
    Return a list of files similar to the find command
    """
    if isinstance(pattern, str):
        pattern = [pattern]
    if isinstance(kind, str):
        if not kind:
            kind = []
        else:
            kind = [kind]
    if not isinstance(pattern, list):
        raise TypeError('Pattern must be a string or list')
    if not isinstance(kind, list):
        raise TypeError('Kind must be a string or list')
    # Top level not excluded if it is a mount point
    mounts = []
    for root, dirs, files in os.walk(path, followlinks=follow_links):
        for name in [os.path.join(root, x) for x in dirs + files]:
            if not follow_mounts:
                if os.path.isdir(name) and os.path.ismount(name):
                    mounts.append(os.path.join(name, ''))
                    continue
                undermount = False
                for mountpoint in mounts:
                    if name.startswith(mountpoint):
                        undermount = True
                        break
                if undermount:
                    continue
            pattern_matched = False
            for pat in pattern:
                if fnmatch.fnmatchcase(os.path.basename(name), pat):
                    pattern_matched = True
                    break
            if not pattern_matched:
                continue
            if not kind:
                yield name
                continue
            statinfo = os.lstat(name).st_mode
            for entry in kind:
                if not entry:
                    yield name
                    break
                for letter in entry:
                    if letter == 'f' and stat.S_ISREG(statinfo):
                        yield name
                        break
                    elif letter == 'l' and stat.S_ISLNK(statinfo):
                        yield name
                        break
                    elif letter == 'c' and stat.S_ISCHR(statinfo):
                        yield name
                        break
                    elif letter == 'b' and stat.S_ISBLK(statinfo):
                        yield name
                        break
                    elif letter == 'p' and stat.S_ISFIFO(statinfo):
                        yield name
                        break
                    elif letter == 's' and stat.S_ISSOCK(statinfo):
                        yield name
                        break
                    elif letter == 'd' and stat.S_ISDIR(statinfo):
                        yield name
                        break


def initialize_resource(resc):
    """
    Return a properly cast zero value
    """
    if isinstance(resc, pbs.pbs_int):
        ret = pbs.pbs_int(0)
    elif isinstance(resc, pbs.pbs_float):
        ret = pbs.pbs_float(0)
    elif isinstance(resc, pbs.size):
        ret = pbs.size('0')
    elif isinstance(resc, int):
        ret = 0
    elif isinstance(resc, float):
        ret = 0.0
    elif isinstance(resc, list):
        ret = []
    elif isinstance(resc, dict):
        ret = {}
    elif isinstance(resc, tuple):
        ret = ()
    elif isinstance(resc, str):
        ret = ''
    else:
        raise ValueError('Unable to initialize unknown resource type')
    return ret


def printjob_info(jobid, include_attributes=False):
    """
    Use printjob to acquire the job information
    """
    info = {}
    jobfile = os.path.join(PBS_MOM_JOBS, '%s.JB' % jobid)
    if not os.path.isfile(jobfile):
        pbs.logmsg(pbs.EVENT_DEBUG4, 'File not found: %s' % (jobfile))
        return info
    cmd = [os.path.join(PBS_EXEC, 'bin', 'printjob')]
    if not include_attributes:
        cmd.append('-a')
    cmd.append(jobfile)
    try:
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Running: %s' % cmd)
        process = subprocess.Popen(cmd, shell=False,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE,
                                   universal_newlines=True)
        out, err = process.communicate()
        if process.returncode != 0:
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'command return code non-zero: %s'
                       % str(process.returncode))
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'command stderr: %s'
                       % stringified_output(err))
    except Exception as exc:
        pbs.logmsg(pbs.EVENT_DEBUG2, 'Error running command: %s' % cmd)
        pbs.logmsg(pbs.EVENT_DEBUG2, 'Exception: %s' % exc)
        return {}
    # if we get a non-str type then convert before calling splitlines
    # should not happen since we pass universal_newlines True
    out_split = stringified_output(out).splitlines()
    pattern = re.compile(r'^(\w.*):\s*(\S+)')
    for line in out_split:
        result = re.match(pattern, line)
        if not result:
            continue
        key, val = result.groups()
        if not key or not val:
            continue
        if val.startswith('0x'):
            info[key] = int(val, 16)
        elif val.isdigit():
            info[key] = int(val)
        else:
            info[key] = val
    pbs.logmsg(pbs.EVENT_DEBUG4, 'JB file info returned: %s' % repr(info))
    return info


def job_is_suspended(jobid):
    """
    Returns True if job is in a suspended or unknown substate
    """
    jobinfo = printjob_info(jobid)
    if 'substate' in jobinfo:
        return jobinfo['substate'] in [43, 45, 'unknown']
    return False


def job_is_running(jobid):
    """
    Returns True if job shows a running state and substate
    """
    jobinfo = printjob_info(jobid)
    if 'substate' in jobinfo:
        return jobinfo['substate'] == 42
    return False


def fetch_vnode_comments_nomp(vnode_list, timeout=10):
    comment_dict = {}
    failure = False
    pbs.logmsg(pbs.EVENT_DEBUG4,
               "vnode list in fetch_vnode_comment is %s"
               % vnode_list)
    try:
        with Timeout(timeout, 'Timed out contacting server'):
            for vn in vnode_list:
                comment_dict[vn] = pbs.server().vnode(vn).comment
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           "comment for vnode %s fetched from server is %s"
                           % (str(vn), comment_dict[vn]))
    except TimeoutError:
        # pbs.server().vnode(xx).comment got stuck, or the timeout
        # was too short for the number of nodes supplied
        pbs.logmsg(pbs.EVENT_ERROR,
                   'Timed out while fetching comments from server, '
                   'timeout was %s' % str(timeout))
        failure = True
    except Exception as exc:
        # other exception, like e.g. wrong vnode name
        pbs.logmsg(pbs.EVENT_ERROR,
                   'Unexpected error in fetch_vnode_comments: %s'
                   % repr(exc))
        failure = True
    # only return a full dictionary if you got the comment for all vnodes
    if not failure:
        return (comment_dict, failure)
    else:
        return ({}, failure)


def fetch_vnode_comments_queue(vnode_list, commq):
    comment_dict = {}
    failure = False
    pbs.logmsg(pbs.EVENT_DEBUG4,
               "vnode list in fetch_vnode_comment is %s"
               % vnode_list)
    try:
        for vn in vnode_list:
            comment_dict[vn] = pbs.server().vnode(vn).comment
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       "comment for vnode %s fetched from server is %s"
                       % (str(vn), comment_dict[vn]))

    except Exception as exc:
        # other exception, like e.g. wrong vnode name
        pbs.logmsg(pbs.EVENT_ERROR,
                   'Unexpected error in fetch_vnode_comments: %s'
                   % repr(exc))
        failure = True
    # only return a full dictionary if you got the comment for all vnodes
    if not failure:
        commq.put(comment_dict)
    else:
        commq.put({})
    return True


def fetch_vnode_comments_mp(vnode_list, timeout=10):
    worker_comment_dict = {}
    commq = multiprocessing.Queue()
    worker = multiprocessing.Process(target=fetch_vnode_comments_queue,
                                     args=(vnode_list, commq))
    worker.start()
    worker.join(timeout)
    if worker.is_alive():
        pbs.logmsg(pbs.EVENT_ERROR,
                   "comment fetcher for %s timed out after %s seconds"
                   % (str(vnode_list), timeout))
        # will mess up the commq Queue but we don't care
        # will send SIGTERM, but it's possible that is masked
        # while in a SWIG function
        # we don't really care since unline in threading,
        # in multiprocessing we can exit and orphan the worker
        worker.terminate()
        pbs.logmsg(pbs.EVENT_ERROR,
                   'Timed out while fetching comments from server,'
                   'timeout was %s' % str(timeout))
        return ({}, True)
    else:
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   "comments fetched from server without timeout")
        comment_dict = {}
        try:
            comment_dict = commq.get()
        except Exception:
            # Treat failure to get comments dictionary from queue
            # as a timeout
            return ({}, True)
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   "worker comment_dict is %r" % comment_dict)

        return (comment_dict, False)


def fetch_vnode_comments(vnode_list, timeout=10):
    if not isinstance(multiprocessing, types.ModuleType):
        pbs.logmsg(pbs.EVENT_DEBUG4, "multiprocessing not available, "
                   "fetch_vnode_comment will use SIGALRM timeout")
        return fetch_vnode_comments_nomp(vnode_list, timeout)
    else:
        pbs.logmsg(pbs.EVENT_DEBUG4, "multiprocessing available, "
                   "fetch_vnode_comment will use mp for timeout")
        return fetch_vnode_comments_mp(vnode_list, timeout)


# ============================================================================
# Utility classes
# ============================================================================

#
# CLASS Lock
#
class Lock(object):
    """
    Implement a simple locking mechanism using a file lock
    """

    def __init__(self, path):
        self.path = path
        self.lockfd = None

    def getpath(self):
        """
        Return the path of the lock file.
        """
        return self.path

    def getlockfd(self):
        """
        Return the file descriptor of the lock file.
        """
        return self.lockfd

    def __enter__(self):
        self.lockfd = open(self.path, 'w')
        fcntl.flock(self.lockfd, fcntl.LOCK_EX)
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s file lock acquired by %s' %
                   (self.path, str(sys._getframe(1).f_code.co_name)))

    def __exit__(self, exc, val, trace):
        if self.lockfd:
            fcntl.flock(self.lockfd, fcntl.LOCK_UN)
            self.lockfd.close()
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s file lock released by %s' %
                   (self.path, str(sys._getframe(1).f_code.co_name)))


#
# CLASS Timeout
#
class Timeout(object):
    """
    Implement a timeout mechanism via SIGALRM
    """

    def __init__(self, duration=1, message='Operation timed out'):
        self.duration = duration
        self.message = message

    def handler(self, sig, frame):
        """
        Throw a timeout error when SIGALRM is received
        """
        raise TimeoutError(self.message)

    def getduration(self):
        """
        Return the timeout duration
        """
        return self.duration

    def getmessage(self):
        """
        Return the timeout message
        """
        return self.message

    def __enter__(self):
        if signal.getsignal(signal.SIGALRM):
            raise RuntimeError('Alarm handler already registered')
        signal.signal(signal.SIGALRM, self.handler)
        signal.alarm(self.duration)

    def __exit__(self, exc, val, trace):
        signal.alarm(0)
        signal.signal(signal.SIGALRM, signal.SIG_DFL)


#
# CLASS HookUtils
#
class HookUtils(object):
    """
    Hook utility methods
    """

    def __init__(self, hook_events=None):
        if hook_events is not None:
            self.hook_events = hook_events
        else:
            # Defined in the order they appear in module_pbs_v1.c
            # if adding new events that may not exist in all PBS versions,
            # use hasattr() to prevent exceptions when the hook is used
            # on older PBS versions - see e.g. EXECJOB_ABORT
            self.hook_events = {}
            self.hook_events[pbs.QUEUEJOB] = {
                'name': 'queuejob',
                'handler': None
            }
            self.hook_events[pbs.MODIFYJOB] = {
                'name': 'modifyjob',
                'handler': None
            }
            self.hook_events[pbs.RESVSUB] = {
                'name': 'resvsub',
                'handler': None
            }
            if hasattr(pbs, "MODIFYRESV"):
                self.hook_events[pbs.MODIFYRESV] = {
                    'name': 'modifyresv',
                    'handler': None
                }
            self.hook_events[pbs.MOVEJOB] = {
                'name': 'movejob',
                'handler': None
            }
            self.hook_events[pbs.RUNJOB] = {
                'name': 'runjob',
                'handler': None
            }
            if hasattr(pbs, "MANAGEMENT"):
                self.hook_events[pbs.MANAGEMENT] = {
                    'name': 'management',
                    'handler': None
                }
            if hasattr(pbs, "MODIFYVNODE"):
                self.hook_events[pbs.MODIFYVNODE] = {
                    'name': 'modifyvnode',
                    'handler': None
                }
            self.hook_events[pbs.PROVISION] = {
                'name': 'provision',
                'handler': None
            }
            if hasattr(pbs, "RESV_END"):
                self.hook_events[pbs.RESV_END] = {
                'name': 'resv_end',
                'handler': None
                }
            if hasattr(pbs, "RESV_BEGIN"):
                self.hook_events[pbs.RESV_BEGIN] = {
                'name': 'resv_begin',
                'handler': None
                }
            if hasattr(pbs, "RESV_CONFIRM"):
                self.hook_events[pbs.RESV_CONFIRM] = {
                'name': 'resv_confirm',
                'handler': None
                }
            self.hook_events[pbs.EXECJOB_BEGIN] = {
                'name': 'execjob_begin',
                'handler': self._execjob_begin_handler
            }
            self.hook_events[pbs.EXECJOB_PROLOGUE] = {
                'name': 'execjob_prologue',
                'handler': None
            }
            self.hook_events[pbs.EXECJOB_EPILOGUE] = {
                'name': 'execjob_epilogue',
                'handler': self._execjob_epilogue_handler
            }
            self.hook_events[pbs.EXECJOB_PRETERM] = {
                'name': 'execjob_preterm',
                'handler': None
            }
            self.hook_events[pbs.EXECJOB_END] = {
                'name': 'execjob_end',
                'handler': self._execjob_end_handler
            }
            self.hook_events[pbs.EXECJOB_LAUNCH] = {
                'name': 'execjob_launch',
                'handler': self._execjob_launch_handler
            }
            self.hook_events[pbs.EXECHOST_PERIODIC] = {
                'name': 'exechost_periodic',
                'handler': self._exechost_periodic_handler
            }
            self.hook_events[pbs.EXECHOST_STARTUP] = {
                'name': 'exechost_startup',
                'handler': self._exechost_startup_handler
            }
            self.hook_events[pbs.EXECJOB_ATTACH] = {
                'name': 'execjob_attach',
                'handler': self._execjob_attach_handler
            }
            if hasattr(pbs, "EXECJOB_RESIZE"):
                self.hook_events[pbs.EXECJOB_RESIZE] = {
                    'name': 'execjob_resize',
                    'handler': self._execjob_resize_handler
                }
            if hasattr(pbs, "EXECJOB_ABORT"):
                self.hook_events[pbs.EXECJOB_ABORT] = {
                    'name': 'execjob_abort',
                    'handler': self._execjob_end_handler
                }
            if hasattr(pbs, "EXECJOB_POSTSUSPEND"):
                self.hook_events[pbs.EXECJOB_POSTSUSPEND] = {
                    'name': 'execjob_postsuspend',
                    'handler': self._execjob_postsuspend_handler
                }
            if hasattr(pbs, "EXECJOB_PRERESUME"):
                self.hook_events[pbs.EXECJOB_PRERESUME] = {
                    'name': 'execjob_preresume',
                    'handler': self._execjob_preresume_handler
                }
            self.hook_events[pbs.MOM_EVENTS] = {
                'name': 'mom_events',
                'handler': None
            }

    def __repr__(self):
        return 'HookUtils(%s)' % (repr(self.hook_events))

    def event_name(self, hooktype):
        """
        Return the event name for the supplied hook type.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if hooktype in self.hook_events:
            return self.hook_events[hooktype]['name']
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   '%s: Type: %s not found' % (caller_name(), type))
        return None

    def hashandler(self, hooktype):
        """
        Return the handler for the supplied hook type.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if hooktype in self.hook_events:
            return self.hook_events[hooktype]['handler'] is not None
        return None

    def invoke_handler(self, event, cgroup, jobutil, *args):
        """
        Call the appropriate handler for the supplied event.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: UID: real=%d, effective=%d' %
                   (caller_name(), os.getuid(), os.geteuid()))
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: GID: real=%d, effective=%d' %
                   (caller_name(), os.getgid(), os.getegid()))
        if self.hashandler(event.type):
            return self.hook_events[event.type]['handler'](event, cgroup,
                                                           jobutil, *args)
        pbs.logmsg(pbs.EVENT_DEBUG2,
                   '%s: %s event not handled by this hook' %
                   (caller_name(), self.event_name(event.type)))
        return False

    def _execjob_begin_handler(self, event, cgroup, jobutil):
        """
        Handler for execjob_begin events.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Instantiate the NodeUtils class for get_memory_on_node and
        # get_vmem_on node
        node = NodeUtils(cgroup.cfg)
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: NodeUtils class instantiated' %
                   caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Host assigned job resources: %s' %
                   (caller_name(), jobutil.assigned_resources))
        # Make sure the parent cgroup directories exist
        cgroup.create_paths()
        # Make sure the cgroup does not already exist
        # from a failed run
        cgroup.delete(event.job.id, False)
        # Now that we have a lock, determine the current cgroup tree assigned
        # resources
        cgroup.assigned_resources = cgroup._get_assigned_cgroup_resources()
        # Create the cgroup(s) for the job
        cgroup.create_job(event.job.id, node)
        if (cgroup.cfg['cgroup']['cpuset']['enabled']
                and not cgroup.cfg['cgroup']['cpuset']['allow_zero_cpus']):
            if ('ncpus' not in jobutil.assigned_resources
                    or jobutil.assigned_resources['ncpus'] <= 0):
                if event.job.in_ms_mom():
                    pbs.logmsg(pbs.EVENT_ERROR,
                               'cpuset enabled with mandatory ncpus >= 1, '
                               'but job does not request ncpus on '
                               'mother superior: rejecting job')
                    event.reject('cpuset enabled with mandatory ncpus >= 1, '
                                 'but job does not request ncpus on '
                                 'mother superior: rejecting job')
                else:
                    # will be done in configure_job
                    pbs.logmsg(pbs.EVENT_JOB_USAGE,
                               'cpuset enabled with mandatory ncpus >= 1, '
                               'but job does not request ncpus on host: '
                               'deleting cpuset cgroup')

        # Configure the new cgroup
        cgroup.configure_job(event.job, jobutil.assigned_resources,
                             node, cgroup, event.type)

        # Write out the assigned resources
        cgroup.write_cgroup_assigned_resources(event.job.id)
        # Write out the environment variable for the host (pbs_attach)
        if 'device_names' in cgroup.assigned_resources:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Devices: %s' %
                       (caller_name(),
                        cgroup.assigned_resources['device_names']))
            env_list = []
            if cgroup.assigned_resources['device_names']:
                mics = []
                gpus = []
                for key in cgroup.assigned_resources['device_names']:
                    if key.startswith('mic'):
                        mics.append(key[3:])
                    elif (key.startswith('nvidia')
                            and "gpu" in node.devices
                            and key in node.devices['gpu']):
                        if 'uuid' in node.devices['gpu'][key]:
                            gpus.append(node.devices['gpu'][key]['uuid'])
                        if 'uuids' in node.devices['gpu'][key]:
                            gpus.extend(node.devices['gpu'][key]['uuids'])
                if mics:
                    env_list.append('OFFLOAD_DEVICES=%s' %
                                    ",".join(mics))
                if gpus:
                    # Don't put quotes around the values. ex "0" or "0,1".
                    # This will cause it to fail.
                    env_list.append('CUDA_VISIBLE_DEVICES=%s' %
                                    ",".join(gpus))
                    env_list.append('CUDA_DEVICE_ORDER=PCI_BUS_ID')
            pbs.logmsg(pbs.EVENT_DEBUG4, 'ENV_LIST: %s' % env_list)
            cgroup.write_job_env_file(event.job.id, env_list)
        # Add jobid to cgroup_jobs file to tell periodic handler that this
        # job is new and its cgroup should not be cleaned up
        cgroup.add_jobid_to_cgroup_jobs(event.job.id)

        # Initialize resources_used values that the hook will update
        # so that they are not updated through MoM polling.
        # Particularly important for resources_used.vmem which has
        # different semantics when the cgroup hook manages mem+swap.
        # Add 'force' flag because we haven't reached substate 42 yet
        cgroup.update_job_usage(event.job.id, event.job.resources_used,
                                force=True)
        return True

    def _execjob_epilogue_handler(self, event, cgroup, jobutil):
        """
        Handler for execjob_epilogue events.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # delete this jobid from cgroup_jobs in case hook events before me
        # failed to do that
        cgroup.remove_jobid_from_cgroup_jobs(event.job.id)
        # The resources_used information has a base type of pbs_resource.
        # Update the usage data
        cgroup.update_job_usage(event.job.id, event.job.resources_used)
        # The job script has completed, but the obit has not been sent.
        # Delete the cgroups for this job so that they don't interfere
        # with incoming jobs assigned to this node.
        cgroup.delete(event.job.id)
        return True

    def _execjob_end_handler(self, event, cgroup, jobutil):
        """
        Handler for execjob_end events.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # delete this jobid from cgroup_jobs in case hook events before me
        # failed to do that
        cgroup.remove_jobid_from_cgroup_jobs(event.job.id)
        # The cgroup is usually deleted in the execjob_epilogue event
        # There are certain corner cases where epilogue can fail or skip
        # Delete files again here to make sure we catch those
        # cgroup.delete() does nothing if files are already deleted
        cgroup.delete(event.job.id)
        # Remove the assigned_resources and job_env files.
        filelist = []
        filelist.append(os.path.join(cgroup.hook_storage_dir, event.job.id))
        filelist.append(cgroup.host_job_env_filename % event.job.id)
        for filename in filelist:
            try:
                os.remove(filename)
            except OSError:
                pbs.logmsg(pbs.EVENT_DEBUG4, 'File: %s not found' % (filename))
            except Exception:
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Error removing file: %s' % (filename))
        return True

    def manage_rlimit_as(self, job):
        """
        Sets rlimit_as according to pvmem (if present)
        if pvmem is not present, sets it to unlimited
        (since we know vmem specifies memory+swap usage limit,
         not address space limit)
        """
        import resource
        rlimit_as = None
        parent_pid = os.getppid()
        if 'pvmem' in job.Resource_List and job.Resource_List['pvmem']:
            rlimit_as = size_as_int(job.Resource_List['pvmem'])
        else:
            rlimit_as = resource.RLIM_INFINITY

        prlimit_command = None
        if hasattr(resource, 'prlimit'):
            # No need to call command -- Python has direct support
            prlimit_command = ''
            pbs.logmsg(pbs.EVENT_DEBUG3,
                       'Calling resource.prlimit(%s, resource.RLIMIT_AS, %s)'
                       % (parent_pid, str((rlimit_as, rlimit_as))))
            resource.prlimit(parent_pid, resource.RLIMIT_AS,
                             (rlimit_as, rlimit_as))
        elif os.path.isfile('/usr/bin/prlimit'):
            prlimit_command = '/usr/bin/prlimit'
        elif os.path.isfile('/bin/prlimit'):
            prlimit_command = '/bin/prlimit'

        if prlimit_command is None:
            pbs.logmsg(pbs.EVENT_DEBUG, "cannot set rlimit_as for task: "
                       "prlimit command not found")
        elif prlimit_command != '':
            cmd = [prlimit_command,
                   '--as=' + str(rlimit_as) + ':' + str(rlimit_as),
                   '--pid=' + str(parent_pid)]
            pbs.logmsg(pbs.EVENT_DEBUG3, 'Running: ' + ' '.join(cmd))
            try:
                # Try running the prlimit command
                process = subprocess.Popen(cmd, shell=False,
                                           stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE)
                out = process.communicate()[0]
            except Exception:
                pbs.logmsg(pbs.EVENT_ERROR,
                           'Found but to failed to execute: %s' %
                           ' '.join(cmd))

    def _execjob_launch_handler(self, event, cgroup, jobutil):
        """
        Handler for execjob_launch events.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        node = NodeUtils(cgroup.cfg)
        # delete this jobid from cgroup_jobs in case hook events before me
        # failed to do that
        cgroup.remove_jobid_from_cgroup_jobs(event.job.id)
        # Add the parent process id to the appropriate cgroups.
        cgroup.add_pids(os.getppid(), jobutil.job.id)
        # FUTURE: Add environment variable to the job environment
        # if job requested mic or gpu
        cgroup.read_cgroup_assigned_resources(event.job.id)
        if cgroup.assigned_resources is not None:
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'assigned_resources: %s' %
                       (cgroup.assigned_resources))
            if "gpu" in node.devices:
                cgroup.setup_job_devices_env(node.devices['gpu'])

        # If vmem was requested, set per process address space limit
        # or clear the one MoM has set,
        # if possible (requires prlimit support)
        if (cgroup.cfg['manage_rlimit_as']
                and cgroup.cfg['cgroup']['memsw']['enabled']):
            self.manage_rlimit_as(pbs.event().job)
        return True

    def _exechost_periodic_handler(self, event, cgroup, jobutil):
        """
        Handler for exechost_periodic events.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Instantiate the NodeUtils class for gather_jobs_on_node
        node = NodeUtils(cgroup.cfg)
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: NodeUtils class instantiated' %
                   caller_name())
        # Cleanup cgroups for jobs not present on this node
        jobdict = node.gather_jobs_on_node(cgroup)
        for jobid in event.job_list:
            if jobid not in jobdict:
                jobdict[jobid] = float()
        remaining = cgroup.cleanup_orphans(jobdict)
        # Offline the node if there are remaining orphans
        if remaining > 0:
            try:
                node.take_node_offline()
            except Exception as exc:
                pbs.logmsg(pbs.EVENT_DEBUG, '%s: Failed to offline node: %s' %
                           (caller_name(), exc))
        # Online nodes that were offlined due to a cgroup not cleaning up
        if remaining == 0 and cgroup.cfg['online_offlined_nodes']:
            node.bring_node_online()
        # Update the resource usage information for each job
        if cgroup.cfg['periodic_resc_update']:
            # Using event.job_list, without the parenthesis, will
            # make the dictionary iterable.
            for jobid in event.job_list:
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           '%s: Updating resource usage for %s' %
                           (caller_name(), jobid))
                try:
                    cgroup.update_job_usage(jobid, (event.job_list[jobid]
                                                    .resources_used))
                except Exception:
                    pbs.logmsg(pbs.EVENT_DEBUG, '%s: Failed to update %s' %
                               (caller_name(), jobid))
        return True

    def _exechost_startup_handler(self, event, cgroup, jobutil):
        """
        Handler for exechost_startup events.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        cgroup.create_paths()
        node = NodeUtils(cgroup.cfg)
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: NodeUtils class instantiated' %
                   caller_name())
        node.create_vnodes(cgroup.vntype)
        host = node.hostname
        # The memory limits are interdependent and might fail when set.
        # There are three limits. Worst case scenario is to loop three
        # times in order to set them all.
        mem_on_node = 0
        for _ in range(3):
            result = True
            if 'memory' in cgroup.subsystems:
                val = node.get_memory_on_node()
                mem_on_node = val
                if val is not None and val > 0:
                    try:
                        cgroup.set_limit('mem', val)
                    except Exception:
                        result = False
                try:
                    val = int(cgroup.cfg['cgroup']['memory']['swappiness'])
                    cgroup.set_swappiness(val)
                except Exception:
                    # error is not fatal, do not set result to False
                    pbs.logmsg(pbs.EVENT_DEBUG,
                               '%s: Failed to set swappiness' % caller_name())
            if 'memsw' in cgroup.subsystems:
                val = node.get_vmem_on_node()
                if val < mem_on_node:
                    val = mem_on_node
                if val is not None and val > 0:
                    try:
                        cgroup.set_limit('vmem', val)
                    except Exception:
                        result = False
            if 'hugetlb' in cgroup.subsystems:
                val = node.get_hpmem_on_node(ignore_reserved=False)
                if val is not None and val > 0:
                    try:
                        cgroup.set_limit('hpmem', val)
                    except Exception:
                        result = False
            if result:
                return True
        return False

    def _execjob_attach_handler(self, event, cgroup, jobutil):
        """
        Handler for execjob_attach events.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Ensure the job ID has been removed from cgroup_jobs
        cgroup.remove_jobid_from_cgroup_jobs(event.job.id)
        pbs.logjobmsg(jobutil.job.id, '%s: Attaching PID %s' %
                      (caller_name(), event.pid))
        # Add all processes in the job session to the appropriate cgroups
        cgroup.add_pids(event.pid, jobutil.job.id)
        return True

    def _execjob_resize_handler(self, event, cgroup, jobutil):
        """
        Handler for execjob_resize events.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Instantiate the NodeUtils class for get_memory_on_node and
        # get_vmem_on node
        node = NodeUtils(cgroup.cfg)
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: NodeUtils class instantiated' %
                   caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Host assigned job resources: %s' %
                   (caller_name(), jobutil.assigned_resources))
        if (cgroup.cfg['cgroup']['cpuset']['enabled']
                and not cgroup.cfg['cgroup']['cpuset']['allow_zero_cpus']):
            if ('ncpus' not in jobutil.assigned_resources
                    or jobutil.assigned_resources['ncpus'] <= 0):
                if event.job.in_ms_mom():
                    pbs.logmsg(pbs.EVENT_ERROR,
                               'cpuset enabled with mandatory ncpus >= 1, '
                               'but job does not request ncpus on '
                               'mother superior: rejecting resize')
                    event.reject('cpuset enabled with mandatory ncpus >= 1, '
                                 'but job does not request ncpus on '
                                 'mother superior: rejecting resize')
                else:
                    # will be done in configure_job
                    pbs.logmsg(pbs.EVENT_ERROR,
                               'cpuset enabled with mandatory ncpus >= 1, '
                               'but job no longer requests ncpus on host: '
                               'deleting cpuset cgroup')

        # Configure the cgroup
        cgroup.configure_job(event.job, jobutil.assigned_resources,
                             node, cgroup, event.type)
        # Write out the assigned resources
        cgroup.write_cgroup_assigned_resources(event.job.id)
        # Write out the environment variable for the host (pbs_attach)
        if 'device_names' in cgroup.assigned_resources:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Devices: %s' %
                       (caller_name(),
                        cgroup.assigned_resources['device_names']))
            env_list = []
            if cgroup.assigned_resources['device_names']:
                mics = []
                gpus = []
                for key in cgroup.assigned_resources['device_names']:
                    if key.startswith('mic'):
                        mics.append(key[3:])
                    elif (key.startswith('nvidia')
                            and "gpu" in node.devices
                            and key in node.devices['gpu']):
                        if 'uuid' in node.devices['gpu'][key]:
                            gpus.append(node.devices['gpu'][key]['uuid'])
                        if 'uuids' in node.devices['gpu'][key]:
                            gpus.extend(node.devices['gpu'][key]['uuids'])
                if mics:
                    env_list.append('OFFLOAD_DEVICES=%s' %
                                    ",".join(mics))
                if gpus:
                    # Don't put quotes around the values. ex "0" or "0,1".
                    # This will cause it to fail.
                    env_list.append('CUDA_VISIBLE_DEVICES=%s' %
                                    ",".join(gpus))
                    env_list.append('CUDA_DEVICE_ORDER=PCI_BUS_ID')
            pbs.logmsg(pbs.EVENT_DEBUG4, 'ENV_LIST: %s' % env_list)
            cgroup.write_job_env_file(event.job.id, env_list)
        return True

    def _execjob_postsuspend_handler(self, event, cgroup, jobutil):
        """
        Handler for execjob_postsuspend events.
        """
        return True

    def _execjob_preresume_handler(self, event, cgroup, jobutil):
        """
        Handler for execjob_preresume events.
        """
        return True

#
# CLASS JobUtils
#


class JobUtils(object):
    """
    Job utility methods
    """

    def __init__(self, job, hostname=None, assigned_resources=None):
        self.job = job
        if hostname is not None:
            self.hostname = hostname
        else:
            self.hostname = pbs.get_local_nodename()
        if assigned_resources is not None:
            self.assigned_resources = assigned_resources
        else:
            self.assigned_resources = self._get_assigned_job_resources()

    def __repr__(self):
        return ('JobUtils(%s, %s, %s)' %
                (repr(self.job),
                 repr(self.hostname),
                 repr(self.assigned_resources)))

    def _get_assigned_job_resources(self, hostname=None):
        """
        Return a dictionary of assigned resources on the local node
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Bail out if no hostname was provided
        if not hostname:
            hostname = self.hostname
        if not hostname:
            raise CgroupProcessingError('No hostname available')
        # Bail out if no job information is present
        if self.job is None:
            raise CgroupProcessingError('No job information available')
        # Create a list of local vnodes
        vnodes = []
        vnhost_pattern = r'%s\[[\d]+\]' % hostname
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: vnhost pattern: %s' %
                   (caller_name(), vnhost_pattern))
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Job exec_vnode list: %s' %
                   (caller_name(), self.job.exec_vnode))
        for match in re.findall(vnhost_pattern, str(self.job.exec_vnode)):
            vnodes.append(match)
        if vnodes:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Vnodes on %s: %s' %
                       (caller_name(), hostname, vnodes))
        # Collect host assigned resources
        resources = {}
        for chunk in self.job.exec_vnode.chunks:
            if vnodes:
                # Vnodes list is not empty
                if chunk.vnode_name not in vnodes:
                    continue
                if 'vnodes' not in resources:
                    resources['vnodes'] = {}
                if chunk.vnode_name not in resources['vnodes']:
                    resources['vnodes'][chunk.vnode_name] = {}
                # Initialize any missing resources for the vnode.
                # This check is needed because some resources might
                # not be present in each chunk of a job. For example:
                # exec_vnodes =
                # (node1[0]:ncpus=4:mem=4gb+node1[1]:mem=2gb) +
                # (node1[1]:ncpus=3+node[0]:ncpus=1:mem=4gb)
                for resc in list(chunk.chunk_resources.keys()):
                    vnresc = resources['vnodes'][chunk.vnode_name]
                    if resc in list(vnresc.keys()):
                        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: %s:%s defined' %
                                   (caller_name(), chunk.vnode_name, resc))
                    else:
                        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: %s:%s missing' %
                                   (caller_name(), chunk.vnode_name, resc))
                        vnresc[resc] = \
                            initialize_resource(chunk.chunk_resources[resc])
                pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Chunk %s resources: %s' %
                           (caller_name(), chunk.vnode_name, resources))
            else:
                # Vnodes list is empty
                if chunk.vnode_name != hostname:
                    continue
            for resc in list(chunk.chunk_resources.keys()):
                if resc not in list(resources.keys()):
                    resources[resc] = \
                        initialize_resource(chunk.chunk_resources[resc])
                # Add resource value to total
                if isinstance(chunk.chunk_resources[resc],
                              (pbs.pbs_int, pbs.pbs_float, pbs.size)):
                    resources[resc] += chunk.chunk_resources[resc]
                    pbs.logmsg(pbs.EVENT_DEBUG4,
                               '%s: resources[%s][%s] is now %s' %
                               (caller_name(), hostname, resc,
                                resources[resc]))
                    if vnodes:
                        resources['vnodes'][chunk.vnode_name][resc] += \
                            chunk.chunk_resources[resc]
                else:
                    pbs.logmsg(pbs.EVENT_DEBUG4,
                               '%s: Setting resource %s to string %s' %
                               (caller_name(), resc,
                                str(chunk.chunk_resources[resc])))
                    resources[resc] = str(chunk.chunk_resources[resc])
                    if vnodes:
                        resources['vnodes'][chunk.vnode_name][resc] = \
                            str(chunk.chunk_resources[resc])
        if resources:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Resources for %s: %s' %
                       (caller_name(), hostname, repr(resources)))
            # Return assigned resources for specified host
            return resources
        else:
            pbs.logmsg(pbs.EVENT_JOB_USAGE, "WARNING: job seems "
                       + "to have no resources assigned to this host.")
            pbs.logmsg(pbs.EVENT_JOB_USAGE,
                       "Server and MoM vnode names may not be consistent.")
            pbs.logmsg(pbs.EVENT_JOB_USAGE,
                       "Pattern for expected vnode name(s) is %s"
                       % vnhost_pattern)
            pbs.logmsg(pbs.EVENT_JOB_USAGE,
                       "Job exec_vnode is %s" % str(self.job.exec_vnode))
            pbs.logmsg(pbs.EVENT_JOB_USAGE,
                       "You may have forgotten to set PBS_MOM_NODE_NAME to "
                       "the desired matching entry in the exec_vnode string")
            pbs.logmsg(pbs.EVENT_JOB_USAGE,
                       "Job will fail or be configured with default ncpus/mem")
            return {}


#
# CLASS NodeUtils
#
class NodeUtils(object):
    """
    Node utility methods
    NOTE: Multiple log messages pertaining to devices have been commented
          out due to the size of the messages. They may be uncommented for
          additional debugging if necessary.
    """

    def __init__(self, cfg, hostname=None, cpuinfo=None, meminfo=None,
                 numa_nodes=None, devices=None):
        self.cfg = cfg
        if hostname is not None:
            self.hostname = hostname
        else:
            self.hostname = pbs.get_local_nodename()
        if cpuinfo is not None:
            self.cpuinfo = cpuinfo
        else:
            self.cpuinfo = self._discover_cpuinfo()
        if meminfo is not None:
            self.meminfo = meminfo
        else:
            self.meminfo = self._discover_meminfo()
        if numa_nodes is not None:
            self.numa_nodes = numa_nodes
        else:
            self.numa_nodes = dict()
            self.numa_nodes = self._discover_numa_nodes()
        if devices is not None:
            self.devices = devices
        elif self.cfg['cgroup']['devices']['enabled']:
            self.devices = self._discover_devices()
        else:
            self.devices = {}
        # Add the devices count i.e. nmics and ngpus to the numa nodes
        self._add_device_counts_to_numa_nodes()
        # Information for offlining nodes
        self.offline_file = os.path.join(PBS_MOM_HOME, 'mom_priv', 'hooks',
                                         ('%s.offline' %
                                          pbs.event().hook_name))
        self.offline_msg = 'Hook %s: ' % pbs.event().hook_name
        self.offline_msg += 'Unable to clean up one or more cgroups'

    def __repr__(self):
        return ('NodeUtils(%s, %s, %s, %s, %s, %s)' %
                (repr(self.cfg),
                 repr(self.hostname),
                 repr(self.cpuinfo),
                 repr(self.meminfo),
                 repr(self.numa_nodes),
                 repr(self.devices)))

    def _add_device_counts_to_numa_nodes(self):
        """
        Update the device counts per numa node
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        for dclass in self.devices:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Device class: %s' %
                       (caller_name(), dclass))
            if dclass == 'mic' or dclass == 'gpu':
                for inst in self.devices[dclass]:
                    numa_node = self.devices[dclass][inst]['numa_node']
                    if dclass == 'mic' and inst.find('mic') != -1:
                        if 'nmics' not in self.numa_nodes[numa_node]:
                            self.numa_nodes[numa_node]['nmics'] = 1
                        else:
                            self.numa_nodes[numa_node]['nmics'] += 1
                    elif dclass == 'gpu':
                        if 'ngpus' not in self.numa_nodes[numa_node]:
                            self.numa_nodes[numa_node]['ngpus'] = 1
                        else:
                            self.numa_nodes[numa_node]['ngpus'] += 1
        pbs.logmsg(pbs.EVENT_DEBUG4, 'NUMA nodes: %s' % (self.numa_nodes))
        return

    def _discover_numa_nodes(self):
        """
        Discover what type of hardware is on this node and how it
        is partitioned
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        numa_nodes = {}
        for node in glob.glob(os.path.join(os.sep, 'sys', 'devices',
                                           'system', 'node', 'node*')):
            # The basename will be node0, node1, etc.
            # Capture the numeric portion as the identifier/ordinal.
            num = int(os.path.basename(node)[4:])
            if num not in numa_nodes:
                numa_nodes[num] = {}
                numa_nodes[num]['devices'] = []
            exclude = expand_list(self.cfg['cgroup']['cpuset']['exclude_cpus'])
            with open(os.path.join(node, 'cpulist'), 'r') as desc:
                avail = expand_list(desc.readline())
                numa_nodes[num]['cpus'] = [
                    x for x in avail if x not in exclude]
            with open(os.path.join(node, 'meminfo'), 'r') as desc:
                for line in desc:
                    # Each line will contain four or five fields. Examples:
                    # Node 0 MemTotal:       32995028 kB
                    # Node 0 HugePages_Total:     0
                    entries = line.split()
                    if len(entries) < 4:
                        continue
                    if entries[2] == 'MemTotal:':
                        numa_nodes[num]['MemTotal'] = \
                            convert_size(entries[3] + entries[4], 'kb')
                    elif entries[2] == 'HugePages_Total:':
                        numa_nodes[num]['HugePages_Total'] = int(entries[3])
        # Adjust NUMA nodes wrt reserved memory resource
        # note that the get_* routines only work because this routine
        # is called in the constructor; that constructor first
        # sets self.numa_nodes to the empty dict, which triggers
        # the get_* routines to use the non-NUMA section
        num_numa_nodes = len(numa_nodes)
        if self.cfg['vnode_per_numa_node'] and num_numa_nodes > 0:
            # Huge page memory
            host_hpmem = self.get_hpmem_on_node(ignore_reserved=True)
            host_resv_hpmem = host_hpmem - self.get_hpmem_on_node()
            if host_resv_hpmem < 0:
                host_resv_hpmem = 0
            node_resv_hpmem = int(math.ceil(host_resv_hpmem / num_numa_nodes))
            if 'Hugepagesize' in self.meminfo:
                node_resv_hpmem -= \
                    (node_resv_hpmem
                     % size_as_int(self.meminfo['Hugepagesize']))
            # Physical memory
            host_mem = self.get_memory_on_node(ignore_reserved=True)
            host_mem_net = self.get_memory_on_node(ignore_reserved=False)
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: gross mem = %s, net mem = %s'
                       % (caller_name(), host_mem, host_mem_net))
            host_resv_mem = host_mem - host_mem_net
            if host_resv_mem < 0:
                host_resv_mem = 0
            node_resv_mem = int(math.ceil(host_resv_mem / num_numa_nodes))
            node_resv_mem -= node_resv_mem % (1024 * 1024)
            # Swap to be added to virtual memory
            # net values, taking into account reserved memory
            host_vmem_net = self.get_vmem_on_node(ignore_reserved=False)
            node_swapmem = int(math.floor((host_vmem_net - host_mem_net)
                                          / num_numa_nodes))
            if node_swapmem < 0:
                node_swapmem = 0
            node_swapmem -= node_swapmem % (1024 * 1024)
            # Set the NUMA node values
            for num in numa_nodes:
                val = 0
                if 'HugePages_Total' in numa_nodes[num]:
                    val = numa_nodes[num]['HugePages_Total']
                    if 'Hugepagesize' in self.meminfo:
                        val *= size_as_int(self.meminfo['Hugepagesize'])
                    val -= node_resv_hpmem
                numa_nodes[num]['hpmem'] = val
                val = size_as_int(numa_nodes[num]['MemTotal'])
                # round down only svr-reported values, not internal values
                val -= node_resv_mem
                numa_nodes[num]['mem'] = val
                val += node_swapmem
                # round down only svr-reported values, not internal values
                numa_nodes[num]['vmem'] = val
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: %s' % (caller_name(), numa_nodes))
        return numa_nodes

    def _devinfo(self, path):
        """
        Returns major minor and type from device
        """
        # If the stat fails, log it and continue.
        try:
            statinfo = os.stat(path)
        except OSError:
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Stat error on %s' %
                       (caller_name(), path))
            return None
        major = os.major(statinfo.st_rdev)
        minor = os.minor(statinfo.st_rdev)
        if stat.S_ISCHR(statinfo.st_mode):
            dtype = 'c'
        else:
            dtype = 'b'
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   'Path: %s, Major: %d, Minor: %d, Type: %s' %
                   (path, major, minor, dtype))
        return {'major': major, 'minor': minor, 'type': dtype}

    def _discover_devices(self):
        """
        Identify devices and to which numa nodes they are attached
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        devices = {}
        # First loop identifies all devices and determines their true path,
        # major/minor device IDs, and NUMA node affiliation (if any).
        paths = glob.glob(os.path.join(os.sep, 'sys', 'class', '*', '*'))
        paths.extend(glob.glob(os.path.join(
            os.sep, 'sys', 'bus', 'pci', 'devices', '*')))
        for path in paths:
            # Skip this path if it is not a directory
            if not os.path.isdir(path):
                continue
            dirs = path.split(os.sep)   # Path components
            dclass = dirs[-2]   # Device class
            inst = dirs[-1]   # Device instance
            if dclass not in devices:
                devices[dclass] = {}
            devices[dclass][inst] = {}
            devices[dclass][inst]['realpath'] = os.path.realpath(path)
            # Determine the PCI bus ID of the device
            devices[dclass][inst]['bus_id'] = ''
            if dirs[-3] == 'pci' and dirs[-2] == 'devices':
                devices[dclass][inst]['bus_id'] = dirs[-1]
            # Determine the major and minor device numbers
            filename = os.path.join(devices[dclass][inst]['realpath'], 'dev')
            devices[dclass][inst]['major'] = None
            devices[dclass][inst]['minor'] = None
            if os.path.isfile(filename):
                with open(filename, 'r') as desc:
                    major, minor = list(
                        map(int, desc.readline().strip().split(':')))
                    devices[dclass][inst]['major'] = int(major)
                    devices[dclass][inst]['minor'] = int(minor)
            numa_node = -1
            subdir = os.path.join(devices[dclass][inst]['realpath'], 'device')
            # The numa_node file is not always in the same place
            # so work our way up the path trying to find it.
            while len(subdir.split(os.sep)) > 2:
                filename = os.path.join(subdir, 'numa_node')
                if os.path.isfile(filename):
                    # The file should contain a single integer
                    with open(filename, 'r') as desc:
                        numa_node = int(desc.readline().strip())
                    break
                subdir = os.path.dirname(subdir)
            if numa_node < 0:
                numa_node = 0
            devices[dclass][inst]['numa_node'] = numa_node
        # Second loop determines device types and their location
        # under /dev. Only look for block and character devices.
        for path in find_files(os.path.join(os.sep, 'dev'), kind='bc',
                               follow_mounts=False):
            # If the stat fails, log it and continue.
            devinfo = self._devinfo(path)
            if not devinfo:
                continue

            for dclass in devices:
                for inst in devices[dclass]:
                    if 'type' not in devices[dclass][inst]:
                        devices[dclass][inst]['type'] = None
                    if 'device' not in devices[dclass][inst]:
                        devices[dclass][inst]['device'] = None
                    if devices[dclass][inst]['major'] == devinfo['major']:
                        if devices[dclass][inst]['minor'] == devinfo['minor']:
                            devices[dclass][inst]['type'] = devinfo['type']
                            devices[dclass][inst]['device'] = path
        # Check to see if there are gpus on the node and copy them
        # into their own dictionary.
        devices['gpu'] = {}
        gpus = self._discover_gpus()
        if gpus:
            for dclass in devices:
                for inst in devices[dclass]:
                    for gpuid in gpus:
                        bus_id = devices[dclass][inst]['bus_id'].lower()
                        if bus_id == gpus[gpuid]['pci_bus_id']:
                            devices['gpu'][gpuid] = devices[dclass][inst]
                            # For NVIDIA devices, sysfs doesn't contain a dev
                            # file, so we must get the major, minor and device
                            # type from the matching /dev/nvidia[0-9]*
                            if gpuid.startswith('nvidia'):
                                path = os.path.join(os.sep, 'dev', gpuid)
                                # If the stat fails, continue.
                                devinfo = self._devinfo(path)
                                if not devinfo:
                                    continue
                                devices[dclass][inst]['major'] = \
                                    devinfo['major']
                                devices[dclass][inst]['minor'] = \
                                    devinfo['minor']
                                devices[dclass][inst]['type'] = \
                                    devinfo['type']
                                devices[dclass][inst]['device'] = path
                                devices[dclass][inst]['uuid'] = \
                                    gpus[gpuid]['uuid']
        # if any gpu has mig enabled, let's use them
        if gpus and any(gpus[gpu]['mig'] for gpu in gpus):
            nvidia_cap_major = None
            with open(os.path.join(os.sep, 'proc', 'devices')) as f:
                for line in f:
                    device = line.split()
                    if len(device) != 2:
                        continue
                    if device[1] == 'nvidia-caps':
                        nvidia_cap_major = int(device[0])
                        break
            if nvidia_cap_major is None:
                pbs.logmsg(pbs.EVENT_SYSTEM, '%s: A GPU has MIG enabled, but'
                           'nvidia-caps is not found in /proc/devices. '
                           'Skipping MIG configuration'
                           % caller_name())
            else:
                gis = {}
                # we need major, minor, type, uuids, numa_node
                for gpuid in gpus:
                    if 'gis' not in gpus[gpuid]:
                        continue
                    for giid in gpus[gpuid]['gis']:
                        gi = gpus[gpuid]['gis'][giid]
                        if 'cis' not in gi:
                            # no cis found for this gi, just skip
                            continue
                        name = 'nvidia-cap%d' % gi['minor']
                        numa = devices['gpu'][gpuid]['numa_node']
                        major = devices['gpu'][gpuid]['major']
                        minor = devices['gpu'][gpuid]['minor']
                        # extra_devs are the device numbers of the physical
                        # gpu, as well as the nvidia controller
                        new_gpu = {'major': nvidia_cap_major,
                                   'minor': gi['minor'],
                                   'is_gi': True,
                                   'type': 'c',
                                   'numa_node': numa,
                                   'extra_devs': ['%d:%d' % (major, minor),
                                                  '%d:255' % (major)],
                                   'uuids': []
                                   }
                        for ciid in gi['cis']:
                            ci = gi['cis'][ciid]
                            new_gpu['uuids'].append(ci['uuid'])
                            major = nvidia_cap_major
                            minor = ci['minor']
                            new_gpu['extra_devs'].append(
                                '%d:%d' % (major, minor))
                        devices['gpu'][name] = new_gpu
                    del devices['gpu'][gpuid]

        pbs.logmsg(pbs.EVENT_DEBUG4, 'Processed GPUs: %s' % devices['gpu'])
        if gpus and not devices['gpu']:
            pbs.logmsg(pbs.EVENT_SYSTEM, '%s: GPUs discovered but could not '
                       'be successfully mapped to devices.' % (caller_name()))
        return devices

    def _discover_gpus(self):
        """
        Return a dictionary where the keys are the name of the GPU devices
        and the values are the PCI bus IDs.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        gpus = {}
        if self.cfg['discover_gpus'] and self.cfg['nvidia-smi']:
            cmd = [self.cfg['nvidia-smi'], '-q', '-x']
        else:
            return gpus
        pbs.logmsg(pbs.EVENT_DEBUG4, 'NVIDIA SMI command: %s' % cmd)
        time_start = time.time()
        mig_found = False
        try:
            # Try running the nvidia-smi command
            process = subprocess.Popen(cmd, shell=False,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       universal_newlines=True)
            out = process.communicate()[0]
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Failed to execute: %s' %
                       " ".join(cmd))
            pbs.logmsg(pbs.EVENT_DEBUG3, '%s: No GPUs found' % caller_name())
            return gpus
        elapsed_time = time.time() - time_start
        if elapsed_time > 2.0:
            pbs.logmsg(pbs.EVENT_DEBUG,
                       '%s: nvidia-smi call took %f seconds' %
                       (caller_name(), elapsed_time))
        # if we get a non-str type then convert before calling xmlet
        # should not happen since we passed universal_newlines True
        try:
            # Try parsing the output
            import xml.etree.ElementTree as xmlet
            root = xmlet.fromstring(stringified_output(out))
            pbs.logmsg(pbs.EVENT_DEBUG4, 'root.tag: %s' % root.tag)
            for child in root:
                if child.tag == 'gpu':
                    bus_id = child.get('id')
                    result = re.match(r'([^:]+):(.*)', bus_id)
                    if not result:
                        raise ValueError('GPU ID not recognized: ' + bus_id)
                    domain, instance = result.groups()
                    # Make sure the PCI domain is 16 bits (4 hex digits)
                    if len(domain) == 8:
                        domain = domain[-4:]
                    if len(domain) != 4:
                        raise ValueError('GPU ID not recognized: ' + bus_id)
                    name = 'nvidia%s' % child.find('minor_number').text
                    mig_enabled = False
                    mig_mode = child.find('mig_mode')
                    if mig_mode is not None:
                        current_mig = mig_mode.find('current_mig')
                        if current_mig is not None:
                            if current_mig.text == 'Enabled':
                                mig_found = True
                                mig_enabled = True
                    gpus[name] = {
                        'pci_bus_id': (domain + ':' + instance).lower(),
                        'uuid': child.find('uuid').text,
                        'mig': mig_enabled
                    }
        except Exception as exc:
            pbs.logmsg(pbs.EVENT_DEBUG, 'Unexpected error: %s' % exc)

        # at least one gpu had mig enabled, let's add the gis
        if mig_found:
            self._discover_migs(gpus)

        pbs.logmsg(pbs.EVENT_DEBUG4, 'GPUs: %s' % gpus)
        return gpus

    def _discover_migs(self, gpus):
        """
        Mutate the gpus dictionary with mig info, GIs and CIs
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # find GIs
        cmd = [self.cfg['nvidia-smi'], 'mig', '-lgi']
        pbs.logmsg(pbs.EVENT_DEBUG4, 'NVIDIA SMI command: %s' % cmd)
        time_start = time.time()
        out = []
        try:
            # Try running the nvidia-smi command
            process = subprocess.Popen(cmd, shell=False,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       universal_newlines=True)
            out = process.communicate()[0].split('\n')
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Failed to execute: %s' %
                       " ".join(cmd))
            pbs.logmsg(pbs.EVENT_DEBUG3, '%s: No MIGs found' % caller_name())
            return
        elapsed_time = time.time() - time_start
        if elapsed_time > 2.0:
            pbs.logmsg(pbs.EVENT_DEBUG,
                       '%s: nvidia-smi call took %f seconds' %
                       (caller_name(), elapsed_time))

        # format is
        # | GPUID   NAME       ProfileID  GIID       PLACE     |
        # |====================================================|
        # |   0  MIG 1g.5gb       19        7          4:1     |
        # +----------------------------------------------------+
        # |   0  MIG 1g.5gb       19        8          5:1     |
        r = re.compile(r'^\|\s+(\d+)\s+MIG\s+\S+\s+\d+\s+(\d+)\s+\S+\s+\|$')
        for out_line in out:
            match = r.match(out_line)
            if not match:
                continue
            gpu_num = int(match.group(1))
            gpuid = 'nvidia%d' % gpu_num
            giid = int(match.group(2))
            minor = self._discover_mig_minor(gpu_num, giid, ci=None)
            gi = {'minor': minor, 'gi': giid}
            pbs.logmsg(pbs.EVENT_DEBUG4, 'GI found %s' % str(gi))
            if 'gis' not in gpus[gpuid]:
                gpus[gpuid]['gis'] = {}
            gpus[gpuid]['gis'][giid] = gi

        # now find all CIs
        cmd = [self.cfg['nvidia-smi'], 'mig', '-lci']
        pbs.logmsg(pbs.EVENT_DEBUG4, 'NVIDIA SMI command: %s' % cmd)
        time_start = time.time()
        out = []
        try:
            # Try running the nvidia-smi command
            process = subprocess.Popen(cmd, shell=False,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       universal_newlines=True)
            out = process.communicate()[0].split('\n')
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Failed to execute: %s' %
                       " ".join(cmd))
            pbs.logmsg(pbs.EVENT_DEBUG3, '%s: No MIGs found' % caller_name())
            return
        elapsed_time = time.time() - time_start
        if elapsed_time > 2.0:
            pbs.logmsg(pbs.EVENT_DEBUG,
                       '%s: nvidia-smi call took %f seconds' %
                       (caller_name(), elapsed_time))

        # format is either
        # | Compute instances:                                    |
        # | GPU     GPU       Name             Profile   Instance |
        # |       Instance                       ID        ID     |
        # |         ID                                            |
        # |   0      7       MIG 1g.5gb           0         0     |
        # or
        # | Compute instances:                                             |
        # | GPU     GPU       Name         Profile   Instance   Placement  |
        # |       Instance                   ID        ID       Start:Size |
        # |         ID                                                     |
        # |   0      7       MIG 1g.5gb       0         0          0:1     |
        r = re.compile(
            r'^\|\s+(\d+)\s+(\d+)\s+MIG\s+\S+\s+\d+\s+(\d+)\s+(\S+\s+)?\|$')
        for out_line in out:
            match = r.match(out_line)
            if not match:
                continue
            gpu_num = int(match.group(1))
            gpuid = 'nvidia%d' % gpu_num
            giid = int(match.group(2))
            ciid = int(match.group(3))
            minor = self._discover_mig_minor(gpu_num, giid, ciid)
            if minor == -1:
                continue
            uuid = 'MIG-%s/%s/%s' % (gpus[gpuid]['uuid'], giid, ciid)
            ci = {'minor': minor, 'gi': giid, 'ci': ciid, 'uuid': uuid}
            pbs.logmsg(pbs.EVENT_DEBUG4, 'CI found %s' % str(ci))
            if 'cis' not in gpus[gpuid]['gis'][giid]:
                gpus[gpuid]['gis'][giid]['cis'] = {}
            gpus[gpuid]['gis'][giid]['cis'][ciid] = ci

    def _discover_mig_minor(self, gpu, gi, ci=None):
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        path = os.path.join(os.sep, 'proc', 'driver',
                            'nvidia-caps', 'mig-minors')
        if ci is None:
            capname = 'gpu%d/gi%d/access' % (gpu, gi)
        else:
            capname = 'gpu%d/gi%d/ci%d/access' % (gpu, gi, ci)
        with open(path, 'r') as minors:
            for line in minors:
                cap, minor = line.split(' ', 1)
                if cap == capname:
                    return int(minor)
        pbs.logmsg(pbs.EVENT_DEBUG, 'Cannot find minor number for %s'
                   % capname)
        return -1

    def _discover_meminfo(self):
        """
        Return a dictionary where the keys are the NUMA node ordinals
        and the values are the various memory sizes
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        meminfo = {}
        with open(os.path.join(os.sep, 'proc', 'meminfo'), 'r') as desc:
            for line in desc:
                entries = line.split()
                if entries[0] == 'MemTotal:':
                    meminfo[entries[0].rstrip(':')] = \
                        convert_size(entries[1] + entries[2], 'kb')
                elif entries[0] == 'SwapTotal:':
                    meminfo[entries[0].rstrip(':')] = \
                        convert_size(entries[1] + entries[2], 'kb')
                elif entries[0] == 'Hugepagesize:':
                    meminfo[entries[0].rstrip(':')] = \
                        convert_size(entries[1] + entries[2], 'kb')
                elif entries[0] == 'HugePages_Total:':
                    meminfo[entries[0].rstrip(':')] = int(entries[1])
                elif entries[0] == 'HugePages_Rsvd:':
                    meminfo[entries[0].rstrip(':')] = int(entries[1])
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Discover meminfo: %s' % meminfo)
        return meminfo

    def _discover_cpuinfo(self):
        """
        Return a dictionary where the keys include both global settings
        and individual CPU characteristics
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        cpuinfo = {}
        cpuinfo['cpu'] = {}
        proc = None
        with open(os.path.join(os.sep, 'proc', 'cpuinfo'), 'r') as desc:
            for line in desc:
                entries = line.strip().split(':')
                if len(entries) < 2:
                    # Blank line indicates end of processor
                    proc = None
                    continue
                key = entries[0].strip()
                val = entries[1].strip()
                if proc is None and key != 'processor':
                    raise ProcessingError('Failed to parse /proc/cpuinfo')
                if key == 'processor':
                    proc = int(val)
                    if proc in cpuinfo:
                        raise ProcessingError('Duplicate CPU ID found')
                    cpuinfo['cpu'][proc] = {}
                    cpuinfo['cpu'][proc]['threads'] = []
                elif key == 'flags':
                    cpuinfo['cpu'][proc][key] = val.split()
                elif val.isdigit():
                    cpuinfo['cpu'][proc][key] = int(val)
                else:
                    cpuinfo['cpu'][proc][key] = val
        if not cpuinfo['cpu']:
            raise ProcessingError('No CPU information found')
        cpuinfo['logical_cpus'] = len(cpuinfo['cpu'])
        cpuinfo['hyperthreads_per_core'] = 1
        cpuinfo['hyperthreads'] = []
        # Now try to construct a dictionary with hyperthread information
        # if this is an Intel based processor
        try:
            if ('Intel' in cpuinfo['cpu'][0]['vendor_id']
                    or 'AuthenticAMD' in cpuinfo['cpu'][0]['vendor_id']):
                if 'ht' in cpuinfo['cpu'][0]['flags']:
                    cpuinfo['hyperthreads_per_core'] = \
                        int(cpuinfo['cpu'][0]['siblings']
                            // cpuinfo['cpu'][0]['cpu cores'])
                    # Map hyperthreads to physical cores
                    if cpuinfo['hyperthreads_per_core'] > 1:
                        pbs.logmsg(pbs.EVENT_DEBUG4,
                                   'Mapping hyperthreads to cores')
                        cores = list(cpuinfo['cpu'].keys())
                        threads = set()
                        # CPUs with matching core IDs are hyperthreads
                        # sharing the same physical core. Loop through
                        # the cores to construct a list of threads.
                        for xid in cores:
                            xcore = cpuinfo['cpu'][xid]
                            for yid in cores:
                                if yid < xid:
                                    continue
                                if yid == xid:
                                    cpuinfo['cpu'][xid]['threads'].append(yid)
                                    continue
                                ycore = cpuinfo['cpu'][yid]
                                if xcore['physical id'] != \
                                        ycore['physical id']:
                                    continue
                                if xcore['core id'] == ycore['core id']:
                                    cpuinfo['cpu'][xid]['threads'].append(yid)
                                    cpuinfo['cpu'][yid]['threads'].append(xid)
                                    threads.add(yid)
                        pbs.logmsg(pbs.EVENT_DEBUG4, 'HT cores: %s' % threads)
                        cpuinfo['hyperthreads'] = sorted(threads)
                    else:
                        cores = cpuinfo['cpu'].keys()
                        for xid in cores:
                            cpuinfo['cpu'][xid]['threads'].append(xid)
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG, '%s: Hyperthreading check failed' %
                       caller_name())
        cpuinfo['physical_cpus'] = int(cpuinfo['logical_cpus']
                                       // cpuinfo['hyperthreads_per_core'])
        wanted_keys = ['physical_cpus', 'logical_cpus',
                       'hyperthreads_per_core', 'hyperthreads']
        cpuinfo_short = dict((k, cpuinfo[k])
                             for k in wanted_keys if k in cpuinfo)
        pbs.logmsg(pbs.EVENT_DEBUG4, "%s returning: %s"
                   % (caller_name(), cpuinfo_short))
        if 0 in cpuinfo['cpu']:
            pbs.logmsg(pbs.EVENT_DEBUG4, "%s For CPU 0: cpuinfo['cpu'][0]: %s"
                       % (caller_name(), cpuinfo['cpu'][0]))
        return cpuinfo

    def gather_jobs_on_node(self, cgroup):
        """
        Gather the jobs assigned to this node and local vnodes
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Construct a dictionary where the keys are job IDs and the values
        # are timestamps. The job IDs are collected from the cgroup jobs
        # file and by inspecting MoM's job directory. Both are needed to
        # ensure orphans are properly identified.
        jobdict = cgroup.read_cgroup_jobs()
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   'cgroup_jobs file content: %s' % str(jobdict))
        try:
            for jobfile in glob.glob(os.path.join(PBS_MOM_JOBS, '*.JB')):
                (jobid, dot_jb) = os.path.splitext(os.path.basename(jobfile))
                if jobid not in jobdict:
                    if os.stat_float_times():
                        jobdict[jobid] = os.path.getmtime(jobfile)
                    else:
                        jobdict[jobid] = float(os.path.getmtime(jobfile))
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG, 'Could not get job list for %s' %
                       self.hostname)
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Local job dictionary: %s' % str(jobdict))
        return jobdict

    def get_memory_on_node(self, memtotal=None, ignore_reserved=False):
        """
        Get the memory resource on this mom
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        total = 0
        if self.numa_nodes and self.cfg['vnode_per_numa_node']:
            # Caller wants the sum of all NUMA nodes
            for nnid in self.numa_nodes:
                if 'mem' in self.numa_nodes[nnid]:
                    total += self.numa_nodes[nnid]['mem']
                else:
                    # NUMA node unreliable, make sure other method is used
                    total = 0
                    break
            # only round down svr-reported values, not internal values
            if total > 0:
                return total
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: Failed to obtain memory using NUMA node method' %
                       caller_name())
        # Calculate total memory
        try:
            if memtotal is None:
                total = size_as_int(self.meminfo['MemTotal'])
            else:
                total = size_as_int(memtotal)
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG,
                       '%s: Could not determine total node memory' %
                       caller_name())
            raise
        if total <= 0:
            raise ValueError('Total node memory value invalid')
        pbs.logmsg(pbs.EVENT_DEBUG4, 'total visible mem: %d' % total)
        # Calculate reserved memory
        reserved = 0
        if not ignore_reserved:
            reserve_pct = int(self.cfg['cgroup']['memory']['reserve_percent'])
            reserved += int(total * (reserve_pct / 100.0))
            reserve_amount = self.cfg['cgroup']['memory']['reserve_amount']
            reserved += size_as_int(reserve_amount)
        pbs.logmsg(pbs.EVENT_DEBUG4, 'reserved mem: %d' % reserved)
        # Calculate remaining memory
        remaining = total - reserved
        # only round down svr-reported values, not internal values
        if remaining <= 0:
            raise ValueError('Too much reserved memory')
        pbs.logmsg(pbs.EVENT_DEBUG4, 'remaining mem: %d' % remaining)
        amount = convert_size(str(remaining), 'kb')
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Returning: %s' %
                   (caller_name(), amount))
        return remaining

    def get_vmem_on_node(self, vmemtotal=None, ignore_reserved=False):
        """
        Get the virtual memory resource on this mom
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        total = 0
        # If NUMA nodes were not yet discovered then get totals
        # using non-NUMA methods
        if self.numa_nodes and self.cfg['vnode_per_numa_node']:
            # Caller wants the sum of all NUMA nodes, and they were
            # computed earlier
            for nnid in self.numa_nodes:
                if 'vmem' in self.numa_nodes[nnid]:
                    total += self.numa_nodes[nnid]['vmem']
                else:
                    total = 0
                    break
            # only round down svr-reported values, not internal values
            if total > 0:
                return total
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: Failed to obtain vmem using NUMA node method' %
                       caller_name())
        # Calculate total vmem; start with visible or usable physical memory
        total = self.get_memory_on_node(None, ignore_reserved)
        if ignore_reserved:
            pbs.logmsg(pbs.EVENT_DEBUG4, 'total visible mem: %d' % total)
        else:
            pbs.logmsg(pbs.EVENT_DEBUG4, 'total usable mem: %d' % total)
        # Calculate total swap
        try:
            if vmemtotal is None:
                swap = size_as_int(self.meminfo['SwapTotal'])
            else:
                swap = size_as_int(vmemtotal)
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG,
                       '%s: Could not determine total node swap' %
                       caller_name())
            raise
        if swap <= 0:
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: No swap space detected' %
                       caller_name())
            swap = 0
        pbs.logmsg(pbs.EVENT_DEBUG4, 'total swap: %d' % swap)
        # Calculate reserved swap
        reserved = 0
        if not ignore_reserved:
            reserve_pct = int(self.cfg['cgroup']['memsw']['reserve_percent'])
            reserved += int(swap * (reserve_pct / 100.0))
            reserve_amount = self.cfg['cgroup']['memsw']['reserve_amount']
            reserved += size_as_int(reserve_amount)
            pbs.logmsg(pbs.EVENT_DEBUG4, 'reserved swap: %d' % reserved)
            if reserved > swap:
                reserved = swap
        # Calculate remaining vmem
        remaining = total + swap - reserved
        # only round down svr-reported values, not internal values
        if remaining <= 0:
            raise ValueError('Too much reserved vmem')
        pbs.logmsg(pbs.EVENT_DEBUG4, 'remaining vmem: %d' % remaining)
        amount = convert_size(str(remaining), 'kb')
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Returning: %s' %
                   (caller_name(), amount))
        return remaining

    def get_hpmem_on_node(self, hpmemtotal=None, ignore_reserved=False):
        """
        Get the huge page memory resource on this mom
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        total = 0
        if self.numa_nodes and self.cfg['vnode_per_numa_node']:
            # Caller wants the sum of all NUMA nodes
            for nnid in self.numa_nodes:
                if 'hpmem' in self.numa_nodes[nnid]:
                    total += self.numa_nodes[nnid]['hpmem']
                else:
                    total = 0
                    break
            # only round down svr-reported values, not internal values
            if total > 0:
                return total
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: Failed to obtain memory using NUMA node method' %
                       caller_name())
        # Calculate hpmem
        try:
            if hpmemtotal is None:
                total = size_as_int(self.meminfo['Hugepagesize'])
                total *= (self.meminfo['HugePages_Total'] -
                          self.meminfo['HugePages_Rsvd'])
            else:
                total = size_as_int(hpmemtotal)
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG3,
                       '%s: Could not determine huge page availability' %
                       caller_name())
            total = 0
        if total <= 0:
            total = 0
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: No huge page memory detected' %
                       caller_name())
            return 0
        # Calculate reserved hpmem
        reserved = 0
        if not ignore_reserved:
            reserve_pct = int(self.cfg['cgroup']['hugetlb']['reserve_percent'])
            reserved += int(total * (reserve_pct / 100.0))
            reserve_amount = self.cfg['cgroup']['hugetlb']['reserve_amount']
            reserved += size_as_int(reserve_amount)
        pbs.logmsg(pbs.EVENT_DEBUG4, 'reserved hpmem: %d' % reserved)
        # Calculate remaining vmem
        remaining = total - reserved
        # Round down to nearest huge page
        if 'Hugepagesize' in self.meminfo:
            remaining -= (remaining
                          % (size_as_int(self.meminfo['Hugepagesize'])))
        if remaining <= 0:
            raise ValueError('Too much reserved hpmem')
        pbs.logmsg(pbs.EVENT_DEBUG4, 'remaining hpmem: %d' % remaining)
        amount = convert_size(str(remaining), 'kb')
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Returning: %s' %
                   (caller_name(), amount))
        # Remove any bytes beyond the last MB
        return remaining

    def create_vnodes(self, vntype=None):
        """
        Create individual vnodes per socket
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        vnode_list = pbs.event().vnode_list
        if self.cfg['vnode_per_numa_node']:
            vnodes = True
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: vnode_per_numa_node is enabled' %
                       caller_name())
        else:
            vnodes = False
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: vnode_per_numa_node is disabled' % caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: numa nodes: %s' %
                   (caller_name(), self.numa_nodes))
        vnode_name = self.hostname
        # In some cases the hostname and vnode name do not match
        # admin should fix this!
        # Give hints
        if vnode_name not in vnode_list:
            pbs.logmsg(pbs.EVENT_ERROR,
                       "Could not find hostname %s in vnode_list %s"
                       % (vnode_name, str(vnode_list.keys())))
            pbs.logmsg(pbs.EVENT_ERROR,
                       "This error is FATAL. Possible causes:")
            pbs.logmsg(pbs.EVENT_ERROR, "a) the server's name for "
                       "the natural node created on the server "
                       "does not match the output of 'hostname' on the host.")
            pbs.logmsg(pbs.EVENT_ERROR, "   Please use PBS_MOM_NODE_NAME "
                       "in /etc/pbs.conf to tell MoM the correct vnode name.")
            pbs.logmsg(pbs.EVENT_ERROR, "b) v2 config files are used "
                       "but none mention the natural vnode. Add a line for "
                       "the natural vnode in one of the v2 config files.")
            raise ProcessingError('Could not identify local vnode')
        vnode_list[vnode_name] = pbs.vnode(vnode_name)
        host_resc_avail = vnode_list[vnode_name].resources_available
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: host_resc_avail: %s' %
                   (caller_name(), host_resc_avail))
        # Set resources_available.vntype of natural node according
        # to what's in local file if it needs to be propagated to server
        if (vntype and self.cfg['propagate_vntype_to_server']):
            host_resc_avail['vntype'] = vntype
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: vnode type set to %s'
                       % (caller_name(), vntype))

        vnode_msg_cpu = '%s: vnode_list[%s].resources_available[ncpus] = %d'
        vnode_msg_mem = '%s: vnode_list[%s].resources_available[mem] = %s'

        if not vnodes:
            # memory (global for host)
            mem = self.get_memory_on_node(ignore_reserved=False)
            # remove X MB - handle jitter in MemTotal observed in field
            mem -= (1024 * 1024
                    * self.cfg['cgroup']['memory']['vnode_hidden_mb'])
            if mem < 0:
                mem = 0
            mem -= mem % (1024 * 1024)
            mem = pbs.size(convert_size(mem, 'mb'))
            host_resc_avail['mem'] = mem
            pbs.logmsg(pbs.EVENT_DEBUG4, vnode_msg_mem %
                       (caller_name(), vnode_name,
                        str(host_resc_avail['mem'])))
            # memory+swap ('vmem') (global for host)
            vmem = self.get_vmem_on_node(ignore_reserved=False)
            # remove X MB - handle jitter in MemTotal observed in field
            vmem -= (1024 * 1024
                     * self.cfg['cgroup']['memory']['vnode_hidden_mb'])
            if vmem < 0:
                vmem = 0
            vmem -= vmem % (1024 * 1024)
            # if there is no swap, trying to subtract vnode_hidden_mb
            # will lower vmem under mem, do not allow
            if vmem < mem:
                vmem = mem
            vmem = pbs.size(convert_size(vmem, 'mb'))
            host_resc_avail['vmem'] = vmem
            pbs.logmsg(pbs.EVENT_DEBUG4, vnode_msg_mem %
                       (caller_name(), vnode_name,
                        str(host_resc_avail['mem'])))
            # huge page mem (global for host)
            val = self.get_hpmem_on_node(ignore_reserved=False)
            # remove X MB - handle jitter in mem reported by OS
            val -= (1024 * 1024
                    * self.cfg['cgroup']['hugetlb']
                              ['vnode_hidden_mb'])
            if val < 0:
                val = 0
            val -= val % (1024 * 1024)  # round down to MB
            host_resc_avail['hpmem'] = \
                pbs.size(convert_size(val, 'mb'))
            if (self.cfg['cgroup']['memsw']['enabled']
                    and self.cfg['cgroup']['memsw']['manage_cgswap']):
                # special case 0 cgswap because assigning a "0mb"
                # pbs.size difference causes a bug
                if vmem == mem:
                    host_resc_avail['cgswap'] = pbs.size('0')
                else:
                    host_resc_avail['cgswap'] = (host_resc_avail['vmem']
                                                 - host_resc_avail['mem'])
            else:
                # This unsets it if it was defined earlier and needs to
                # disappear
                host_resc_avail['cgswap'] = None
        else:
            # set the value on the host to 0
            host_resc_avail['mem'] = pbs.size('0')
            host_resc_avail['vmem'] = pbs.size('0')
            host_resc_avail['hpmem'] = pbs.size('0')
            if (self.cfg['cgroup']['memsw']['enabled']
                    and self.cfg['cgroup']['memsw']['manage_cgswap']):
                host_resc_avail['cgswap'] = pbs.size('0')
            # Some kernel drivers "reserve" memory on /proc/meminfo
            # and this is not reflected in the node meminfo; if necessary,
            # adjust the values published as resources_available.mem
            # on the per-socket vnodes by the difference spread over nodes
            adjust_bytes_per_node = 0
            num_numa_nodes = len(self.numa_nodes)
            total_nodemem = 0
            for num in self.numa_nodes:
                total_nodemem += size_as_int(self.numa_nodes[num]['MemTotal'])
            total_hostmem = size_as_int(self.meminfo['MemTotal'])
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: host memtotal %s; node memtotal %s'
                       % (caller_name(),
                          str(total_hostmem), str(total_nodemem)))
            if total_hostmem < total_nodemem:
                adjust_bytes_per_node = \
                    int(math.ceil((total_nodemem - total_hostmem)
                                  / float(num_numa_nodes)))
                pbs.logmsg(pbs.EVENT_DEBUG,
                           '%s: host memtotal %s < node memtotal %s, '
                           'adjusting each vnode mem down by %s bytes'
                           % (caller_name(),
                              str(total_hostmem), str(total_nodemem),
                              str(adjust_bytes_per_node)))
        for nnid in self.numa_nodes:
            if vnodes:
                vnode_key = vnode_name + '[%d]' % nnid
                vnode_list[vnode_key] = pbs.vnode(vnode_name)
                vnode_resc_avail = vnode_list[vnode_key].resources_available
                # ensure that if no devices were discovered that
                # ngpus is set to 0; otherwise MoM may still use stale value
                vnode_resc_avail['ngpus'] = 0
                if (vntype and self.cfg['propagate_vntype_to_server']):
                    vnode_resc_avail['vntype'] = vntype
            for key, val in sorted(self.numa_nodes[nnid].items()):
                if key is None:
                    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: key is None'
                               % caller_name())
                    continue
                if val is None:
                    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: val is None'
                               % caller_name())
                    continue
                pbs.logmsg(pbs.EVENT_DEBUG4, '%s: %s = %s'
                           % (caller_name(), key, val))
                if key in ['MemTotal', 'HugePages_Total']:
                    # Irrelevant: transformed to other keys if vnodes is True
                    # done outside of loop if vnodes is False
                    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: key %s skipped'
                               % (caller_name(), key))
                elif key == 'cpus':
                    threads = len(val)
                    if not self.cfg['use_hyperthreads']:
                        # Do not treat a hyperthread as a core when
                        # use_hyperthreads is false.
                        threads = int(threads
                                      // self.cpuinfo['hyperthreads_per_core'])
                    elif self.cfg['ncpus_are_cores']:
                        # use_hyperthreads and ncpus_are_cores are both true,
                        # advertise only one thread per core
                        threads = int(threads
                                      // self.cpuinfo['hyperthreads_per_core'])
                    if vnodes:
                        # set the value on the host to 0
                        host_resc_avail['ncpus'] = 0
                        pbs.logmsg(pbs.EVENT_DEBUG4, vnode_msg_cpu %
                                   (caller_name(), vnode_name,
                                    host_resc_avail['ncpus']))
                        # set the vnode value
                        vnode_resc_avail['ncpus'] = threads
                        pbs.logmsg(pbs.EVENT_DEBUG4, vnode_msg_cpu %
                                   (caller_name(), vnode_key,
                                    vnode_resc_avail['ncpus']))
                    else:
                        if 'ncpus' not in host_resc_avail:
                            host_resc_avail['ncpus'] = 0
                        if not isinstance(host_resc_avail['ncpus'],
                                          (int, pbs.pbs_int)):
                            host_resc_avail['ncpus'] = 0
                        # update the cumulative value
                        host_resc_avail['ncpus'] += threads
                        pbs.logmsg(pbs.EVENT_DEBUG4, vnode_msg_cpu %
                                   (caller_name(), vnode_name,
                                    host_resc_avail['ncpus']))
                elif key in ['mem', 'vmem', 'hpmem']:
                    # Used for vnodes per NUMA socket
                    if vnodes:
                        mem_val = val
                        if isinstance(val, float):
                            mem_val = int(val)
                        # remove X MB - handle jitter in mem reported by OS
                        key_to_subsys = ({'mem': 'memory',
                                          'vmem': 'memsw',
                                          'hpmem': 'hugetlb'})
                        if key != 'hpmem':
                            mem_val -= adjust_bytes_per_node
                        mem_val -= (1024 * 1024
                                    * self.cfg['cgroup'][key_to_subsys[key]]
                                                        ['vnode_hidden_mb'])
                        if mem_val < 0:
                            mem_val = 0
                        mem_val -= mem_val % (1024 * 1024)
                        vnode_resc_avail[key] = \
                            pbs.size(convert_size(mem_val, 'mb'))
                elif isinstance(val, list):
                    pass
                elif isinstance(val, dict):
                    pass
                else:
                    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: key = %s (%s)' %
                               (caller_name(), key, type(key)))
                    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: val = %s (%s)' %
                               (caller_name(), val, type(val)))
                    if vnodes:
                        vnode_resc_avail[key] = val
                        host_resc_avail[key] = initialize_resource(val)
                    else:
                        if key not in host_resc_avail:
                            host_resc_avail[key] = initialize_resource(val)
                        else:
                            if not host_resc_avail[key]:
                                host_resc_avail[key] = initialize_resource(val)
                        host_resc_avail[key] += val
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: vnode list: %s' %
                   (caller_name(), str(vnode_list)))
        if vnodes:
            for nnid in self.numa_nodes:
                vnode_key = vnode_name + '[%d]' % nnid
                vnode_resc_avail = vnode_list[vnode_key].resources_available
                # vmem can be smaller than mem if there is no swap, since
                # we try to hide 1MB of swap from the server
                if vnode_resc_avail['vmem'] < vnode_resc_avail['mem']:
                    vnode_resc_avail['vmem'] = vnode_resc_avail['mem']
                if (self.cfg['cgroup']['memsw']['enabled']
                        and self.cfg['cgroup']['memsw']['manage_cgswap']):
                    # Special case 0 cgswap since a bug creates a crash when
                    # the "0mb" pbs.size difference is assigned to the resource
                    if vnode_resc_avail['vmem'] == vnode_resc_avail['mem']:
                        vnode_resc_avail['cgswap'] = pbs.size('0')
                    else:
                        vnode_resc_avail['cgswap'] = \
                            vnode_resc_avail['vmem'] - vnode_resc_avail['mem']
                else:
                    # This unsets it if it was defined earlier and needs to
                    # disappear
                    host_resc_avail['cgswap'] = None

                pbs.logmsg(pbs.EVENT_DEBUG4, '%s: %s vnode_resc_avail: %s' %
                           (caller_name(), vnode_key, vnode_resc_avail))
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: host_resc_avail: %s' %
                   (caller_name(), host_resc_avail))
        return True

    def take_node_offline(self):
        """
        Take the local node and associated vnodes offline
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Taking vnode(s) offline' %
                   caller_name())
        # Attempt to take vnodes that match this host offline
        # Assume vnode names resemble self.hostname[#]
        match_found = False
        for vnode_name in pbs.event().vnode_list:
            if (vnode_name == self.hostname or
                    re.match(self.hostname + r'\[.*\]', vnode_name)):
                pbs.event().vnode_list[vnode_name].state = pbs.ND_OFFLINE
                pbs.event().vnode_list[vnode_name].comment = self.offline_msg
                pbs.logmsg(pbs.EVENT_DEBUG2, '%s: %s; offlining %s' %
                           (caller_name(), self.offline_msg, vnode_name))
                match_found = True
        if not match_found:
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: No vnodes match %s' %
                       (caller_name(), self.hostname))
            return
        # Write a file locally to reduce server traffic when the node
        # is brought back online
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Offline file: %s' %
                   (caller_name(), self.offline_file))
        if os.path.isfile(self.offline_file):
            pbs.logmsg(pbs.EVENT_DEBUG2,
                       '%s: Offline file already exists, not overwriting' %
                       caller_name())
            return
        try:
            # Write a timestamp so that exechost_periodic can avoid
            # cleaning up before this event has sent updates to server
            with open(self.offline_file, 'w') as fd:
                fd.write(str(time.time()))
        except Exception as exc:
            pbs.logmsg(pbs.EVENT_DEBUG, '%s: Failed to write to %s: %s' %
                       (caller_name(), self.offline_file, exc))
            pass
        if not os.path.isfile(self.offline_file):
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Offline file not present: %s' %
                       (caller_name(), self.offline_file))
        pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Node taken offline' %
                   caller_name())

    def bring_node_online(self):
        """
        Bring the local node and associated vnodes online
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if not os.path.isfile(self.offline_file):
            pbs.logmsg(pbs.EVENT_DEBUG3, '%s: Offline file not present: %s' %
                       (caller_name(), self.offline_file))
            return
        # Read timestamp from offline file
        timestamp = float()
        try:
            with open(self.offline_file, 'r') as fd:
                timestamp = float(fd.read())
        except Exception as exc:
            pbs.logmsg(pbs.EVENT_DEBUG, '%s: Failed to read from %s: %s' %
                       (caller_name(), self.offline_file, exc))
            return
        # Only bring node online after minimum delay has passed
        delta = time.time() - timestamp
        if delta < float(self.cfg['online_nodes_min_delay']):
            pbs.logmsg(pbs.EVENT_DEBUG2,
                       '%s: Too soon since node was offlined' % caller_name())
            return
        # Get comments for vnodes associated with this event
        vnl = pbs.event().vnode_list.keys()
        (vnode_comments, failure) = \
            fetch_vnode_comments(vnl, timeout=self.cfg['server_timeout'])
        if failure:
            pbs.logmsg(pbs.EVENT_ERROR,
                       '%s: Failed contacting server for vnode comments'
                       % caller_name())
            pbs.logmsg(pbs.EVENT_ERROR, '%s: Not bringing vnodes online'
                       % caller_name())
            return
        # Bring vnodes online that this hook has taken offline
        for vnode_name in vnode_comments:
            if vnode_comments[vnode_name] != self.offline_msg:
                pbs.logmsg(pbs.EVENT_DEBUG, ('%s: Comment for vnode %s '
                                             + 'was not set by this hook')
                           % (caller_name(), vnode_name))
                continue
            vnode = pbs.event().vnode_list[vnode_name]
            vnode.state = pbs.ND_FREE
            vnode.comment = None
            pbs.logmsg(pbs.EVENT_DEBUG,
                       '%s: Vnode %s will be brought back online' %
                       (caller_name(), vnode_name))
        # Remove the offline file
        try:
            os.remove(self.offline_file)
        except Exception as exc:
            pbs.logmsg(pbs.EVENT_DEBUG,
                       '%s: Failed to remove offline file: %s' %
                       (caller_name(), exc))


#
# CLASS CgroupUtils
#
class CgroupUtils(object):
    """
    Cgroup utility methods
    """

    def __init__(self, hostname, vnode, cfg=None, subsystems=None,
                 paths=None, vntype=None, assigned_resources=None,
                 systemd_version=None):
        self.hostname = hostname
        self.vnode = vnode

        # Read in the config file
        if cfg is not None:
            self.cfg = cfg
        else:
            self.cfg = self.parse_config_file()
        # Determine the systemd version (zero for no systemd)
        if systemd_version:
            self.systemd_version = systemd_version
        else:
            self.systemd_version = self._get_systemd_version()
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: systemd version seems to be %d'
                   % (caller_name(), self.systemd_version))
        # Collect the cgroup mount points
        if paths is not None:
            self.paths = paths
        else:
            self.paths = self._get_paths()

        # Define the local vnode type
        if vntype is not None:
            self.vntype = vntype
        else:
            self.vntype = self._get_vnode_type()

        # morph the strings that should become booleans
        # into booleans depending on host/vntype
        self.morph_config_dict_bools(self.cfg)
        pbs.logmsg(pbs.EVENT_DEBUG4, "Final cgroup cfg: %s" % repr(self.cfg))

        # Remove the added "enabled" in the cgroup section
        # it's added at all levels in the cfg because of the recursion,
        # but some of the code assumes only iterables are
        # in the "cgroup" dictionary,
        # without any guards to protect against extras!!
        if "enabled" in self.cfg['cgroup']:
            del self.cfg['cgroup']['enabled']

        if not self.cfg['cgroup']['devices']['enabled']:
            self.cfg['discover_gpus'] = False
            pbs.logmsg(pbs.EVENT_DEBUG, 'discover_gpus set to False because '
                       'devices subsystem is disabled')

        # Determine which subsystems we care about
        if subsystems is not None:
            self.subsystems = subsystems
        else:
            self.subsystems = self._target_subsystems()

        # Check if memsw is enabled despite lack of kernel support
        # if so disable memsw
        if 'memsw' in self.subsystems:
            memsw_limit_file = (self.paths['memsw']
                                + 'limit_in_bytes')
            if not os.path.isfile(memsw_limit_file):
                pbs.logmsg(pbs.EVENT_ERROR,
                           'CONFIG ERROR: CF enables memsw '
                           'but %s is missing'
                           % memsw_limit_file)
                pbs.logmsg(pbs.EVENT_ERROR,
                           'CONFIG ERROR: kernel swapaccount parameter is 0, '
                           'disabling memsw subsystem')
                pbs.logmsg(pbs.EVENT_ERROR,
                           'CONFIG ERROR: to fix this, either add '
                           'swapaccount=1 to kernel command line '
                           'or disable memsw')
                self.cfg['cgroup']['memsw']['enabled'] = False
                self.subsystems.remove('memsw')
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'remaining enabled subsystems: '
                           + repr(self.subsystems))

        # Return now if nothing is enabled
        if not self.subsystems:
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: No cgroups enabled' %
                       caller_name())
            self.assigned_resources = {}
            return

        # Collect the cgroup resources
        if assigned_resources:
            self.assigned_resources = assigned_resources
        else:
            self.assigned_resources = self._get_assigned_cgroup_resources()

        # location to store information for the different hook events
        self.hook_storage_dir = os.path.join(PBS_MOM_HOME, 'mom_priv',
                                             'hooks', 'hook_data')

        if not os.path.isdir(self.hook_storage_dir):
            try:
                os.makedirs(self.hook_storage_dir, 0o700)
            except OSError:
                pbs.logmsg(pbs.EVENT_DEBUG, 'Failed to create %s' %
                           self.hook_storage_dir)
        self.host_job_env_dir = os.path.join(PBS_MOM_HOME, 'aux')
        self.host_job_env_filename = os.path.join(self.host_job_env_dir,
                                                  '%s.env')
        # Temporarily stores list of new jobs that came after job_list was
        # written to mom hook input file (work around for periodic
        # and begin race condition)
        self.cgroup_jobs_file = os.path.join(self.hook_storage_dir,
                                             'cgroup_jobs')
        if not os.path.isfile(self.cgroup_jobs_file):
            self.empty_cgroup_jobs_file()

    def __repr__(self):
        return ('CgroupUtils(%s, %s, %s, %s, %s, %s, %s, %s)' %
                (repr(self.hostname),
                 repr(self.vnode),
                 repr(self.cfg),
                 repr(self.subsystems),
                 repr(self.paths),
                 repr(self.vntype),
                 repr(self.assigned_resources),
                 repr(self.systemd_version)))

    def write_to_stderr(self, job, msg):
        """
        Write a message to the job stderr file
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        filename = None
        try:
            if str(job.Join_Path) == 'oe':
                # If we write in stderr we might do it after the join!
                # so write in stdout instead
                filename = job.stdout_file()
            else:
                filename = job.stderr_file()
            pbs.logmsg(pbs.EVENT_DEBUG4, "Message to be written to %s: %s"
                       % (str(filename), msg.strip()))
            if filename is None:
                return
            with open(filename, 'a') as desc:
                desc.write(msg)
        except Exception:
            # tough luck for user, but not fatal to system
            # Note: sometimes normal (e.g. interactive jobs)
            pass

    def _target_subsystems(self):
        """
        Determine which subsystems are being requested
        Note that config file parsing has set "enabled" to false/true
        if necessary by now, no need to delve in config dictionary
        since self.enabled will discover it
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Check to see if this node is in the approved hosts list
        subsystems = []
        for key in self.cfg['cgroup']:
            if self.enabled(key):
                subsystems.append(key)
        # Add an entry for systemd if anything else is enabled. This allows
        # the hook to cleanup any directories systemd leaves behind.
        # Add at start since we want this processed first
        if subsystems and self.systemd_version >= 205:
            subsystems.insert(0, 'systemd')
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Enabled subsystems: %s' %
                   (caller_name(), subsystems))
        # It is not an error for all subsystems to be disabled.
        # This host or vnode type may be in the excluded list.
        return subsystems

    def _copy_from_parent(self, dest):
        """
        Copy a setting from the parent cgroup
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        filename = os.path.basename(dest)
        subdir = os.path.dirname(dest)
        parent = os.path.dirname(subdir)
        source = os.path.join(parent, filename)
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   'Copying value from %s to %s'
                   % (source, dest))
        if not os.path.isfile(source):
            raise CgroupConfigError('Failed to read %s' % (source))
        with open(source, 'r') as desc:
            self.write_value(dest, desc.read().strip())

    def _assemble_path(self, subsys, mnt_point, flags):
        """
        Determine the path for a cgroup directory given the subsystem, mount
        point, and mount flags
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if 'noprefix' in flags:
            prefix = ''
        else:
            if subsys == 'hugetlb':
                # hugetlb includes size in prefix
                # TODO: make size component configurable
                prefix = subsys + '.2MB.'
            elif subsys == 'memsw':
                prefix = 'memory.' + subsys + '.'
            elif subsys in ['systemd', 'perf_event']:
                prefix = ''
            else:
                prefix = subsys + '.'
        return os.path.join(mnt_point,
                            str(self.cfg['cgroup_prefix'])
                            + '.service/jobid',
                            prefix)

    def _get_paths(self):
        """
        Create a dictionary of the cgroup subsystems and their corresponding
        directories taking mount options (noprefix) into account
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        paths = {}
        subsys_to_discover = {'blkio', 'cpu', 'cpuacct', 'cpuset', 'devices',
                              'freezer', 'hugetlb', 'memory', 'net_cls',
                              'net_prio', 'perf_event', 'pids', 'rdma',
                              'systemd'}
        subsys_to_skip = set()

        # First deal with the ones with paths set in the configuration file
        for subsys in subsys_to_discover:
            if (subsys in self.cfg['cgroup']
                    and 'mount_path' in self.cfg['cgroup'][subsys]
                    and self.cfg['cgroup'][subsys]['mount_path']):
                flags = []
                if (subsys == 'cpuset'
                        and os.path.exists(os.path.join(self.cfg['cgroup']
                                                                ['cpuset']
                                                                ['mount_path'],
                                                        'cpus'))):
                    flags = ['noprefix']
                paths[subsys] = \
                    self._assemble_path(subsys,
                                        self.cfg['cgroup'][subsys]
                                                ['mount_path'],
                                        flags)
                # memory -> memsw, different prefix
                if (subsys == 'memory'):
                    paths['memsw'] = \
                        self._assemble_path(subsys,
                                            self.cfg['cgroup']['memory']
                                                    ['mount_path'],
                                            flags)
                subsys_to_skip.add(subsys)
        subsys_to_discover -= subsys_to_skip

        # Loop through the mounts and collect the ones for cgroups
        with open(os.path.join(os.sep, 'proc', 'mounts'), 'r') as desc:
            for line in desc:
                entries = line.split()
                if entries[2] != 'cgroup':
                    continue
                # It is possible to have more than one cgroup mounted in
                # the same place, so check them all for each mount.
                flags = entries[3].split(',')
                for subsys in subsys_to_discover:
                    if (subsys in flags
                            or (subsys == 'systemd'
                                and ('name=systemd' in flags))):
                        subsys_path_candidate = \
                            self._assemble_path(subsys, entries[1], flags)
                        # If there are more than one option, prefer shortest
                        if (subsys not in paths
                            or (len(paths[subsys])
                                > len(subsys_path_candidate))):
                            if subsys in paths:
                                pbs.logmsg(pbs.EVENT_DEBUG3,
                                           '_get_paths: shorter path+prefix '
                                           '%s used for subsystem %s'
                                           % (subsys_path_candidate, subsys))
                            if subsys == 'memory':
                                # memory and memsw share a common mount point,
                                # but use a different prefix
                                paths['memsw'] = \
                                    self._assemble_path('memsw', entries[1],
                                                        flags)
                            paths[subsys] = subsys_path_candidate

        # if a host does not have any cgroup controllers mounted
        # don't panic here, let main code handle it by just accepting event

        return paths

    def _cgroup_path(self, subsys, cgfile='', jobid=''):
        """
        Return the path to a cgroup file or directory
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Note: The tasks file never uses a prefix (e.g. use tasks and not
        # cpuset.tasks).
        # Note: The os.path.join() method is smart enough to ignore
        # empty strings unless they occur as the last parameter.
        if not subsys or subsys not in self.paths:
            return None
        try:
            subdir, prefix = os.path.split(self.paths[subsys])
        except Exception:
            return None
        if not cgfile:
            if jobid:
                # Caller wants parent directory of job
                return os.path.join(subdir, jobid, '')
            # Caller wants job parent directory for subsystem
            return os.path.join(subdir, '')
        # Caller wants full path to file
        if cgfile == 'tasks':
            # tasks file never uses a prefix
            return os.path.join(subdir, jobid,
                                cgfile)
        if jobid:
            return os.path.join(subdir, jobid,
                                prefix + cgfile)
        return os.path.join(subdir, prefix + cgfile)

    def morph_config_dict_bools(self, config_dict, subsection=None):
        """
        function to morph 'vntype in: vntype1,vntype2,...'
        and 'vntype not in: ...' values
        into booleans depending on the vntype in the CgroupConfig object
        Ditto for 'host in:' and 'host not in:'
        Convert old run_only_on_hosts and exclude_vntypes to derive
        'enabled' boolean (which will now _always_ be defined)
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        vntype = self.vntype
        # avoid crashes in fnmatch if no vntype was specified on the host
        if not vntype:
            vntype = "__UNKNOWN__"
        # get a 'basestring' type that identifies all string types
        # already exists in Python 2 (has both std and unicode strings)
        # but not in Python 3, which only has "str" (always unicode)
        try:
            basestring
        except NameError:
            basestring = str
        # We want a view/iterator object, not something that creates copied
        # lists. iteritems/viewitems() no longer exists in Py3
        # since items() itself is now implemented as a view object.
        dict_iter_object = (config_dict.iteritems() if PYTHON2
                            else config_dict.items())
        for key_found, value_found in dict_iter_object:
            if isinstance(value_found, dict):
                # subsection found
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           "%s: Cfg file parsing, subsection for %s"
                           % (caller_name(), key_found))
                self.morph_config_dict_bools(value_found, key_found)
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           "%s: Cfg file parsing, finished subsection for %s"
                           % (caller_name(), key_found))
            elif isinstance(value_found, basestring):
                # string value -- could be morphable description
                value_split = value_found.strip().split(':', 1)
                if (len(value_split) > 1
                        and value_split[0].lower().strip() == 'vntype in'):
                    vntypes_t_unstripped = value_split[1].split(',')
                    vntypes_t = [item.strip() for item in vntypes_t_unstripped]
                    if any([fnmatch.fnmatch(vntype, p) for p in vntypes_t]):
                        config_dict[key_found] = True
                    else:
                        config_dict[key_found] = False
                    pbs.logmsg(pbs.EVENT_DEBUG4,
                               "%s: Config file parsing,"
                               " set %s to %s based on vntype inclusion"
                               % (caller_name(), key_found,
                                  str(config_dict[key_found])))
                elif (len(value_split) > 1
                        and value_split[0].lower().strip() == 'vntype not in'):
                    vntypes_f_unstripped = value_split[1].split(',')
                    vntypes_f = [item.strip() for item in vntypes_f_unstripped]
                    if any([fnmatch.fnmatch(vntype, p) for p in vntypes_f]):
                        config_dict[key_found] = False
                    else:
                        config_dict[key_found] = True
                    pbs.logmsg(pbs.EVENT_DEBUG4,
                               "%s: Config file parsing,"
                               " set %s to %s based on vntype exclusion"
                               % (caller_name(), key_found,
                                  str(config_dict[key_found])))
                elif (len(value_split) > 1
                        and value_split[0].lower().strip() == 'host in'):
                    hosts_t_unstripped = value_split[1].split(',')
                    hosts_t = [item.strip() for item in hosts_t_unstripped]
                    if any([fnmatch.fnmatch(self.hostname, p)
                            for p in hosts_t]):
                        config_dict[key_found] = True
                    else:
                        config_dict[key_found] = False
                    pbs.logmsg(pbs.EVENT_DEBUG4,
                               "%s: Config file parsing,"
                               " set %s to %s based on host inclusion"
                               % (caller_name(), key_found,
                                  str(config_dict[key_found])))
                elif (len(value_split) > 1
                        and value_split[0].lower().strip() == 'host not in'):
                    hosts_f_unstripped = value_split[1].split(',')
                    hosts_f = [item.strip() for item in hosts_f_unstripped]
                    if any([fnmatch.fnmatch(self.hostname, p)
                            for p in hosts_f]):
                        config_dict[key_found] = False
                    else:
                        config_dict[key_found] = True
                    pbs.logmsg(pbs.EVENT_DEBUG4,
                               "%s: Config file parsing,"
                               " set %s to %s based on host exclusion"
                               % (caller_name(), key_found,
                                  str(config_dict[key_found])))

        # Old "exclude_vntypes" now modulates current "enabled"
        # (if present) or creates it if non-empty
        if ("exclude_vntypes" in config_dict
                and config_dict['exclude_vntypes']):
            if ("enabled" not in config_dict
                    or not isinstance(config_dict['enabled'], bool)):
                config_dict['enabled'] = False
            if (config_dict['enabled'] and
                    any([fnmatch.fnmatch(vntype, p)
                         for p in config_dict['exclude_vntypes']])):
                config_dict['enabled'] = False
                if subsection is None:
                    subname = 'all subsystems'
                else:
                    subname = 'subsystem ' + subsection
                pbs.logmsg(pbs.EVENT_DEBUG,
                           '%s: cgroup excluded for '
                           '%s on vnode type %s' %
                           (caller_name(), subname, vntype))
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           '%s is in the excluded vnode type list: %s' %
                           (vntype, config_dict['exclude_vntypes']))
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       "%s: Config file parsing, "
                       "set %s to %s based on exclude_vntypes"
                       % (caller_name(), 'enabled',
                          str(config_dict['enabled'])))

        # Old "exclude_hosts" now modulates current "enabled" (if present)
        # or creates enabled if it is non-empty
        if ("exclude_hosts" in config_dict
                and config_dict['exclude_hosts']):
            if ("enabled" not in config_dict
                    or not isinstance(config_dict['enabled'], bool)):
                config_dict['enabled'] = False
            if (config_dict['enabled'] and
                any([fnmatch.fnmatch(self.hostname, p)
                     for p in config_dict['exclude_hosts']])):
                config_dict['enabled'] = False
                if subsection is None:
                    subname = 'all subsystems'
                else:
                    subname = 'subsystem ' + subsection
                pbs.logmsg(pbs.EVENT_DEBUG,
                           '%s: cgroup excluded for '
                           '%s on host %s' %
                           (caller_name(), subname, self.hostname))
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           '%s is in the excluded host list: %s' %
                           (self.hostname, config_dict['exclude_hosts']))
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       "%s: Config file parsing, "
                       "set %s to %s based on exclude_hosts"
                       % (caller_name(), 'enabled',
                          str(config_dict['enabled'])))
        # mirror image include_hosts of old "exclude_hosts"
        # now modulates current "enabled" (if present)
        # or creates enabled if it is non-empty
        if ("include_hosts" in config_dict
                and config_dict['include_hosts']):
            if ("enabled" not in config_dict
                    or not isinstance(config_dict['enabled'], bool)):
                config_dict['enabled'] = \
                    (any([fnmatch.fnmatch(self.hostname, p)
                          for p in config_dict['include_hosts']]))
            else:
                config_dict['enabled'] = \
                    (config_dict['enabled']
                     or any([fnmatch.fnmatch(self.hostname, p)
                             for p in config_dict['include_hosts']]))
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       "%s: Config file parsing, "
                       "set %s to %s based on include_hosts"
                       % (caller_name(), 'enabled',
                          str(config_dict['enabled'])))

        # Add "disabled" if unspecified
        if "enabled" not in config_dict:
            config_dict['enabled'] = False
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       "%s: Config file parsing, "
                       "section disabled by default"
                       % caller_name())

        # Old "run_only_on_hosts" limits enabled hosts if present
        if ("run_only_on_hosts" in config_dict
                and config_dict['run_only_on_hosts']):
            config_dict['enabled'] = \
                (config_dict['enabled']
                 and any([fnmatch.fnmatch(self.hostname, p)
                          for p in config_dict['run_only_on_hosts']]))
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       "%s: Config file parsing,"
                       " set %s to %s based on run_only_on_hosts"
                       % (caller_name(), 'enabled',
                          str(config_dict['enabled'])))

    @staticmethod
    def parse_config_file():
        """
        Read the config file in json format
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Turn everything off by default. These settings be modified
        # when the configuration file is read. Keep the keys in sync
        # with the default cgroup configuration files.
        defaults = {}
        defaults['enabled'] = True
        defaults['cgroup_prefix'] = 'pbs_jobs'
        defaults['cgroup_lock_file'] = os.path.join(PBS_MOM_HOME, 'mom_priv',
                                                    'cgroups.lock')
        defaults['discover_gpus'] = True
        defaults['nvidia-smi'] = os.path.join(os.sep, 'usr', 'bin',
                                              'nvidia-smi')
        defaults['exclude_hosts'] = []
        defaults['exclude_vntypes'] = []
        defaults['run_only_on_hosts'] = []
        defaults['periodic_resc_update'] = False
        defaults['vnode_per_numa_node'] = False
        defaults['online_offlined_nodes'] = False
        defaults['online_nodes_min_delay'] = 30
        defaults['use_hyperthreads'] = False
        defaults['ncpus_are_cores'] = False
        defaults['kill_timeout'] = 10
        defaults['server_timeout'] = 15
        defaults['job_setup_timeout'] = 30
        defaults['placement_type'] = 'load_balanced'
        defaults['propagate_vntype_to_server'] = True
        defaults['manage_rlimit_as'] = True
        defaults['cgroup'] = {}
        defaults['cgroup']['cpu'] = {}
        defaults['cgroup']['cpu']['enabled'] = False
        defaults['cgroup']['cpu']['exclude_hosts'] = []
        defaults['cgroup']['cpu']['exclude_vntypes'] = []
        defaults['cgroup']['cpu']['cfs_period_us'] = 100000
        defaults['cgroup']['cpu']['cfs_quota_fudge_factor'] = 1.03
        defaults['cgroup']['cpu']['enforce_per_period_quota'] = False
        # For zero_cpus_shares_fraction, the default value of 0.002 * 1000 = 2,
        # which is the smallest value allowed by kernel as cpu.shares
        defaults['cgroup']['cpu']['zero_cpus_shares_fraction'] = 0.002
        defaults['cgroup']['cpu']['zero_cpus_quota_fraction'] = 0.2
        defaults['cgroup']['blkio'] = {}
        defaults['cgroup']['blkio']['enabled'] = False
        defaults['cgroup']['cpuacct'] = {}
        defaults['cgroup']['cpuacct']['enabled'] = False
        defaults['cgroup']['cpuacct']['exclude_hosts'] = []
        defaults['cgroup']['cpuacct']['exclude_vntypes'] = []
        defaults['cgroup']['cpuset'] = {}
        defaults['cgroup']['cpuset']['enabled'] = False
        defaults['cgroup']['cpuset']['exclude_cpus'] = []
        defaults['cgroup']['cpuset']['exclude_hosts'] = []
        defaults['cgroup']['cpuset']['exclude_vntypes'] = []
        defaults['cgroup']['cpuset']['mem_fences'] = True
        defaults['cgroup']['cpuset']['mem_hardwall'] = False
        defaults['cgroup']['cpuset']['memory_spread_page'] = False
        defaults['cgroup']['cpuset']['allow_zero_cpus'] = True
        defaults['cgroup']['devices'] = {}
        defaults['cgroup']['devices']['enabled'] = False
        defaults['cgroup']['devices']['exclude_hosts'] = []
        defaults['cgroup']['devices']['exclude_vntypes'] = []
        defaults['cgroup']['devices']['allow'] = []
        defaults['cgroup']['memory'] = {}
        defaults['cgroup']['memory']['enabled'] = False
        defaults['cgroup']['memory']['exclude_hosts'] = []
        defaults['cgroup']['memory']['exclude_vntypes'] = []
        defaults['cgroup']['memory']['soft_limit'] = False
        defaults['cgroup']['memory']['default'] = '0MB'
        defaults['cgroup']['memory']['enforce_default'] = True
        defaults['cgroup']['memory']['exclhost_ignore_default'] = False
        defaults['cgroup']['memory']['reserve_percent'] = 0
        defaults['cgroup']['memory']['reserve_amount'] = '0MB'
        defaults['cgroup']['memory']['vnode_hidden_mb'] = 1
        defaults['cgroup']['memory']['swappiness'] = 1
        defaults['cgroup']['memsw'] = {}
        defaults['cgroup']['memsw']['enabled'] = False
        defaults['cgroup']['memsw']['exclude_hosts'] = []
        defaults['cgroup']['memsw']['exclude_vntypes'] = []
        defaults['cgroup']['memsw']['default'] = '0MB'
        defaults['cgroup']['memsw']['enforce_default'] = True
        defaults['cgroup']['memsw']['exclhost_ignore_default'] = False
        defaults['cgroup']['memsw']['reserve_percent'] = 0
        defaults['cgroup']['memsw']['reserve_amount'] = '0MB'
        defaults['cgroup']['memsw']['vnode_hidden_mb'] = 1
        defaults['cgroup']['memsw']['manage_cgswap'] = False
        defaults['cgroup']['hugetlb'] = {}
        defaults['cgroup']['hugetlb']['enabled'] = False
        defaults['cgroup']['hugetlb']['exclude_hosts'] = []
        defaults['cgroup']['hugetlb']['exclude_vntypes'] = []
        defaults['cgroup']['hugetlb']['default'] = '0MB'
        defaults['cgroup']['hugetlb']['enforce_defaults'] = True
        defaults['cgroup']['hugetlb']['exclhost_ignore_default'] = False
        defaults['cgroup']['hugetlb']['reserve_percent'] = 0
        defaults['cgroup']['hugetlb']['reserve_amount'] = '0MB'
        defaults['cgroup']['hugetlb']['vnode_hidden_mb'] = 1

        # These are unmanaged -- if enabled only creation/removal
        # and filling in 'tasks' are currently performed
        defaults['cgroup']['blkio'] = {}
        defaults['cgroup']['blkio']['enabled'] = False
        defaults['cgroup']['freezer'] = {}
        defaults['cgroup']['freezer']['enabled'] = False
        defaults['cgroup']['net_cls'] = {}
        defaults['cgroup']['net_cls']['enabled'] = False
        defaults['cgroup']['net_prio'] = {}
        defaults['cgroup']['net_prio']['enabled'] = False
        defaults['cgroup']['perf_event'] = {}
        defaults['cgroup']['perf_event']['enabled'] = False
        defaults['cgroup']['pids'] = {}
        defaults['cgroup']['pids']['enabled'] = False
        defaults['cgroup']['rdma'] = {}
        defaults['cgroup']['rdma']['enabled'] = False

        # Identify the config file and read in the data
        config_file = ''
        if 'PBS_HOOK_CONFIG_FILE' in os.environ:
            config_file = os.environ['PBS_HOOK_CONFIG_FILE']
        if not config_file:
            tmpcfg = os.path.join(PBS_MOM_HOME, 'mom_priv', 'hooks',
                                  'pbs_cgroups.CF')
            if os.path.isfile(tmpcfg):
                config_file = tmpcfg
        if not config_file:
            tmpcfg = os.path.join(PBS_HOME, 'server_priv', 'hooks',
                                  'pbs_cgroups.CF')
            if os.path.isfile(tmpcfg):
                config_file = tmpcfg
        if not config_file:
            tmpcfg = os.path.join(PBS_MOM_HOME, 'mom_priv', 'hooks',
                                  'pbs_cgroups.json')
            if os.path.isfile(tmpcfg):
                config_file = tmpcfg
        if not config_file:
            tmpcfg = os.path.join(PBS_HOME, 'server_priv', 'hooks',
                                  'pbs_cgroups.json')
            if os.path.isfile(tmpcfg):
                config_file = tmpcfg
        if not config_file:
            raise CgroupConfigError('Config file not found')
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Config file is %s' %
                   (caller_name(), config_file))
        try:
            with open(config_file, 'r') as desc:
                config = merge_dict(defaults,
                                    json.load(desc, object_hook=decode_dict))
            # config file entries denotes reserved _swap_
            # but vnode_hidden_mb used in code for vmem relies
            # on total for physical plus swap (i.e. memsw)
            config['cgroup']['memsw']['vnode_hidden_mb'] += \
                config['cgroup']['memory']['vnode_hidden_mb']

        except IOError:
            raise CgroupConfigError('I/O error reading config file')
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   '%s: cgroup hook configuration: %s' %
                   (caller_name(), config))
        config['cgroup_prefix'] = systemd_escape(config['cgroup_prefix'])
        return config

    def _create_service(self):
        """
        Create the pbs_jobs.service systemd service
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if self.systemd_version < 205:
            return
        if (pbs.event().type == pbs.EXECJOB_BEGIN):
            # normally service is there for this event -- if so, return
            try:
                cmd = ['systemctl', 'is-active', self.cfg['cgroup_prefix']]
                process = subprocess.Popen(cmd, shell=False,
                                           stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE,
                                           universal_newlines=True)
                out = process.communicate()[0]
                if (process.returncode == 0):
                    # service is already active -- no need to create it
                    return
                else:
                    pbs.logmsg(pbs.EVENT_DEBUG,
                               '%s: systemctl is-active <svc> return code %s'
                               % (caller_name(), process.returncode))
            except Exception:
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           '%s: Failed to call systemctl is-active'
                           % caller_name())
                # was worth a try -- try to create service now
                # and see if that fails
                pass
        description = 'PBS Pro job parent service'
        servicefile = os.path.join(os.sep, 'run', 'systemd', 'system',
                                   self.cfg['cgroup_prefix'] + '.service')
        try:
            with open(servicefile, 'w') as desc:
                desc.write('[Unit]\n'
                           'Description=%s\n'
                           '[Service]\n'
                           'Type=simple\n'
                           'Slice=-.slice\n'
                           'TasksMax=infinity\n'
                           'RemainAfterExit=yes\n'
                           'ExecStart=/bin/sleep infinity\n'
                           'ExecStop=/bin/true\n'
                           'CPUAccounting=off\n'
                           'MemoryAccounting=off\n'
                           'Delegate=yes'
                           % description)
                desc.truncate()
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG, '%s: Failed to write service file: %s'
                       % (caller_name(), servicefile))
            raise
        try:
            cmd = ['systemctl', 'start', os.path.basename(servicefile)]
            process = subprocess.Popen(cmd, shell=False,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       universal_newlines=True)
            out, err = process.communicate()
            if (process.returncode == 0):
                pbs.logmsg(pbs.EVENT_DEBUG,
                           '%s: Started systemd service with file %s'
                           % (caller_name(), servicefile))
            else:
                pbs.logmsg(pbs.EVENT_ERROR,
                           '%s: systemctl start <svc> return code %s'
                           % (caller_name(), process.returncode))
                pbs.logmsg(pbs.EVENT_ERROR, '%s: stderr of systemctl was: %s'
                           % (caller_name(),
                              stringified_output(err)))
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG,
                       '%s: Failed to start systemd service: %s'
                       % (caller_name(), os.path.basename(servicefile)))
            raise

    def create_paths(self):
        """
        Create the cgroup parent directories that will contain the jobs
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        old_umask = os.umask(0o022)
        try:
            # Create a systemd service for PBS jobs (if necessary)
            self._create_service()
            # Create the directories that PBS will use to house the jobs
            # now under the controller mount at <prefix>.service/jobid
            for subsys in self.subsystems:
                subdir = self._cgroup_path(subsys)
                if not subdir:
                    raise CgroupConfigError('No path for subsystem: %s'
                                            % (subsys))
                created = False
                if not os.path.exists(subdir):
                    os.makedirs(subdir, 0o755)
                    created = True
                    pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Created directory %s' %
                               (caller_name(), subdir))
                if (not created and pbs.event().type == pbs.EXECJOB_BEGIN):
                    # only exechost_startup configures values in the cgroups
                    # if they exist
                    continue
                if subsys == 'memory' or subsys == 'memsw':
                    # Enable 'use_hierarchy' for memory when either memory
                    # or memsw is in use.
                    filename = self._cgroup_path('memory', 'use_hierarchy')
                    if not os.path.isfile(filename):
                        raise CgroupConfigError('Failed to configure %s' %
                                                (filename))
                    try:
                        self.write_value(filename, 1)
                    except CgroupBusyError:
                        # Some kernels do not like the value written when
                        # other jobs are running, or if the parent already
                        # enabled use_hierarchy.
                        # But then things were already set up anyway
                        pass
                elif subsys == 'cpuset':
                    # copy <cgroup_prefix>.services cpuset values
                    # from its parent (root), then copy those to
                    # directory under it; necessary if systemd is not there
                    (cpuset_dir, cpus_file) = \
                        os.path.split(self._cgroup_path(subsys, 'cpus'))
                    (memset_dir, mems_file) = \
                        os.path.split(self._cgroup_path(subsys, 'mems'))
                    cpuset_pardir = \
                        os.path.dirname(cpuset_dir)
                    self._copy_from_parent(os.path.join(cpuset_pardir,
                                                        cpus_file))
                    self._copy_from_parent(os.path.join(cpuset_pardir,
                                                        mems_file))
                    self._copy_from_parent(self._cgroup_path(subsys, 'cpus'))
                    self._copy_from_parent(self._cgroup_path(subsys, 'mems'))
        except Exception as exc:
            raise CgroupConfigError('Failed to create cgroup paths: %s' % exc)
        finally:
            os.umask(old_umask)

    def _get_vnode_type(self):
        """
        Return the vnode type of the local node
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # self.vnode is not defined for pbs_attach events so the vnode
        # type gets cached in the mom_priv/vntype file. First, check
        # to see if it is defined.
        resc_vntype = ''
        if self.vnode is not None:
            if 'vntype' in self.vnode.resources_available:
                if self.vnode.resources_available['vntype']:
                    resc_vntype = self.vnode.resources_available['vntype']
        pbs.logmsg(pbs.EVENT_DEBUG4, 'resc_vntype: %s' % resc_vntype)
        # Next, read it from the cache file.
        file_vntype = ''
        filename = os.path.join(PBS_MOM_HOME, 'mom_priv', 'vntype')
        try:
            with open(filename, 'r') as desc:
                file_vntype = desc.readline().strip()
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: Failed to read vntype file %s' %
                       (caller_name(), filename))
        pbs.logmsg(pbs.EVENT_DEBUG4, 'file_vntype: %s' % file_vntype)
        # If vntype was not set then log a message. It is too expensive
        # to have all moms query the server for large jobs.
        if not resc_vntype and not file_vntype:
            pbs.logmsg(pbs.EVENT_DEBUG3,
                       '%s: Could not determine vntype' % caller_name())
            return None
        # Return file_vntype if it is set and resc_vntype is not.
        if not resc_vntype and file_vntype:
            pbs.logmsg(pbs.EVENT_DEBUG4, 'vntype: %s' % file_vntype)
            return file_vntype
        # Make sure the cache file is up to date.
        if resc_vntype and resc_vntype != file_vntype:
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Updating vntype file')
            try:
                with open(filename, 'w') as desc:
                    desc.write(resc_vntype)
            except Exception:
                pbs.logmsg(pbs.EVENT_DEBUG2,
                           '%s: Failed to update vntype file %s' %
                           (caller_name(), filename))
        pbs.logmsg(pbs.EVENT_DEBUG4, 'vntype: %s' % resc_vntype)
        return resc_vntype

    def _get_assigned_cgroup_resources(self):
        """
        Return a dictionary of currently assigned cgroup resources per job
        """
        # if jobs have a limit *exactly* equal to the total limit available
        # for all jobs, then they have no explicit memory req and the
        # limit is the result of an 'unlimited' default;
        # in that case, the scheduler assigned 0, so be consistent with it

        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        assigned = {}

        # Get totals available for all jobs to compare with later
        mem_avail = None
        filename = self._cgroup_path('memory', 'limit_in_bytes')
        if filename and os.path.isfile(filename):
            with open(filename) as desc:
                mem_avail = int(desc.readline())
        vmem_avail = None
        filename = self._cgroup_path('memsw', 'limit_in_bytes')
        if filename and os.path.isfile(filename):
            with open(filename) as desc:
                vmem_avail = int(desc.readline())
        hpmem_avail = None
        filename = self._cgroup_path('hugetlb', 'limit_in_bytes')
        if filename and os.path.isfile(filename):
            with open(filename) as desc:
                hpmem_avail = int(desc.readline())
        pbs.logmsg(pbs.EVENT_DEBUG4, "get_assigned_cgroup_resources:"
                   " total host avail mem=%s vmem=%s hpmem=%s"
                   % (mem_avail, vmem_avail, hpmem_avail))

        for key in self.paths:
            if key in ('blkio', 'cpu', 'cpuacct', 'freezer',
                       'net_cls', 'net_prio', 'perf_event',
                       'pids', 'rdma', 'systemd'):
                continue
            if not self.enabled(key):
                continue
            path = os.path.dirname(self._cgroup_path(key))
            # do not exclude orphans
            pattern = self._glob_subdir_wildcard()
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Examining %s' %
                       (caller_name(), os.path.join(path, pattern)))
            for subdir in glob.glob(os.path.join(path, pattern)):
                jobid = os.path.basename(subdir)
                if not jobid:
                    continue
                pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Job ID is %s' %
                           (caller_name(), jobid))
                if jobid not in assigned:
                    assigned[jobid] = {}
                if key not in assigned[jobid]:
                    assigned[jobid][key] = {}
                if key == 'cpuset':
                    with open(self._cgroup_path(key, 'cpus', jobid)) as desc:
                        assigned[jobid][key]['cpus'] = \
                            expand_list(desc.readline())
                    with open(self._cgroup_path(key, 'mems', jobid)) as desc:
                        assigned[jobid][key]['mems'] = \
                            expand_list(desc.readline())
                elif key == 'memory':
                    filename = self._cgroup_path(key, 'limit_in_bytes', jobid)
                    if filename and os.path.isfile(filename):
                        with open(filename) as desc:
                            assigned[jobid][key]['limit_in_bytes'] = \
                                int(desc.readline())
                        if assigned[jobid][key]['limit_in_bytes'] == mem_avail:
                            # not explicitly assigned, 'unlimited' raised dflt
                            assigned[jobid][key]['limit_in_bytes'] = 0
                    filename = self._cgroup_path(key,
                                                 'soft_limit_in_bytes', jobid)
                    if filename and os.path.isfile(filename):
                        with open(filename) as desc:
                            assigned[jobid][key]['soft_limit_in_bytes'] = \
                                int(desc.readline())
                        if (assigned[jobid][key]['soft_limit_in_bytes']
                                == mem_avail):
                            # not explicitly assigned, 'unlimited' raised dflt
                            assigned[jobid][key]['soft_limit_in_bytes'] = 0
                elif key == 'memsw':
                    filename = self._cgroup_path(key, 'limit_in_bytes', jobid)
                    if filename and os.path.isfile(filename):
                        with open(filename) as desc:
                            assigned[jobid][key]['limit_in_bytes'] = \
                                int(desc.readline())
                        if (assigned[jobid][key]['limit_in_bytes']
                                == vmem_avail):
                            # not explicitly assigned, 'unlimited' raised dflt
                            assigned[jobid][key]['limit_in_bytes'] = 0
                    else:
                        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: No such file: %s' %
                                   (caller_name(), filename))
                elif key == 'hugetlb':
                    filename = self._cgroup_path(key, 'limit_in_bytes', jobid)
                    if filename and os.path.isfile(filename):
                        with open(filename) as desc:
                            assigned[jobid][key]['limit_in_bytes'] = \
                                int(desc.readline())
                        if (assigned[jobid][key]['limit_in_bytes']
                                == hpmem_avail):
                            # not explicitly assigned, 'unlimited' raised dflt
                            assigned[jobid][key]['limit_in_bytes'] = 0
                elif key == 'devices':
                    path = self._cgroup_path(key, 'list', jobid)
                    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Devices path is %s' %
                               (caller_name(), path))
                    if path and os.path.isfile(path):
                        with open(path) as desc:
                            assigned[jobid][key]['list'] = []
                            for line in desc:
                                pbs.logmsg(pbs.EVENT_DEBUG4, '%s: '
                                           'Appending %s'
                                           % (caller_name(), line))
                                assigned[jobid][key]['list'].append(line)
                                pbs.logmsg(pbs.EVENT_DEBUG4,
                                           '%s: assigned[%s][%s][list] '
                                           '= %s'
                                           % (caller_name(), jobid, key,
                                              assigned[jobid][key]['list']))
                # Note: unmanaged but known subsystems should be filtered
                # at the top of the loop body
                else:
                    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Unknown subsystem %s' %
                               (caller_name(), key))
                    raise CgroupConfigError('Unknown subsystem: %s' % key)
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Returning %s' %
                   (caller_name(), str(assigned)))
        return assigned

    def _get_systemd_version(self):
        """
        Return an integer reflecting the systemd version, zero for no systemd
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        ver = 0
        try:
            process = subprocess.Popen(['systemctl', '--system', 'show',
                                        '--property=Version'],
                                       shell=False,
                                       stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE,
                                       universal_newlines=True)
            out = process.communicate()[0]
            # if we get a non-str type then convert
            # before calling splitlines
            # should not happen since we pass universal_newlines True
            # Note: some versions prepend "Version=", other versions
            # add a more precise version in parentheses after a space
            out_split = stringified_output(out).splitlines()
            ver = int(re.sub(r'Version=\D*(\d+)', r'\1',
                             out_split[0].split()[0]))
        except Exception:
            # Note we also get here if we can't decode an int version
            return 0
        if not os.path.exists(
                os.path.join(os.sep, 'run', 'systemd', 'system')):
            # no place to drop unit files? systemd not running in system mode?
            return 0
        return ver

    def _glob_subdir_wildcard(self, extension=''):
        """
        Return a string that may be used as a pattern with glob.glob
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        buf = '[0-9]*'
        if extension:
            buf += '.' + extension
        return buf

    def enabled(self, subsystem):
        """
        Return whether a subsystem is enabled.
        if we get here OS is supported: if a controller is enabled
        in the cfg file but the controller is not mounted,
        it is a configuration error that should be fixed
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Check whether the subsystem is enabled in the configuration file
        if subsystem not in self.cfg['cgroup']:
            return False
        if ('enabled' in self.cfg
                and isinstance(self.cfg['enabled'], bool)
                and not self.cfg['enabled']):
            return False
        if ('enabled' not in self.cfg['cgroup'][subsystem]
                or not self.cfg['cgroup'][subsystem]['enabled']):
            return False
        # Check whether the cgroup is mounted for this subsystem
        if subsystem not in self.paths:
            raise CgroupConfigError('%s: cgroups enabled '
                                    'but not mounted for subsystem %s'
                                    % (caller_name(), subsystem))
            return False
        return True

    def default(self, subsystem):
        """
        Return the default value for a subsystem
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if subsystem in self.cfg['cgroup']:
            if 'default' in self.cfg['cgroup'][subsystem]:
                return self.cfg['cgroup'][subsystem]['default']
        return None

    def _is_pid_owner(self, pid, job_uid):
        """
        Check to see if the pid's owner matches the job's owner
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        try:
            proc_uid = os.stat('/proc/%d' % pid).st_uid
        except OSError:
            pbs.logmsg(pbs.EVENT_DEBUG, 'Unknown pid: %d' % pid)
            return False
        except Exception as exc:
            pbs.logmsg(pbs.EVENT_DEBUG, 'Unexpected error: %s' % exc)
            return False
        pbs.logmsg(pbs.EVENT_DEBUG4, '/proc/%d uid:%d' % (pid, proc_uid))
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Job uid: %d' % job_uid)
        if proc_uid != job_uid:
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Proc uid: %d != Job owner: %d' %
                       (proc_uid, job_uid))
            return False
        return True

    def _get_pids_in_sid(self, sid=None):
        """
        Return a list of all PIDS associated with a session ID
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        pids = []
        if not sid:
            return pids
        # Older kernels will not have a task directory
        if os.path.isdir(os.path.join(os.sep, 'proc', 'self', 'task')):
            check_tasks = True
        else:
            check_tasks = False
        pattern = os.path.join(os.sep, 'proc', '[0-9]*', 'stat')
        for filename in glob.glob(pattern):
            try:
                with open(filename, 'r') as desc:
                    entries = desc.readline().split(' ')
                    if int(entries[5]) != sid:
                        continue
                    if check_tasks:
                        # Thread group leader will have a task entry.
                        # No need to append entry from stat file.
                        taskdir = os.path.join(os.path.dirname(filename),
                                               'task')
                        for task in glob.glob(os.path.join(taskdir, '[0-9]*')):
                            pid = int(os.path.basename(task))
                            if pid not in pids:
                                pids.append(pid)
                    else:
                        # Append entry from stat file
                        if int(entries[0]) not in pids:
                            pids.append(int(entries[0]))
            except (OSError, IOError):
                # PIDs may come and go as we read /proc so the glob data can
                # become stale. Tolerate failures in this case.
                pass
        return pids

    def add_pids(self, pidarg, jobid):
        """
        Add some number of PIDs to the cgroup tasks files for each subsystem
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # make pids a list
        pids = []
        if isinstance(pidarg, int):
            sid = 0
            try:
                sid = os.getsid(pidarg)
            except OSError as exc:
                sid = -1
                pbs.logmsg(pbs.EVENT_DEBUG2,
                           '%s: Request to attach session of non-existing'
                           ' PID %s to jobid %s'
                           % (caller_name(), pidarg, jobid))
            if (sid > 1):
                pids = self._get_pids_in_sid(sid)
        elif isinstance(pidarg, list):
            for pid in pidarg:
                if not isinstance(pid, int):
                    raise ValueError('PID list must contain integers')
            pids = pidarg
        else:
            raise ValueError('PID argument must be integer or list')
        if not pids:
            return
        if pbs.event().type == pbs.EXECJOB_LAUNCH:
            if 1 in pids:
                pbs.logmsg(pbs.EVENT_DEBUG2,
                           '%s: Job %s contains defunct process' %
                           (caller_name(), jobid))
                # Use a list comprehension to remove all instances of the
                # number 1
                pids = [x for x in pids if x != 1]
        if not pids:
            return
        # check pids to make sure that they are owned by the job owner
        if pbs.event().type == pbs.EXECJOB_ATTACH:
            pbs.logmsg(pbs.EVENT_DEBUG4, 'event type: attach')
            try:
                uid = pwd.getpwnam(pbs.event().job.euser).pw_uid
            except Exception:
                pbs.logmsg(pbs.EVENT_DEBUG2,
                           'Failed to lookup UID by name')
                raise
            tmp_pids = []
            for process in pids:
                if self._is_pid_owner(process, uid):
                    tmp_pids.append(process)
                else:
                    pbs.logmsg(pbs.EVENT_DEBUG2,
                               'process %d not owned by %s' %
                               (process, uid))
            pids = tmp_pids
        if not pids:
            return
        # Determine which subsystems will be used
        for subsys in self.subsystems:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: subsys = %s' %
                       (caller_name(), subsys))
            # memsw and memory use the same tasks file
            if subsys == 'memsw' and 'memory' in self.subsystems:
                continue
            tasks_file = self._cgroup_path(subsys, 'tasks', jobid)
            if ((not os.path.exists(tasks_file))
                    and (subsys == 'cpuset')):
                if (self.cfg['cgroup']['cpuset']['enabled']
                        and not self.cfg['cgroup']['cpuset']
                                        ['allow_zero_cpus']):
                    pbs.logmsg(pbs.EVENT_JOB_USAGE,
                               'No cpuset cgroup present '
                               'and npcus==0 disallowed')
                    pbs.event().reject('No cpuset cgroup present '
                                       'and npcus==0 disallowed')
                else:
                    # zero CPU job: should be attached to root pbs cpuset
                    tasks_file = self._cgroup_path(subsys, 'tasks')
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: tasks file = %s' %
                       (caller_name(), tasks_file))
            try:
                for process in pids:
                    self.write_value(tasks_file, process, 'a')
            except IOError as exc:
                raise CgroupLimitError('Failed to add PIDs %s to %s (%s)' %
                                       (str(pids), tasks_file,
                                        errno.errorcode[exc.errno]))
            except Exception:
                raise

    def setup_job_devices_env(self, gpus):
        """
        Setup the job environment for the devices assigned to the job for an
        execjob_launch hook
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if 'devices' in self.subsystems:
            # prevent using GPUs without user awareness
            pbs.event().env['CUDA_VISIBLE_DEVICES'] = ''
        if 'device_names' in self.assigned_resources:
            names = self.assigned_resources['device_names']
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'devices: %s' % (names))
            offload_devices = []
            cuda_visible_devices = []
            for name in names:
                if name.startswith('mic'):
                    offload_devices.append(name[3:])
                elif name.startswith('nvidia'):
                    if 'uuid' in gpus[name]:
                        cuda_visible_devices.append(gpus[name]['uuid'])
                    if 'uuids' in gpus[name]:
                        cuda_visible_devices.extend(gpus[name]['uuids'])
            if offload_devices:
                value = "\\,".join(offload_devices)
                pbs.event().env['OFFLOAD_DEVICES'] = '%s' % value
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'offload_devices: %s' % offload_devices)
            if cuda_visible_devices:
                value = "\\,".join(cuda_visible_devices)
                pbs.event().env['CUDA_VISIBLE_DEVICES'] = '%s' % value
                pbs.event().env['CUDA_DEVICE_ORDER'] = 'PCI_BUS_ID'
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'cuda_visible_devices: %s' % cuda_visible_devices)
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'Environment: %s' % pbs.event().env)
            return [offload_devices, cuda_visible_devices]
        else:
            return False

    def _setup_subsys_devices(self, jobid, node):
        """
        Configure access to devices given the job ID and node resources
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if 'devices' not in self.subsystems:
            return
        devices_list_file = self._cgroup_path('devices', 'list', jobid)
        devices_deny_file = self._cgroup_path('devices', 'deny', jobid)
        devices_allow_file = self._cgroup_path('devices', 'allow', jobid)
        # Add devices the user is granted access to
        with open(devices_list_file, 'r') as desc:
            devices_allowed = desc.read().splitlines()
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Initial devices.list: %s' %
                   devices_allowed)
        # Deny access to mic and gpu devices
        accelerators = []
        devices = node.devices
        for devclass in devices:
            if devclass == 'mic' or devclass == 'gpu':
                for instance in devices[devclass]:
                    dev = devices[devclass][instance]
                    if 'extra_devs' in dev:
                        accelerators.extend(dev['extra_devs'])
                    accelerators.append('%d:%d' % (dev['major'], dev['minor']))
        # For CentOS 7 we need to remove a *:* rwm from devices.list
        # before we can add anything to devices.allow. Otherwise our
        # changes are ignored. Check to see if a *:* rwm is in devices.list
        # If so remove it
        value = 'a *:* rwm'
        if value in devices_allowed:
            self.write_value(devices_deny_file, value)
        # Verify that the following devices are not in devices.list
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Removing access to the following: %s' %
                   accelerators)
        for entry in accelerators:
            value = 'c %s rwm' % entry
            self.write_value(devices_deny_file, value)
        # Add devices back to the list
        devices_allow = self.cfg['cgroup']['devices']['allow']
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   'Allowing access to the following: %s' %
                   devices_allow)
        for item in devices_allow:
            if isinstance(item, str):
                pbs.logmsg(pbs.EVENT_DEBUG4, 'string item: %s' % item)
                self.write_value(devices_allow_file, item)
                pbs.logmsg(pbs.EVENT_DEBUG4, 'write_value: %s' % value)
                continue
            if not isinstance(item, list):
                pbs.logmsg(pbs.EVENT_DEBUG2,
                           '%s: Entry is not a string or list: %s' %
                           (caller_name(), item))
                continue
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Device allow: %s' % item)
            stat_filename = os.path.join(os.sep, 'dev', item[0])
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Stat file: %s' % stat_filename)
            try:
                statinfo = os.stat(stat_filename)
            except OSError:
                pbs.logmsg(pbs.EVENT_DEBUG,
                           '%s: Entry not added to devices.allow: %s' %
                           (caller_name(), item))
                pbs.logmsg(pbs.EVENT_DEBUG4, '%s: File not found: %s' %
                           (caller_name(), stat_filename))
                continue
            except Exception as exc:
                pbs.logmsg(pbs.EVENT_DEBUG, 'Unexpected error: %s' % exc)
                continue
            device_type = None
            if stat.S_ISBLK(statinfo.st_mode):
                device_type = 'b'
            elif stat.S_ISCHR(statinfo.st_mode):
                device_type = 'c'
            if not device_type:
                pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Unknown device type: %s' %
                           (caller_name(), stat_filename))
                continue
            if len(item) == 3 and isinstance(item[2], str):
                value = '%s %s:%s %s' % (device_type,
                                         os.major(statinfo.st_rdev),
                                         item[2], item[1])
            else:
                value = '%s %s:%s %s' % (device_type,
                                         os.major(statinfo.st_rdev),
                                         os.minor(statinfo.st_rdev),
                                         item[1])
            self.write_value(devices_allow_file, value)
            pbs.logmsg(pbs.EVENT_DEBUG4, 'write_value: %s' % value)
        with open(devices_list_file, 'r') as desc:
            devices_allowed = desc.read().splitlines()
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Updated devices.list: %s' %
                   devices_allowed)

    def _assign_devices(self, device_kind, device_list, device_count, node):
        """
        Select devices to assign to the job
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        devices = device_list[:device_count]
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Device List: %s' % devices)
        device_names = []
        device_allowed = []
        for dev in devices:
            # Skip device if already present in names
            if dev in device_names:
                continue
            # Skip device if already present in allowed
            device_info = node.devices[device_kind][dev]
            dev_entry = '%s %d:%d rwm' % (device_info['type'],
                                          device_info['major'],
                                          device_info['minor'])
            if dev_entry in device_allowed:
                continue
            # Device controllers must also be added for certain devices
            if device_kind == 'mic':
                # Requires the ctrl (0) and the scif (1) to be added
                dev_entry = '%s %d:0 rwm' % (device_info['type'],
                                             device_info['major'])
                if dev_entry not in device_allowed:
                    device_allowed.append(dev_entry)
                dev_entry = '%s %d:1 rwm' % (device_info['type'],
                                             device_info['major'])
                if dev_entry not in device_allowed:
                    device_allowed.append(dev_entry)
            elif device_kind == 'gpu' and not device_info.get('is_gi', False):
                # Requires the ctrl (255) to be added
                # unless GPU is a GI from a MIG GPU
                dev_entry = '%s %d:255 rwm' % (device_info['type'],
                                               device_info['major'])
                if dev_entry not in device_allowed:
                    device_allowed.append(dev_entry)
            # Now append the device name and entry
            device_names.append(dev)
            dev_entry = '%s %d:%d rwm' % (device_info['type'],
                                          device_info['major'],
                                          device_info['minor'])
            if dev_entry not in device_allowed:
                device_allowed.append(dev_entry)
            # if there are extra majors/minors, add them too
            if 'extra_devs' in device_info:
                for extra in device_info['extra_devs']:
                    dev_entry = '%s %s rwm' % (device_info['type'],
                                               extra)
                    if dev_entry not in device_allowed:
                        device_allowed.append(dev_entry)

        return device_names, device_allowed

    def get_device_name(self, node, available, socket, major, minor):
        """
        Find the device name
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   'Get device name: major: %s, minor: %s' % (major, minor))
        if not isinstance(major, int):
            return None
        if not isinstance(minor, int):
            return None
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   'Possible devices: %s' % (available[socket]['devices']))
        for avail_device in available[socket]['devices']:
            avail_major = None
            avail_minor = None
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'Checking device: %s' % (avail_device))
            if avail_device.find('mic') != -1:
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Check mic device: %s' % (avail_device))
                avail_major = node.devices['mic'][avail_device]['major']
                avail_minor = node.devices['mic'][avail_device]['minor']
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Device major: %s, minor: %s' % (major, minor))
            elif avail_device.find('nvidia') != -1:
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Check gpu device: %s' % (avail_device))
                avail_major = node.devices['gpu'][avail_device]['major']
                avail_minor = node.devices['gpu'][avail_device]['minor']
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Device major: %s, minor: %s' % (major, minor))
            if avail_major == major and avail_minor == minor:
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Device match: name: %s, major: %s, minor: %s' %
                           (avail_device, major, minor))
                return avail_device
        pbs.logmsg(pbs.EVENT_DEBUG4, 'No match found')
        return None

    def _combine_resources(self, dict1, dict2):
        """
        Take two dictionaries containing known types and combine them together
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        dest = {}
        for src in [dict1, dict2]:
            for key in src:
                val = src[key]
                vtype = type(val)
                if key not in dest:
                    if vtype is int:
                        dest[key] = 0
                    elif vtype is float:
                        dest[key] = 0.0
                    elif vtype is str:
                        dest[key] = ''
                    elif vtype is list:
                        dest[key] = []
                    elif vtype is dict:
                        dest[key] = {}
                    elif vtype is tuple:
                        dest[key] = ()
                    elif vtype is pbs.size:
                        dest[key] = pbs.size(0)
                    elif vtype is pbs.int:
                        dest[key] = pbs.int(0)
                    elif vtype is pbs.float:
                        dest[key] = pbs.float(0.0)
                    else:
                        raise ValueError('Unrecognized resource type')
                dest[key] += val
        return dest

    def _assign_resources(self, requested, available, socketlist, node):
        """
        Determine whether a job fits within resources
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        assigned = {'cpuset.cpus': [], 'cpuset.mems': []}
        if 'ncpus' in requested and int(requested['ncpus']) > 0:
            cores = set(available['cpus'])
            if not self.cfg['use_hyperthreads']:
                # Hyperthreads are excluded from core list
                cores -= set(node.cpuinfo['hyperthreads'])
            avail = len(cores)
            needed = int(requested['ncpus'])
            if self.cfg['use_hyperthreads'] and self.cfg['ncpus_are_cores']:
                needed *= node.cpuinfo['hyperthreads_per_core']
            if needed > avail:
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           '%s: insufficient ncpus: %s needed, %s available'
                           % (caller_name(), needed, avail))
                return {}
            if self.cfg['use_hyperthreads']:
                # Find cores that are fully available
                empty_cores = set()
                for corenum in sorted(cores):
                    if (corenum not in node.cpuinfo['hyperthreads']
                            and set(node.cpuinfo['cpu'][corenum]['threads'])
                            .issubset(set(available['cpus']))):
                        # All hyperthreads available for this core
                        empty_cores.add(corenum)
                # Assign threads from the empty cores
                for corenum in sorted(empty_cores):
                    for thread in node.cpuinfo['cpu'][corenum]['threads']:
                        if thread in assigned['cpuset.cpus']:
                            # this thread is already assigned
                            continue
                        assigned['cpuset.cpus'].append(thread)
                        needed -= 1
                        if thread in cores:
                            cores.remove(thread)
                        if needed <= 0:
                            break
                    if needed <= 0:
                        break
            # When use_hyperthreads is enabled, the above block already
            # assigned all of the fully avaiable cores. There still may
            # be cores to assign. When use_hyperthreads is disabled, we
            # assign all the cores here.
            if needed <= 0:
                # we're done (will always hold if use_hyperthreads
                # and ncpus_are_cores are both True)
                pass
            elif needed <= len(cores):
                corelist = sorted(cores)
                assigned['cpuset.cpus'] += corelist[:needed]
            else:
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           '%s: in final pass, '
                           '%d more ncpus needed than available'
                           % (caller_name(), needed - len(cores)))
                return {}

            # Set cpuset.mems to the socketlist for now even though
            # there may not be sufficient memory. Memory gets
            # checked later in this method.
            assigned['cpuset.mems'] = socketlist
        if 'mem' in requested or 'ngpus' in requested or 'nmics' in requested:
            # for multivnode systems asking memory/ngpus/nmics without asking
            # cpus is valid, need access to memory
            assigned['cpuset.mems'] = socketlist
        if 'nmics' in requested and int(requested['nmics']) > 0:
            assigned['device_names'] = []
            assigned['devices'] = []
            regex = re.compile('.*(mic).*')
            nmics = int(requested['nmics'])
            # Use a list comprehension to construct the mics list
            mics = [m.group(0)
                    for d in available['devices']
                    for m in [regex.search(d)] if m]
            if nmics > len(mics):
                pbs.logmsg(pbs.EVENT_DEBUG4, 'Insufficient nmics: %s/%s' %
                           (nmics, mics))
                return {}
            names, devices = self._assign_devices('mic', mics[:nmics],
                                                  nmics, node)
            for val in names:
                assigned['device_names'].append(val)
            for val in devices:
                assigned['devices'].append(val)
        if 'ngpus' in requested and int(requested['ngpus']) > 0:
            if 'device_names' not in assigned:
                assigned['device_names'] = []
                assigned['devices'] = []
            regex = re.compile('.*(nvidia).*')
            ngpus = int(requested['ngpus'])
            # Use a list comprehension to construct the gpus list
            gpus = [m.group(0)
                    for d in available['devices']
                    for m in [regex.search(d)] if m]
            if ngpus > len(gpus):
                pbs.logmsg(pbs.EVENT_DEBUG4, 'Insufficient ngpus: %s/%s' %
                           (ngpus, gpus))
                return {}
            names, devices = self._assign_devices('gpu', gpus[:ngpus],
                                                  ngpus, node)
            for val in names:
                assigned['device_names'].append(val)
            for val in devices:
                assigned['devices'].append(val)
        if 'mem' in requested:
            req_mem = size_as_int(requested['mem'])
            avail_mem = available['memory']
            if req_mem > avail_mem:
                if self.cfg['vnode_per_numa_node']:
                    # scheduler is not supposed to let this happen
                    debug_level = pbs.EVENT_ERROR
                else:
                    # can happen if chunk needs to be split over sockets
                    debug_level = pbs.EVENT_DEBUG4
                pbs.logmsg(debug_level,
                           ('Insufficient memory on socket(s) '
                            '%s: requested:%s, available:%s') %
                           (socketlist, req_mem, available['memory']))
                return {}
            if 'mem' not in assigned:
                assigned['mem'] = 0
            assigned['mem'] += req_mem
            if 'cpuset.mems' not in assigned:
                assigned['cpuset.mems'] = socketlist
        return assigned

    def assign_job(self, requested, available, node):
        """
        Assign resources to the job. There are two scenarios that need to
        be handled:
        1. If vnodes are present in the requested resources, then the
           scheduler has already decided where the job is to run. Check
           the available resources to ensure an orphaned cgroup is not
           consuming them.
        2. If no vnodes are present in the requested resources, try to
           span the fewest number of sockets when creating the assignment.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   'Requested: %s, Available: %s, Numa Nodes: %s' %
                   (requested, available, node.numa_nodes))
        # Create a list of memory-only NUMA nodes (for KNL). These get assigned
        # in addition to NUMA nodes with assigned devices or cpus.
        memory_only_nodes = []
        for nnid in node.numa_nodes:
            if not node.numa_nodes[nnid]['cpus'] and \
                    not node.numa_nodes[nnid]['devices']:
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Found memory only NUMA node: %s' %
                           (node.numa_nodes[nnid]))
                memory_only_nodes.append(nnid)
        # Create a list of vnode/socket pairs
        if 'vnodes' in requested:
            regex = re.compile(r'(.*)\[(\d+)\].*')
            pairlist = []
            for vnode in requested['vnodes']:
                pairlist.append([regex.search(vnode).group(1),
                                 int(regex.search(vnode).group(2))])
        else:
            sockets = list(available.keys())
            # If placement type is job_balanced, reorder the sockets
            if self.cfg['placement_type'] == 'job_balanced':
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Requested job_balanced placement')
                # Look at assigned_resources and determine which socket
                # to start with
                jobcount = {}
                for sock in sockets:
                    jobcount[sock] = 0
                for job in self.assigned_resources:
                    jobresc = self.assigned_resources[job]
                    if 'cpuset' in jobresc and 'mems' in jobresc['cpuset']:
                        for sock in jobresc['cpuset']['mems']:
                            jobcount[sock] += 1
                sorted_jobcounts = sorted(list(jobcount.items()),
                                          key=operator.itemgetter(1))
                reordered = []
                for count in sorted_jobcounts:
                    reordered.append(count[0])
                sockets = reordered
            elif self.cfg['placement_type'] == 'load_balanced':
                cpucounts = dict()
                for sock in sockets:
                    cpucounts[sock] = len(available[sock]['cpus'])
                sorted_cpucounts = sorted(list(cpucounts.items()),
                                          key=operator.itemgetter(1),
                                          reverse=True)
                reordered = list()
                for count in sorted_cpucounts:
                    reordered.append(count[0])
                sockets = reordered
            elif self.cfg['placement_type'] == 'load_packed':
                cpucounts = dict()
                for sock in sockets:
                    cpucounts[sock] = len(available[sock]['cpus'])
                sorted_cpucounts = sorted(list(cpucounts.items()),
                                          key=operator.itemgetter(1),
                                          reverse=False)
                reordered = list()
                for count in sorted_cpucounts:
                    reordered.append(count[0])
                sockets = reordered
            pairlist = []
            for sock in sockets:
                pairlist.append([None, int(sock)])
        # Loop through the sockets or vnodes and assign resources
        assigned = {}
        for pair in pairlist:
            vnode = pair[0]
            socket = pair[1]
            if vnode:
                myname = 'vnode %s[%d]' % (vnode, socket)
                req = requested['vnodes']['%s[%d]' % (vnode, socket)]
            else:
                myname = 'socket %d' % socket
                req = requested
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Current target is %s' % myname)
            new = None
            if socket in available:
                new = self._assign_resources(req, available[socket],
                                             [socket], node)
            else:
                pbs.logmsg(pbs.EVENT_ERROR, 'Attempt to allocate resources '
                           'on non-existing socket #' + str(socket))
            if new:
                new['cpuset.mems'].append(socket)
                # Add the memory-only NUMA nodes
                for nnid in memory_only_nodes:
                    if nnid not in new['cpuset.mems']:
                        new['cpuset.mems'].append(nnid)
                pbs.logmsg(pbs.EVENT_DEBUG4, 'Resources assigned to %s' %
                           myname)
                if vnode:
                    assigned = self._combine_resources(assigned, new)
                else:
                    # Requested resources fit on this socket
                    return new
            else:
                pbs.logmsg(pbs.EVENT_DEBUG4, 'Resources not assigned to %s' %
                           myname)
                # This is fatal in the case of vnodes
                if vnode:
                    return {}
        if vnode:
            if 'cpuset.cpus' in assigned:
                assigned['cpuset.cpus'].sort()
            if 'cpuset.mems' in assigned:
                assigned['cpuset.mems'].sort()
            if 'devices' in assigned:
                assigned['devices'].sort()
            if 'device_names' in assigned:
                assigned['device_names'].sort()
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'Assigned Resources: %s' % (assigned))
            return assigned
        # Not using vnodes so try spanning sockets
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Attempting to span sockets')
        total = {}
        socketlist = []
        for pair in pairlist:
            socket = pair[1]
            socketlist.append(socket)
            total = self._combine_resources(total, available[socket])
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Combined available resources: %s' %
                   (total))
        return self._assign_resources(requested, total, socketlist, node)

    def available_node_resources(self, node, exclude_jobid=None):
        """
        Determine which resources are available from the supplied node
        dictionary (i.e. the local node) by removing resources already
        assigned to jobs.
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        available = copy.deepcopy(node.numa_nodes)
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Available Keys: %s' % (available[0]))
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Available: %s' % (available))
        for socket in available:
            if 'mem' in available[socket]:
                available[socket]['memory'] = \
                    size_as_int(str(available[socket]['mem']))
            elif 'MemTotal' in available[socket]:
                # Find the memory on the socket in bytes.
                # Remove the 'b' to simplfy the math
                available[socket]['memory'] = size_as_int(
                    available[socket]['MemTotal'])
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Available prior to device add: %s' %
                   (available))
        for device in node.devices:
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: Device Names: %s' %
                       (caller_name(), device))
            if device == 'mic' or device == 'gpu':
                pbs.logmsg(pbs.EVENT_DEBUG4, 'Devices: %s' %
                           node.devices[device])
                for device_name in node.devices[device]:
                    device_socket = \
                        node.devices[device][device_name]['numa_node']
                    if 'devices' not in available[device_socket]:
                        available[device_socket]['devices'] = []
                    pbs.logmsg(pbs.EVENT_DEBUG4,
                               'Device: %s, Socket: %s' %
                               (device, device_socket))
                    available[device_socket]['devices'].append(device_name)
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Available: %s' % (available))
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   'Assigned: %s' % (self.assigned_resources))
        # Remove all of the resources that are assigned to other jobs
        for jobid in self.assigned_resources:
            if exclude_jobid and (jobid == exclude_jobid):
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           ('Job %s res not removed from host '
                            'available res: excluded job') % jobid)
                continue

            # Support suspended jobs on nodes
            if job_is_suspended(jobid):
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           ('Job %s res not removed from host '
                            'available res: suspended job') % jobid)
                continue
            cpus = []
            sockets = []
            devices = []
            memory = 0
            jra = self.assigned_resources[jobid]
            if 'cpuset' in jra:
                if 'cpus' in jra['cpuset']:
                    cpus = jra['cpuset']['cpus']
                if 'mems' in jra['cpuset']:
                    sockets = jra['cpuset']['mems']
            if 'devices' in jra:
                if 'list' in jra['devices']:
                    devices = jra['devices']['list']
            if 'memory' in jra:
                if 'limit_in_bytes' in jra['memory']:
                    memory = size_as_int(jra['memory']['limit_in_bytes'])
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'cpus: %s, sockets: %s, memory limit: %s' %
                       (cpus, sockets, memory))
            pbs.logmsg(pbs.EVENT_DEBUG4, 'devices: %s' % devices)
            # Loop through the sockets and remove cpus that are
            # assigned to other cgroups
            for socket in sockets:
                for cpu in cpus:
                    try:
                        available[socket]['cpus'].remove(cpu)
                    except ValueError:
                        pass
                    except Exception:
                        pbs.logmsg(pbs.EVENT_DEBUG4,
                                   'Error removing %d from %s' %
                                   (cpu, available[socket]['cpus']))
            if len(sockets) == 1:
                avail_mem = available[sockets[0]]['memory']
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Sockets: %s\tAvailable: %s' %
                           (sockets, available))
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Decrementing memory: %d by %d' %
                           (size_as_int(avail_mem), memory))
                if memory <= available[sockets[0]]['memory']:
                    available[sockets[0]]['memory'] -= memory
            # Loop throught the available sockets
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'Assigned device to %s: %s' % (jobid, devices))
            for socket in available:
                for device in devices:
                    try:
                        # loop through known devices and see if they match
                        if available[socket]['devices']:
                            pbs.logmsg(pbs.EVENT_DEBUG4,
                                       'Check device: %s' % (device))
                            pbs.logmsg(pbs.EVENT_DEBUG4,
                                       'Available device: %s' %
                                       (available[socket]['devices']))
                            major, minor = device.split()[1].split(':')
                            avail_device = self.get_device_name(node,
                                                                available,
                                                                socket,
                                                                int(major),
                                                                int(minor))
                            pbs.logmsg(pbs.EVENT_DEBUG4,
                                       'Returned device: %s' %
                                       (avail_device))
                            if avail_device is not None:
                                pbs.logmsg(pbs.EVENT_DEBUG4,
                                           ('socket: %d,\t'
                                            'devices: %s,\t'
                                            'device to remove: %s') %
                                           (socket,
                                            available[socket]['devices'],
                                            avail_device))
                                available[socket]['devices'].remove(
                                    avail_device)
                    except ValueError:
                        pass
                    except Exception as exc:
                        pbs.logmsg(pbs.EVENT_DEBUG2,
                                   'Unexpected error: %s' % exc)
                        pbs.logmsg(pbs.EVENT_DEBUG2,
                                   'Error removing %s from %s' %
                                   (device, available[socket]['devices']))
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Available resources: %s' % (available))
        return available

    def set_swappiness(self, value, jobid=''):
        """
        Set the swappiness for a memory cgroup
        """
        pbs.logmsg(pbs.EVENT_DEBUG3, "%s: Method called" % (caller_name()))
        path = self._cgroup_path('memory', 'swappiness', jobid)
        try:
            self.write_value(path, value)
        except Exception as exc:
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Failed to adjust %s: %s' %
                       (caller_name(), path, exc))

    def set_limit(self, resource, value, jobid=''):
        """
        Set a cgroup limit on a node or a job
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if jobid:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: %s = %s for job %s' %
                       (caller_name(), resource, value, jobid))
        else:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: %s = %s for node' %
                       (caller_name(), resource, value))
        if resource == 'mem':
            if 'memory' in self.subsystems:
                path = self._cgroup_path('memory', 'limit_in_bytes', jobid)
                self.write_value(path, size_as_int(value))
        elif resource == 'softmem':
            if 'memory' in self.subsystems:
                path = self._cgroup_path('memory', 'soft_limit_in_bytes',
                                         jobid)
                self.write_value(path, size_as_int(value))
        elif resource == 'vmem':
            if 'memsw' in self.subsystems:
                if 'memory' not in self.subsystems:
                    path = self._cgroup_path('memory', 'limit_in_bytes',
                                             jobid)
                    self.write_value(path, size_as_int(value))
                path = self._cgroup_path('memsw', 'limit_in_bytes', jobid)
                self.write_value(path, size_as_int(value))
        elif resource == 'hpmem':
            if 'hugetlb' in self.subsystems:
                path = self._cgroup_path('hugetlb', 'limit_in_bytes', jobid)
                self.write_value(path, size_as_int(value))
        elif resource == 'ncpus':
            if 'cpu' in self.subsystems:
                # Note the value is already multiplied by threads/core
                # if ncpus_are_cores and use_hyperthreads are True
                path = self._cgroup_path('cpu', 'shares', jobid)
                weightless = False
                # 'zero' cpu jobs have the minimum shares, but value for
                # clamping to quota needs to be realistic
                if (value <= 0):
                    weightless = True
                    weightless_shares = (self.cfg['cgroup']
                                                 ['cpu']
                                                 ['zero_cpus_shares_fraction'])
                    # Note that the minimum in the kernel is 2
                    self.write_value(
                        path, int(max(2, weightless_shares * 1000)))
                    value = (self.cfg['cgroup']
                                     ['cpu']
                                     ['zero_cpus_quota_fraction'])
                else:
                    self.write_value(path, int(value * 1000))
                if (self.cfg['cgroup']['cpu']['enforce_per_period_quota']
                        or weightless):
                    # zero cpu jobs ALWAYS get a quota -- keep them honest
                    cfs_period_us = self.cfg['cgroup']['cpu']['cfs_period_us']
                    path = self._cgroup_path('cpu', 'cfs_period_us', jobid)
                    self.write_value(path, cfs_period_us)

                    cfs_quota_fudge_factor = \
                        self.cfg['cgroup']['cpu']['cfs_quota_fudge_factor']
                    path = self._cgroup_path('cpu', 'cfs_quota_us', jobid)
                    # zero cpu jobs are clamped to use less than one cpu
                    # during each period
                    cfs_quota_us_calculated = (cfs_period_us
                                               * value
                                               * cfs_quota_fudge_factor)

                    # Get the parent cgroup's maximum cfs_quota_us
                    # we cannot set the child cgroup value to anything more
                    # Note that some kernels display "unlimited" as signed -1
                    # So we should ignore non-positive values
                    max_cfs_quota_us = self._get_cfs_quota_us()
                    if ((max_cfs_quota_us is not None)
                            and (max_cfs_quota_us > 0)
                            and (cfs_quota_us_calculated > max_cfs_quota_us)):
                        cfs_quota_us_calculated = max_cfs_quota_us
                    self.write_value(path, int(cfs_quota_us_calculated))
        elif resource == 'cpuset.cpus':
            if 'cpuset' in self.subsystems:
                path = self._cgroup_path('cpuset', 'cpus', jobid)
                cpus = value
                if not cpus:
                    raise CgroupLimitError('Failed to configure cpuset cpus')
                cpus = ",".join(list(map(str, cpus)))
                self.write_value(path, cpus)
        elif resource == 'cpuset.mems':
            if 'cpuset' in self.subsystems:
                path = self._cgroup_path('cpuset', 'mems', jobid)
                if not os.path.exists(path):
                    # probably zero cpu job now in root cpuset -- do nothing
                    return
                if self.cfg['cgroup']['cpuset']['mem_fences']:
                    mems = value
                    if not mems:
                        raise CgroupLimitError(
                            'Failed to configure cpuset mems')
                    mems = ','.join(list(map(str, mems)))
                    self.write_value(path, mems)
                else:
                    pbs.logmsg(pbs.EVENT_DEBUG4, ('Memory fences disabled, '
                                                  'copying cpuset.mems from '
                                                  ' parent for %s') % jobid)
                    self._copy_from_parent(path)
        elif resource == 'devices':
            if 'devices' in self.subsystems:
                path = self._cgroup_path('devices', 'allow', jobid)
                devices = value
                if not devices:
                    raise CgroupLimitError('Failed to configure devices')
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Setting devices: %s for %s' % (devices, jobid))
                for dev in devices:
                    self.write_value(path, dev)
                path = self._cgroup_path('devices', 'list', jobid)
                with open(path, 'r') as desc:
                    output = desc.readlines()
                pbs.logmsg(pbs.EVENT_DEBUG4, 'devices.list: %s' % output)
        else:
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Resource %s not handled' %
                       (caller_name(), resource))

    def update_job_usage(self, jobid, resc_used, force=False):
        """
        Update resource usage for a job
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: resc_used = %s' %
                   (caller_name(), str(resc_used)))
        if not job_is_running(jobid) and not force:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Job %s is not running' %
                       (caller_name(), jobid))
            return
        # Sort the subsystems so that we consistently look at the subsystems
        # in the same order every time
        self.subsystems.sort()
        for subsys in self.subsystems:
            if subsys == 'memory':
                max_mem = self._get_max_mem_usage(jobid)
                if max_mem is None:
                    pbs.logjobmsg(jobid, '%s: No max mem data' % caller_name())
                else:
                    resc_used['mem'] = pbs.size(convert_size(max_mem, 'kb'))
                    pbs.logjobmsg(jobid, '%s: Memory usage: mem=%s' %
                                  (caller_name(), resc_used['mem']))
                mem_failcnt = self._get_mem_failcnt(jobid)
                if mem_failcnt is None:
                    pbs.logjobmsg(jobid, '%s: No mem fail count data' %
                                  caller_name())
                else:
                    # Check to see if the job exceeded its resource limits
                    if mem_failcnt > 0:
                        err_msg = self._get_error_msg(jobid)
                        pbs.logjobmsg(jobid,
                                      'Cgroup memory limit exceeded: %s' %
                                      (err_msg))
                        if (pbs.event().type == pbs.EXECJOB_EPILOGUE
                                and pbs.event().job.in_ms_mom()):
                            self.write_to_stderr(pbs.event().job,
                                                 "Cgroup mem limit "
                                                 "exceeded: %s\n" % (err_msg))
            elif subsys == 'memsw':
                max_vmem = self._get_max_memsw_usage(jobid)
                if max_vmem is None:
                    pbs.logjobmsg(jobid, '%s: No max vmem data' %
                                  caller_name())
                else:
                    resc_used['vmem'] = pbs.size(convert_size(max_vmem, 'kb'))
                    pbs.logjobmsg(jobid, '%s: Memory usage: vmem=%s' %
                                  (caller_name(), resc_used['vmem']))
                vmem_failcnt = self._get_memsw_failcnt(jobid)
                if vmem_failcnt is None:
                    pbs.logjobmsg(jobid, '%s: No vmem fail count data' %
                                  caller_name())
                else:
                    pbs.logjobmsg(jobid, '%s: vmem fail count: %d ' %
                                  (caller_name(), vmem_failcnt))
                    if vmem_failcnt > 0:
                        err_msg = self._get_error_msg(jobid)
                        pbs.logjobmsg(jobid,
                                      'Cgroup memsw limit exceeded: %s\n' %
                                      (err_msg))
                        if (pbs.event().type == pbs.EXECJOB_EPILOGUE
                                and pbs.event().job.in_ms_mom()):
                            self.write_to_stderr(pbs.event().job,
                                                 "Cgroup memsw limit "
                                                 "exceeded: %s" % (err_msg))
            elif subsys == 'hugetlb':
                max_hpmem = self._get_max_hugetlb_usage(jobid)
                if max_hpmem is None:
                    pbs.logjobmsg(jobid, '%s: No max hpmem data' %
                                  caller_name())
                    return
                hpmem_failcnt = self._get_hugetlb_failcnt(jobid)
                if hpmem_failcnt is None:
                    pbs.logjobmsg(jobid, '%s: No hpmem fail count data' %
                                  caller_name())
                    return
                if hpmem_failcnt > 0:
                    err_msg = self._get_error_msg(jobid)
                    pbs.logjobmsg(jobid, 'Cgroup hugetlb limit exceeded: %s' %
                                  (err_msg))
                    if (pbs.event().type == pbs.EXECJOB_EPILOGUE
                            and pbs.event().job.in_ms_mom()):
                        self.write_to_stderr(pbs.event().job,
                                             "Cgroup hugetlb limit "
                                             "exceeded: %s" % (err_msg))
                resc_used['hpmem'] = pbs.size(convert_size(max_hpmem, 'kb'))
                pbs.logjobmsg(jobid, '%s: Hugepage usage: %s' %
                              (caller_name(), resc_used['hpmem']))
            elif subsys == 'cpuacct':
                if 'walltime' not in resc_used:
                    walltime = 0
                else:
                    if resc_used['walltime']:
                        walltime = int(resc_used['walltime'])
                    else:
                        walltime = 0
                if 'cput' not in resc_used:
                    cput = 0
                else:
                    if resc_used['cput']:
                        cput = int(resc_used['cput'])
                    else:
                        cput = 0
                # Calculate cpupercent based on the reported values
                if walltime > 0:
                    cpupercent = (100.0 * cput) / walltime
                else:
                    cpupercent = 0
                resc_used['cpupercent'] = pbs.pbs_int(int(cpupercent))
                pbs.logjobmsg(jobid, '%s: CPU percent: %d' %
                              (caller_name(), cpupercent))
                # Now update cput
                cput = self._get_cpu_usage(jobid)
                if cput is None:
                    pbs.logjobmsg(jobid, '%s: No CPU usage data' %
                                  caller_name())
                    return
                cput = convert_time(str(cput) + 'ns')
                resc_used['cput'] = pbs.duration(cput)
                pbs.logjobmsg(jobid, '%s: CPU usage: %.3lf secs' %
                              (caller_name(), cput))

    def create_job(self, jobid, node):
        """
        Creates the cgroup if it doesn't exists
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Iterate over the enabled subsystems
        for subsys in self.subsystems:
            # Create a directory for the job
            old_umask = os.umask(0o022)
            try:
                path = self._cgroup_path(subsys, jobid=jobid)
                if not os.path.exists(path):
                    pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Creating directory %s' %
                               (caller_name(), path))
                    os.makedirs(path, 0o755)
                else:
                    pbs.logmsg(pbs.EVENT_DEBUG3,
                               '%s: Directory %s already exists' %
                               (caller_name(), path))
                if subsys == 'devices':
                    self._setup_subsys_devices(jobid, node)
            except OSError as exc:
                raise CgroupConfigError('Failed to create directory: %s (%s)' %
                                        (path, errno.errorcode[exc.errno]))
            except Exception:
                raise
            finally:
                os.umask(old_umask)

    def configure_job(self, job, hostresc, node, cgroup, event_type):
        """
        Determine the cgroup limits and configure the cgroups
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        jobid = job.id

        # Get available totals for mem, vmem and hpmem from parent cgroup
        # Calling node.get_memory_on_node etc. may give slightly different
        # results if kernel drivers have freshly reserved memory since
        # exechost_startup was called
        mem_avail = 0
        filename = self._cgroup_path('memory', 'limit_in_bytes')
        if filename and os.path.isfile(filename):
            with open(filename) as desc:
                mem_avail = int(desc.readline())
        vmem_avail = 0
        filename = self._cgroup_path('memsw', 'limit_in_bytes')
        if filename and os.path.isfile(filename):
            with open(filename) as desc:
                vmem_avail = int(desc.readline())
        hpmem_avail = 0
        filename = self._cgroup_path('hugetlb', 'limit_in_bytes')
        if filename and os.path.isfile(filename):
            with open(filename) as desc:
                hpmem_avail = int(desc.readline())

        # Sanity check vmem_avail >= mem_avail
        # -- unnecessary by construction in exechost_startup
        # but memsw may be disabled or swap accounting disabled
        if vmem_avail < mem_avail:
            vmem_avail = mem_avail

        pbs.logmsg(pbs.EVENT_DEBUG4, "configure_job:"
                   " total host avail mem=%s vmem=%s hpmem=%s"
                   % (mem_avail, vmem_avail, hpmem_avail))

        mem_enabled = 'memory' in self.subsystems
        vmem_enabled = 'memsw' in self.subsystems
        if mem_enabled or vmem_enabled:
            # Initialize mem variables
            mem_requested = None
            if 'mem' in hostresc:
                mem_requested = convert_size(hostresc['mem'], 'kb')
            mem_default = None
            if mem_enabled and self.default('memory') is not None:
                mem_default = size_as_int(self.default('memory'))
            # Initialize vmem variables
            vmem_requested = None
            if 'vmem' in hostresc:
                vmem_requested = convert_size(hostresc['vmem'], 'kb')
            vmem_default = 0
            if (vmem_enabled and (self.default('memsw') is not None)
                    and (vmem_avail > mem_avail)):
                vmem_default = size_as_int(self.default('memsw'))
                if vmem_default > (vmem_avail - mem_avail):
                    vmem_default = vmem_avail - mem_avail
            # Initialize softmem variables
            if 'soft_limit' in self.cfg['cgroup']['memory']:
                softmem_enabled = self.cfg['cgroup']['memory']['soft_limit']
            else:
                softmem_enabled = False
            # Determine the mem limit
            if mem_requested is not None:
                # mem requested may not exceed available
                if size_as_int(mem_requested) > mem_avail:
                    raise JobValueError(
                        'mem requested (%s) exceeds mem available (%s)' %
                        (mem_requested, str(mem_avail)))
                mem_limit = mem_requested
            else:
                # mem was not requested
                if (mem_default is None
                        or not self.cfg['cgroup']['memory']
                                       ['enforce_default']
                        or (self.cfg['cgroup']['memory']
                                    ['exclhost_ignore_default']
                            and "place" in job.Resource_List
                            and 'exclhost'
                                in repr(job.Resource_List['place']))):
                    mem_limit = str(mem_avail)
                else:
                    mem_limit = str(mem_default)
            # Determine the vmem limit
            if not vmem_enabled:
                # set as equal, but will not be used to set cgroup limit
                vmem_limit = mem_limit
            elif vmem_requested is not None:
                # vmem requested may not exceed available
                if size_as_int(vmem_requested) > vmem_avail:
                    raise JobValueError(
                        'vmem requested (%s) exceeds vmem available (%s)' %
                        (vmem_requested, str(vmem_avail)))
                vmem_limit = vmem_requested
            else:
                # vmem was not requested
                if (vmem_default is None
                        or not self.cfg['cgroup']['memsw']
                                       ['enforce_default']
                        or (self.cfg['cgroup']['memsw']
                                    ['exclhost_ignore_default']
                            and 'place' in job.Resource_List
                            and 'exclhost'
                                in repr(job.Resource_List['place']))):
                    vmem_limit = str(vmem_avail)
                else:
                    vmem_limit = str(min(vmem_avail,
                                         size_as_int(mem_limit)
                                         + vmem_default))
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       "Limits computed from requests/defaults: "
                       "mem: %s vmem: %s" % (mem_limit, vmem_limit))
            # Ensure vmem is at least as large as mem
            if size_as_int(vmem_limit) < size_as_int(mem_limit):
                vmem_limit = mem_limit
            # Adjust for soft limits if enabled
            if mem_enabled and softmem_enabled:
                softmem_limit = mem_limit
                # The hard memory limit is assigned the lesser of the vmem
                # limit and available memory
                if size_as_int(vmem_limit) < mem_avail:
                    mem_limit = vmem_limit
                else:
                    mem_limit = str(mem_avail)
            # Again, ensure vmem is at least as large as mem
            if size_as_int(vmem_limit) < size_as_int(mem_limit):
                vmem_limit = mem_limit
            # Sanity checks when both memory and memsw are enabled
            if mem_enabled and vmem_enabled:
                if vmem_requested is not None:
                    if size_as_int(vmem_requested) < size_as_int(vmem_limit):
                        # The user requested an invalid limit
                        raise JobValueError(
                            'vmem requested (%s) under minimum possible (%s)'
                            % (vmem_requested, vmem_limit))
                if size_as_int(vmem_limit) > size_as_int(mem_limit):
                    # This job may try to utilize swap
                    if vmem_avail <= mem_avail:
                        # No swap available
                        if vmem_requested is not None:
                            raise CgroupLimitError('Job might utilize swap'
                                                   ' and no swap on host')
                        else:
                            # It got an impossible vmem>mem
                            # undo that since there is no swap
                            vmem_limit = mem_limit
            # Assign mem and vmem
            if mem_enabled:
                if mem_requested is None:
                    pbs.logmsg(pbs.EVENT_DEBUG2,
                               '%s: mem not requested, '
                               'assigning %s to cgroup'
                               % (caller_name(), mem_limit))
                    hostresc['mem'] = pbs.size(mem_limit)
                if softmem_enabled:
                    hostresc['softmem'] = pbs.size(softmem_limit)
            if vmem_enabled:
                if vmem_requested is None:
                    pbs.logmsg(pbs.EVENT_DEBUG2,
                               '%s: vmem not requested, '
                               'assigning %s to cgroup'
                               % (caller_name(), vmem_limit))
                    pbs.logmsg(pbs.EVENT_DEBUG4,
                               '%s: INFO: vmem is enabled in the hook '
                               'configuration file and should also be '
                               'listed in the resources line of the '
                               'scheduler configuration file' %
                               caller_name())
                    hostresc['vmem'] = pbs.size(vmem_limit)

        # Initialize hpmem variables
        hpmem_default = None
        hpmem_limit = None
        hpmem_enabled = 'hugetlb' in self.subsystems
        if hpmem_enabled:
            if self.default('hugetlb') is not None:
                hpmem_default = size_as_int(self.default('hugetlb'))
            if (hpmem_default is None
                    or not self.cfg['cgroup']['hugetlb']
                                   ['enforce_default']
                    or (self.cfg['cgroup']['hugetlb']
                                ['exclhost_ignore_default']
                        and "place" in job.Resource_List
                        and 'exclhost' in repr(job.Resource_List['place']))):
                hpmem_default = hpmem_avail
            if 'hpmem' in hostresc:
                hpmem_limit = convert_size(hostresc['hpmem'], 'kb')
            else:
                hpmem_limit = str(hpmem_default)
            # Assign hpmem
            if size_as_int(hpmem_limit) > hpmem_avail:
                raise JobValueError('hpmem limit (%s) exceeds available (%s)' %
                                    (hpmem_limit, str(hpmem_avail)))
            hostresc['hpmem'] = pbs.size(hpmem_limit)
        # Initialize cpuset variables
        cpuset_enabled = 'cpuset' in self.subsystems
        if cpuset_enabled:
            cpu_limit = 0
            if 'ncpus' in hostresc:
                cpu_limit = hostresc['ncpus']
            # support weightless jobs in root cpuset
            # do not clamp cpu_limit to min 1
            hostresc['ncpus'] = pbs.pbs_int(cpu_limit)
        # Find the available resources and assign the right ones to the job
        assigned = dict()
        jobdict = dict()
        # Make two attempts since self.cleanup_orphans may actually fix the
        # problem we see in a first attempt
        for attempt in range(2):
            if (hasattr(pbs, "EXECJOB_RESIZE")
                    and event_type == pbs.EXECJOB_RESIZE):
                # consider current job's resources as being available,
                # where a subset of them would be re-assigned to the
                # the same job.
                avail_resc = self.available_node_resources(node, jobid)
            else:
                avail_resc = self.available_node_resources(node)
            assigned = self.assign_job(hostresc, avail_resc, node)
            # If this was not the first attempt, do not bother trying to
            # clean up again. This is handled immediately after the loop.
            if attempt != 0:
                break
            if not assigned:
                # No resources were assigned to the job, most likely because
                # a cgroup has not been cleaned up yet
                pbs.logmsg(pbs.EVENT_DEBUG2,
                           '%s: Failed to assign job resources' %
                           caller_name())
                pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Resyncing local job data' %
                           caller_name())
                # Collect the jobs on the node (try reading mom_priv/jobs)
                try:
                    jobdict = node.gather_jobs_on_node(cgroup)
                except Exception:
                    jobdict = dict()
                    pbs.logmsg(pbs.EVENT_DEBUG2,
                               '%s: Failed to gather local job data' %
                               caller_name())
                # There may not be a .JB file present for this job yet
                if jobid not in jobdict:
                    jobdict[jobid] = time.time()
                self.cleanup_orphans(jobdict)
                # Resynchronize after cleanup
                self.assigned_resources = self._get_assigned_cgroup_resources()
        if not assigned:
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Assignment of resources failed '
                       'for %s, attempting cleanup' % (caller_name, jobid))
            # Cleanup cgroups for jobs not present on this node
            jobdict = node.gather_jobs_on_node(cgroup)
            if jobdict and jobid in jobdict:
                del jobdict[jobid]
            self.cleanup_orphans(jobdict)
            # Log a message and rerun the job
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Requeuing job %s' %
                       (caller_name(), jobid))
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Run count for job %s: %d' %
                       (caller_name(), jobid, pbs.event().job.run_count))
            pbs.event().job.rerun()
            raise CgroupProcessingError('Failed to assign resources')
        # Print out the assigned resources
        pbs.logmsg(pbs.EVENT_DEBUG2,
                   'Assigned resources: %s' % (assigned))
        self.assigned_resources = assigned
        if cpuset_enabled:
            # Do not remove the ncpus key if it exists:
            # used by the "cpu" controller now.
            for key in ['cpuset.cpus', 'cpuset.mems']:
                if key in assigned:
                    hostresc[key] = assigned[key]
                else:
                    pbs.logmsg(pbs.EVENT_DEBUG2,
                               'Key: %s not found in assigned' % key)
        # Initialize devices variables
        key = 'devices'
        if key in self.subsystems:
            if key in assigned:
                hostresc[key] = assigned[key]
            else:
                pbs.logmsg(pbs.EVENT_DEBUG2,
                           'Key: %s not found in assigned' % key)
        # Apply the resource limits to the cgroups
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Setting cgroup limits for: %s' %
                   (caller_name(), hostresc))
        # The vmem limit must be set after the mem limit, so sort the keys
        # also ensures we get to cpuset.cpus before cpuset.mem
        # important for zero cpu jobs migrated to root cpuset
        for resc in sorted(hostresc):
            if resc == 'ncpus':
                # In case of HT, may need to multiply the ncpus;
                # done here since "node" is not passed down to set_limit
                # This only sets the cpu controller limits/shares,
                # since the cpuset stuff is done through
                # cpuset.cpus and cpuset.mems
                # Note the set_limit in the final 'else' should be
                # skipped -- its replacement is at the end here
                htpc = 1
                if (self.cfg['cgroup']['cpu']['enabled']
                        and self.cfg['use_hyperthreads']
                        and self.cfg['ncpus_are_cores']):
                    if 'hyperthreads_per_core' in node.cpuinfo:
                        htpc = node.cpuinfo['hyperthreads_per_core']
                self.set_limit(resc, hostresc[resc] * htpc, jobid)
            elif ((resc == "cpuset.cpus")
                  and not hostresc['cpuset.cpus']):
                # zero cpus jobs -- delete the cpuset made for the job;
                # later allows job processes to be placed in the root cpuset.
                # Note the set_limit in the final else should likewise
                # be skipped.
                # Since the advent of "resize" we may need to
                # clean up processes in a pre-existing cpuset
                finished = False
                giveup_time = time.time() + self.cfg['kill_timeout']
                path = self._cgroup_path('cpuset', '', jobid)
                while not finished:
                    success = self._remove_cgroup(path)
                    if success:
                        finished = True
                    elif time.time() > giveup_time:
                        finished = True
                    else:
                        time.sleep(0.2)
            else:
                # For all the rest just pass hostresc[resc] down to set_limit
                self.set_limit(resc, hostresc[resc], jobid)
        # Set additional parameters
        # Note some kernels will surprisingly not let you set things
        # to the value that is already there in some corner cases
        # hence some of the reads to see "if we need to write"
        if cpuset_enabled:
            path = self._cgroup_path('cpuset', 'mem_hardwall', jobid)
            lines = self.read_value(path)
            curval = 0
            if lines and lines[0] == '1':
                curval = 1
            if self.cfg['cgroup']['cpuset']['mem_hardwall']:
                if curval == 0:
                    self.write_value(path, '1')
            else:
                if curval == 1:
                    self.write_value(path, '0')
            path = self._cgroup_path('cpuset', 'memory_spread_page', jobid)
            lines = self.read_value(path)
            curval = 0
            if lines and lines[0] == '1':
                curval = 1
            if self.cfg['cgroup']['cpuset']['memory_spread_page']:
                if curval == 0:
                    self.write_value(path, '1')
            else:
                if curval == 1:
                    self.write_value(path, '0')

    def _kill_tasks(self, tasks_file):
        """
        Kill any processes contained within a tasks file
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if not os.path.isfile(tasks_file):
            return 0
        count = 0
        with open(tasks_file, 'r') as tasks_desc:
            for line in tasks_desc:
                count += 1
                try:
                    os.kill(int(line.strip()), signal.SIGKILL)
                except Exception:
                    pass
        # Give the OS a moment to update the tasks file
        time.sleep(0.1)
        count = 0
        try:
            with open(tasks_file, 'r') as tasks_desc:
                for line in tasks_desc:
                    count += 1
                    pid = line.strip()
                    filename = os.path.join(os.sep, 'proc', pid, 'status')
                    statlist = []
                    try:
                        with open(filename, 'r') as status_desc:
                            for line2 in status_desc:
                                if line2.find('Name:') != -1:
                                    statlist.append(line2.strip())
                                if line2.find('State:') != -1:
                                    statlist.append(line2.strip())
                                if line2.find('Uid:') != -1:
                                    statlist.append(line2.strip())
                    except Exception:
                        pass
                    pbs.logmsg(pbs.EVENT_DEBUG2, '%s: PID %s survived: %s' %
                               (caller_name(), pid, statlist))
        except Exception as exc:
            if exc.errno != errno.ENOENT:
                raise
        return count

    def _delete_cgroup_children(self, path):
        """
        Recursively delete all children within a cgroup, but not the parent
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if not os.path.isdir(path):
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: No such directory: %s' %
                       (caller_name(), path))
            return 0
        remaining_children = 0
        for filename in os.listdir(path):
            subdir = os.path.join(path, filename)
            if not os.path.isdir(subdir):
                continue
            remaining_children += self._delete_cgroup_children(subdir)
            tasks_file = os.path.join(subdir, 'tasks')
            remaining_tasks = self._kill_tasks(tasks_file)
            if remaining_tasks > 0:
                remaining_children += 1
                continue
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Removing directory %s' %
                       (caller_name(), subdir))
            try:
                os.rmdir(subdir)
            except Exception as exc:
                pbs.logmsg(pbs.EVENT_SYSTEM,
                           'Error removing cgroup path %s: %s' %
                           (subdir, str(exc)))
        return remaining_children

    def _remove_cgroup(self, path, jobid=None):
        """
        Perform the actual removal of the cgroup directory.
        Make only one attempt at killing tasks in cgroup,
        since this method could be called many times (for N
        directories times M jobs).
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        if not os.path.isdir(path):
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: No such directory: %s' %
                       (caller_name(), path))
            return True
        if not jobid:
            parent = path
        else:
            parent = os.path.join(path, jobid)
        # Recursively delete children
        self._delete_cgroup_children(parent)
        # Delete the parent
        tasks_file = os.path.join(parent, 'tasks')
        remaining = 0
        if not os.path.isfile(tasks_file):
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: No such file: %s' %
                       (caller_name(), tasks_file))
        else:
            try:
                remaining = self._kill_tasks(tasks_file)
            except Exception:
                pass
        if remaining == 0:
            pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Removing directory %s' %
                       (caller_name(), parent))
            for _ in range(2):
                try:
                    os.rmdir(parent)
                except OSError as exc:
                    pbs.logmsg(pbs.EVENT_SYSTEM,
                               'OS error removing cgroup path %s: %s' %
                               (parent, errno.errorcode[exc.errno]))
                except Exception as exc:
                    pbs.logmsg(pbs.EVENT_SYSTEM,
                               'Failed to remove cgroup path %s: %s' %
                               (parent, exc))
                    raise
                if not os.path.isdir(parent):
                    break
                time.sleep(0.5)
            return True
        if not os.path.isdir(parent):
            return True
        # Cgroup removal has failed
        pbs.logmsg(pbs.EVENT_SYSTEM, 'cgroup still has %d tasks: %s' %
                   (remaining, parent))
        # Nodes are taken offline in the delete() method
        return False

    def cleanup_hook_data(self, local_jobs=[]):
        pattern = os.path.join(self.hook_storage_dir, '[0-9]*.*')
        for filename in glob.glob(pattern):
            if os.path.basename(filename) in local_jobs:
                continue
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'Stale file %s to be removed' % filename)
            try:
                os.remove(filename)
            except Exception as exc:
                pbs.logmsg(pbs.EVENT_ERROR, 'Error removing file: %s' % exc)

    def cleanup_env_files(self, local_jobs=[]):
        pattern = os.path.join(self.host_job_env_dir, '[0-9]*.env')
        for filename in glob.glob(pattern):
            (jobid, extension) = os.path.splitext(os.path.basename(filename))
            if jobid in local_jobs:
                continue
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       'Stale file %s to be removed' % filename)
            try:
                os.remove(filename)
            except Exception as exc:
                pbs.logmsg(pbs.EVENT_ERROR, 'Error removing file: %s' % exc)

    def cleanup_orphans(self, local_jobs):
        """
        Removes cgroup directories that are not associated with a local job
        and cleanup any environment and assigned_resources files
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Local jobs: %s' % local_jobs)
        self.cleanup_hook_data(local_jobs)
        self.cleanup_env_files(local_jobs)
        remaining = 0
        # Always do systemd first, to prevent it from re-"mirroring"
        # that directory into other hierarchies behind our back
        if 'systemd' in self.paths:
            keys_to_process = \
                (['systemd']
                 + [x for x in self.paths if x != 'systemd'])
        else:
            keys_to_process = [x for x in self.paths]
        for key in keys_to_process:
            path = os.path.dirname(self._cgroup_path(key))
            # Identify any orphans and append an orphan suffix
            pattern = self._glob_subdir_wildcard()
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Searching for orphans: %s' %
                       (caller_name(), os.path.join(path, pattern)))
            for subdir in glob.glob(os.path.join(path, pattern)):
                jobid = os.path.basename(subdir)
                if jobid in local_jobs or jobid.endswith('.orphan'):
                    continue
                # Now rename the directory.
                filename = jobid + '.orphan'
                new_subdir = os.path.join(path, filename)
                pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Renaming %s to %s' %
                           (caller_name(), subdir, new_subdir))
                # Make sure the directory still exists before it is renamed
                # or the logs could contain extraneous messages
                if os.path.exists(subdir):
                    try:
                        os.rename(subdir, new_subdir)
                    except Exception:
                        pbs.logmsg(pbs.EVENT_DEBUG2,
                                   '%s: Failed to rename %s to %s' %
                                   (caller_name(), subdir, new_subdir))
            # Attempt to remove the orphans
            pattern = self._glob_subdir_wildcard(extension='orphan')
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Cleaning up orphans: %s' %
                       (caller_name(), os.path.join(path, pattern)))
            for subdir in glob.glob(os.path.join(path, pattern)):
                pbs.logmsg(pbs.EVENT_DEBUG2,
                           '%s: Removing orphaned cgroup: %s' %
                           (caller_name(), subdir))
                if not self._remove_cgroup(subdir):
                    pbs.logmsg(pbs.EVENT_DEBUG,
                               '%s: Removing orphaned cgroup %s failed ' %
                               (caller_name(), subdir))
                    remaining += 1
        return remaining

    def delete(self, jobid, offline_node=True):
        """
        Removes the cgroup directories for a job
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        # Make multiple attempts to kill tasks in the cgroup. Keep
        # trying for kill_timeout seconds.
        if not jobid:
            raise ValueError('Invalid job ID')
        finished = False
        giveup_time = time.time() + self.cfg['kill_timeout']
        try:
            while not finished:
                failure = False
                # Always do systemd first
                if 'systemd' in self.paths:
                    keys_to_process = \
                        (['systemd']
                         + [x for x in self.paths if x != 'systemd'])
                else:
                    keys_to_process = [x for x in self.paths]
                for key in keys_to_process:
                    cgroup_path = self._cgroup_path(key)
                    path = os.path.dirname(self._cgroup_path(key))
                    subdir_basename = jobid
                    subdir = os.path.join(path, subdir_basename)
                    subdir_parent = path
                    # Make sure it still exists
                    if not os.path.isdir(subdir):
                        pbs.logmsg(pbs.EVENT_DEBUG4,
                                   '%s: Skipping because %s is gone' %
                                   (caller_name(), subdir))
                        continue
                    # Remove it
                    pbs.logmsg(pbs.EVENT_DEBUG4,
                               '%s: Attempting to delete %s' %
                               (caller_name(), subdir))
                    if not self._remove_cgroup(subdir_parent, jobid):
                        pbs.logmsg(pbs.EVENT_DEBUG2, '%s: Unable to '
                                   'delete cgroup for job %s' %
                                   (caller_name(), jobid))
                    # Check again
                    if os.path.isdir(subdir):
                        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Deletion '
                                   'failed: %s still exists' %
                                   (caller_name(), subdir))
                        failure = True
                    else:
                        pbs.logmsg(pbs.EVENT_DEBUG4,
                                   '%s: Deleted %s' %
                                   (caller_name(), subdir))
                if not failure:
                    finished = True
                elif time.time() > giveup_time:
                    finished = True
                else:
                    time.sleep(0.2)
        except Exception as exc:
            failure = True
            pbs.logmsg(pbs.EVENT_DEBUG, '%s: Error removing cgroup '
                       'for %s: %s' % (caller_name(), jobid, exc))

        if finished and not failure:
            return True

        # Handle deletion failure
        if not offline_node:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Offline not requested' %
                       caller_name())
            return False
        node = NodeUtils(self.cfg)
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: NodeUtils class instantiated' %
                   caller_name())
        try:
            node.take_node_offline()
        except Exception as exc:
            pbs.logmsg(pbs.EVENT_DEBUG, '%s: Failed to offline node: %s' %
                       (caller_name(), exc))
        return False

    def read_value(self, filename):
        """
        Read value(s) from a limit file
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        lines = []
        try:
            with open(filename, 'r') as desc:
                lines = desc.readlines()
        except IOError:
            pbs.logmsg(pbs.EVENT_SYSTEM, '%s: Failed to read file: %s' %
                       (caller_name(), filename))
        return [x.strip() for x in lines]

    def write_value(self, filename, value, mode='w'):
        """
        Write a value to a limit file
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: writing %s to %s' %
                   (caller_name(), value, filename))
        try:
            with open(filename, mode) as desc:
                desc.write(str(value) + '\n')
        except IOError as exc:
            if exc.errno == errno.ENOENT:
                pbs.logmsg(pbs.EVENT_SYSTEM, '%s: No such file: %s' %
                           (caller_name(), filename))
            elif exc.errno in [errno.EACCES, errno.EPERM]:
                pbs.logmsg(pbs.EVENT_SYSTEM, '%s: Permission denied: %s' %
                           (caller_name(), filename))
            elif exc.errno == errno.EBUSY:
                raise CgroupBusyError('Limit %s rejected: %s' %
                                      (value, filename))
            elif exc.errno == errno.ENOSPC:
                raise CgroupLimitError('Limit %s too small: %s' %
                                       (value, filename))
            elif exc.errno == errno.EINVAL:
                raise CgroupLimitError('Invalid limit value: %s, file: %s' %
                                       (value, filename))
            else:
                pbs.logmsg(pbs.EVENT_SYSTEM,
                           '%s: Uncaught exception writing %s to %s' %
                           (value, filename))
                raise
        except Exception:
            raise

    def _get_cfs_quota_us(self, jobid=''):
        pbs.logmsg(pbs.EVENT_DEBUG3, "%s: Method called" % (caller_name()))
        # default for _cgroup_path for parent is empty string
        try:
            with open(self._cgroup_path('cpu', 'cfs_quota_us',
                                        jobid), 'r') as fd:
                return int(fd.readline().strip())
        except Exception:
            return None

    def _get_mem_failcnt(self, jobid):
        """
        Return memory failcount
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        try:
            with open(self._cgroup_path('memory', 'failcnt', jobid),
                      'r') as desc:
                return int(desc.readline().strip())
        except Exception:
            return None

    def _get_memsw_failcnt(self, jobid):
        """
        Return vmem failcount
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        try:
            with open(self._cgroup_path('memsw', 'failcnt', jobid),
                      'r') as desc:
                return int(desc.readline().strip())
        except Exception:
            return None

    def _get_hugetlb_failcnt(self, jobid):
        """
        Return hpmem failcount
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        try:
            with open(self._cgroup_path('hugetlb', 'failcnt', jobid),
                      'r') as desc:
                return int(desc.readline().strip())
        except Exception:
            return None

    def _get_max_mem_usage(self, jobid):
        """
        Return the max usage of memory in bytes
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        try:
            with open(self._cgroup_path('memory', 'max_usage_in_bytes',
                                        jobid), 'r') as desc:
                return int(desc.readline().strip())
        except Exception:
            return None

    def _get_max_memsw_usage(self, jobid):
        """
        Return the max usage of memsw in bytes
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        try:
            with open(self._cgroup_path('memsw', 'max_usage_in_bytes', jobid),
                      'r') as desc:
                return int(desc.readline().strip())
        except Exception:
            return None

    def _get_max_hugetlb_usage(self, jobid):
        """
        Return the max usage of hugetlb in bytes
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        try:
            with open(self._cgroup_path('hugetlb', 'max_usage_in_bytes',
                                        jobid),
                      'r') as desc:
                return int(desc.readline().strip())
        except Exception:
            return None

    def _get_cpu_usage(self, jobid):
        """
        Return the cpuacct.usage in cpu seconds
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        path = self._cgroup_path('cpuacct', 'usage', jobid)
        try:
            with open(path, 'r') as desc:
                return int(desc.readline().strip())
        except Exception:
            return None

    def select_cpus(self, path, ncpus):
        """
        Assign CPUs to the cpuset
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: path is %s' % (caller_name(), path))
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: ncpus is %s' %
                   (caller_name(), ncpus))
        if ncpus < 1:
            ncpus = 1
        # Must select from those currently available
        cpufile = os.path.basename(path)
        base = os.path.dirname(path)
        parent = os.path.dirname(base)
        with open(os.path.join(parent, cpufile), 'r') as desc:
            avail = expand_list(desc.read().strip())
        if len(avail) < 1:
            raise CgroupProcessingError('No CPUs available in cgroup')
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Available CPUs: %s' %
                   (caller_name(), avail))
        for filename in glob.glob(os.path.join(parent, '[0-9]*', cpufile)):
            if filename.endswith('.orphan'):
                continue
            with open(filename, 'r') as desc:
                cpus = expand_list(desc.read().strip())
            for entry in cpus:
                if entry in avail:
                    avail.remove(entry)
        if len(avail) < ncpus:
            raise CgroupProcessingError('Insufficient CPUs in cgroup')
        if len(avail) == ncpus:
            return avail
        # TODO: Try to minimize NUMA nodes based on memory requirement
        return avail[:ncpus]

    def _get_error_msg(self, jobid):
        """
        Return the error message in system message file
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        try:
            proc = subprocess.Popen(['dmesg'], shell=False,
                                    stdout=subprocess.PIPE,
                                    universal_newlines=True)
            out = proc.communicate()[0]
            # if we get a non-str type then convert before calling splitlines
            # should not happen since we pass universal_newlines True
            out_split = stringified_output(out).splitlines()
        except Exception:
            return ''
        out_split.reverse()
        # Check to see if the job id is found in dmesg output
        for line in out_split:
            start = line.find('Killed process ')
            if start < 0:
                start = line.find('Task in /%s' % self.cfg['cgroup_prefix'])
            if start < 0:
                continue
            kill_line = line[start:]
            job_start = line.find(jobid)
            if job_start < 0:
                continue
            return kill_line

        # Found nothing -- more recent kernels have different messages
        for line in out_split:
            start = line.find('oom-kill')
            kill_line = line[start:]
            job_start = line.find(jobid)
            if job_start < 0:
                continue
            return kill_line

        return ''

    def write_job_env_file(self, jobid, env_list):
        """
        Write out host cgroup environment for this job
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        jobid = str(jobid)
        if not os.path.exists(self.host_job_env_dir):
            os.makedirs(self.host_job_env_dir, 0o755)
        # Write out assigned_resources
        try:
            lines = "\n".join(env_list)
            filename = self.host_job_env_filename % jobid
            with open(filename, 'w') as desc:
                desc.write(lines)
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Wrote out file: %s' % (filename))
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Data: %s' % (lines))
            return True
        except Exception:
            return False

    def write_cgroup_assigned_resources(self, jobid):
        """
        Write out host cgroup assigned resources for this job
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        jobid = str(jobid)
        if not os.path.exists(self.hook_storage_dir):
            os.makedirs(self.hook_storage_dir, 0o700)
        # Write out assigned_resources
        try:
            json_str = json.dumps(self.assigned_resources)
            filename = os.path.join(self.hook_storage_dir, jobid)
            with open(filename, 'w') as desc:
                desc.write(json_str)
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Wrote out file: %s' %
                       (os.path.join(self.hook_storage_dir, jobid)))
            pbs.logmsg(pbs.EVENT_DEBUG4, 'Data: %s' % (json_str))
            return True
        except Exception:
            return False

    def read_cgroup_assigned_resources(self, jobid):
        """
        Read assigned resources from job file stored in hook storage area
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Method called' % caller_name())
        jobid = str(jobid)
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Host assigned resources: %s' %
                   (self.assigned_resources))
        hrfile = os.path.join(self.hook_storage_dir, jobid)
        if os.path.isfile(hrfile):
            # Read in assigned_resources
            try:
                with open(hrfile, 'r') as desc:
                    json_data = json.load(desc, object_hook=decode_dict)
                self.assigned_resources = json_data
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           'Host assigned resources: %s' %
                           (self.assigned_resources))
            except IOError:
                raise CgroupConfigError('I/O error reading config file')
            except json.JSONDecodeError:
                raise CgroupConfigError(
                    'JSON parsing error reading config file')
        return self.assigned_resources is not None

    def add_jobid_to_cgroup_jobs(self, jobid):
        """
        Add a job ID to the file where local jobs are maintained
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Adding jobid %s to cgroup_jobs' % jobid)
        try:
            with open(self.cgroup_jobs_file, 'r+') as fd:
                jobdict = eval(fd.read())
                if not isinstance(jobdict, dict):
                    pbs.logmsg(pbs.EVENT_ERROR, 'Incompatibly formatted '
                               'cgroup_jobs; emptying it first')
                    jobdict = dict()
                if jobid not in jobdict:
                    jobdict[jobid] = time.time()
                    fd.seek(0)
                    fd.write(str(jobdict))
        except IOError:
            pbs.logmsg(pbs.EVENT_DEBUG, 'Failed to open cgroup_jobs file')
            raise
        except SyntaxError:
            pbs.logmsg(pbs.EVENT_ERROR, 'Incompatibly formatted cgroup_jobs; '
                                        'emptying it first')
            jobdict = dict()
            jobdict[jobid] = time.time()
            try:
                with open(self.cgroup_jobs_file, 'w') as fd:
                    fd.write(str(jobdict))
            except Exception:
                pbs.logsmg('Error adding jobid %s to cgroup_jobs' % jobid)
                self.empty_cgroup_jobs_file()

    def remove_jobid_from_cgroup_jobs(self, jobid):
        """
        Remove a job ID from the file where local jobs are maintained
        """
        pbs.logmsg(pbs.EVENT_DEBUG4,
                   'Removing jobid %s from cgroup_jobs' % jobid)
        try:
            with open(self.cgroup_jobs_file, 'r+') as fd:
                jobdict = eval(fd.read())
                if not isinstance(jobdict, dict):
                    pbs.logmsg(pbs.EVENT_ERROR, 'Incompatibly formatted '
                               'cgroup_jobs; emptying it')
                    jobdict = dict()
                if jobid in jobdict:
                    del jobdict[jobid]
                    fd.seek(0)
                    fd.write(str(jobdict))
                    fd.truncate()
        except IOError:
            pbs.logmsg(pbs.EVENT_DEBUG, 'Failed to open cgroup_jobs file')
            raise
        except SyntaxError:
            pbs.logmsg(pbs.EVENT_ERROR, 'Incompatibly formatted cgroup_jobs; '
                                        'emptying it')
            self.empty_cgroup_jobs_file()

    def read_cgroup_jobs(self):
        """
        Read the file where local jobs are maintained
        """
        jobdict = dict()
        try:
            with open(self.cgroup_jobs_file, 'r') as fd:
                jobdict = eval(fd.read())
                if not isinstance(jobdict, dict):
                    pbs.logmsg(pbs.EVENT_ERROR, 'Incompatibly formatted '
                               'cgroup_jobs; emptying it')
                    jobdict = dict()
                    self.empty_cgroup_jobs_file()
        except IOError:
            pbs.logmsg(pbs.EVENT_DEBUG, 'Failed to open cgroup_jobs file')
            raise
        except SyntaxError:
            pbs.logmsg(pbs.EVENT_ERROR, 'Incompatibly formatted cgroup_jobs; '
                                        'emptying it')
            self.empty_cgroup_jobs_file()
            jobdict = dict()
        cutoff = time.time() - float(self.cfg['job_setup_timeout'])
        result = {key: val for key, val in jobdict.items() if val >= cutoff}
        if len(result) != len(jobdict):
            pbs.logmsg(pbs.EVENT_DEBUG, 'Removing stale jobs from cgroup_jobs')
            try:
                with open(self.cgroup_jobs_file, 'w') as fd:
                    fd.write(str(result))
            except Exception:
                # we tolerate even bad files
                pass
        return result

    def delete_cgroup_jobs_file(self, jobid):
        """
        Delete the file where local jobs are maintained
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Deleting file: %s' %
                   self.cgroup_jobs_file)
        if os.path.isfile(self.cgroup_jobs_file):
            os.remove(self.cgroup_jobs_file)

    def empty_cgroup_jobs_file(self):
        """
        Remove all keys from the file where local jobs are maintained
        """
        pbs.logmsg(pbs.EVENT_DEBUG4, 'Emptying file: %s' %
                   self.cgroup_jobs_file)
        try:
            with open(self.cgroup_jobs_file, 'w') as fd:
                fd.write(str(dict()))
        except IOError:
            pbs.logmsg(pbs.EVENT_DEBUG, 'Failed to open cgroup_jobs file: %s' %
                       self.cgroup_jobs_file)
            raise


def set_global_vars():
    """
    Define some global variables that the hook may use
    """
    global PBS_EXEC
    global PBS_HOME
    global PBS_MOM_HOME
    global PBS_MOM_JOBS
    # Determine location of PBS_HOME, PBS_MOM_HOME, and PBS_EXEC. These
    # should have each be initialized to empty strings near the beginning
    # of this hook.
    # Try the environment first
    if not PBS_EXEC and 'PBS_EXEC' in os.environ:
        PBS_EXEC = os.environ['PBS_EXEC']
    if not PBS_HOME and 'PBS_HOME' in os.environ:
        PBS_HOME = os.environ['PBS_HOME']
    if not PBS_MOM_HOME and 'PBS_MOM_HOME' in os.environ:
        PBS_MOM_HOME = os.environ['PBS_MOM_HOME']
    # Try the built in config values next
    pbs_conf = pbs.get_pbs_conf()
    if pbs_conf:
        if not PBS_EXEC and 'PBS_EXEC' in pbs_conf:
            PBS_EXEC = pbs_conf['PBS_EXEC']
        if not PBS_HOME and 'PBS_HOME' in pbs_conf:
            PBS_HOME = pbs_conf['PBS_HOME']
        if not PBS_MOM_HOME and 'PBS_MOM_HOME' in pbs_conf:
            PBS_MOM_HOME = pbs_conf['PBS_MOM_HOME']
    # Try reading the config file directly
    if not PBS_EXEC or not PBS_HOME or not PBS_MOM_HOME:
        if 'PBS_CONF_FILE' in os.environ:
            pbs_conf_file = os.environ['PBS_CONF_FILE']
        else:
            pbs_conf_file = os.path.join(os.sep, 'etc', 'pbs.conf')
        regex = re.compile(r'\s*([^\s]+)\s*=\s*([^\s]+)\s*')
        try:
            with open(pbs_conf_file, 'r') as desc:
                for line in desc:
                    match = regex.match(line)
                    if match:
                        if not PBS_EXEC and match.group(1) == 'PBS_EXEC':
                            PBS_EXEC = match.group(2)
                        if not PBS_HOME and match.group(1) == 'PBS_HOME':
                            PBS_HOME = match.group(2)
                        if not PBS_MOM_HOME and (match.group(1) ==
                                                 'PBS_MOM_HOME'):
                            PBS_MOM_HOME = match.group(2)
        except Exception:
            pass
    # If PBS_MOM_HOME is not set, use the PBS_HOME value
    if not PBS_MOM_HOME:
        PBS_MOM_HOME = PBS_HOME
    PBS_MOM_JOBS = os.path.join(PBS_MOM_HOME, 'mom_priv', 'jobs')
    # Sanity check to make sure each global path is set
    if not PBS_EXEC:
        raise CgroupConfigError('Unable to determine PBS_EXEC')
    if not PBS_HOME:
        raise CgroupConfigError('Unable to determine PBS_HOME')
    if not PBS_MOM_HOME:
        raise CgroupConfigError('Unable to determine PBS_MOM_HOME')


def missing_str(memspecs):
    if 'vmem' in memspecs and 'mem' in memspecs:
        try:
            if size_as_int(memspecs['vmem']) > 0:
                vmem_size = pbs.size(memspecs['vmem'])
            else:
                vmem_size = pbs.size("0B")
            if size_as_int(memspecs['mem']) > 0:
                mem_size = pbs.size(memspecs['mem'])
            else:
                mem_size = pbs.size("0B")
            if mem_size > vmem_size:
                event.reject("invalid specification: vmem>mem")
            elif mem_size == vmem_size:
                # Avoid creating a "0mb" pbs.size
                return("cgswap=0B")
            else:
                cgswap_size = vmem_size - mem_size
                return ("cgswap="
                        + str(cgswap_size))
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       str(traceback.format_exc().strip().splitlines()))
            event.reject("Invalid (v)mem size requested")

    if 'mem' in memspecs and 'cgswap' in memspecs:
        try:
            if size_as_int(memspecs['mem']) > 0:
                mem_size = pbs.size(memspecs['mem'])
            else:
                mem_size = pbs.size("0B")
            if size_as_int(memspecs['cgswap']) > 0:
                cgswap_size = pbs.size(memspecs['cgswap'])
                vmem_size = mem_size + cgswap_size
            else:
                vmem_size = mem_size
            return ("vmem="
                    + str(vmem_size))
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       str(traceback.format_exc().strip().splitlines()))
            event.reject("Invalid mem or cgswap size requested")

    if 'vmem' in memspecs and 'cgswap' in memspecs:
        try:
            if size_as_int(memspecs['vmem']) > 0:
                vmem_size = pbs.size(memspecs['vmem'])
            else:
                vmem_size = pbs.size("0B")
            # Woraround for bug: no arithmetic on zero pbs.size
            # since user may have specified e.g. "0mb" cgswap
            if size_as_int(memspecs['cgswap']) == 0:
                mem_size = vmem_size
                return ("mem=" + str(mem_size))
            else:
                cgswap_size = pbs.size(memspecs['cgswap'])
                if cgswap_size >= vmem_size:
                    event.reject("invalid specification: mem<=0")
                else:
                    mem_size = vmem_size - cgswap_size
                    return ("mem=" + str(mem_size))
        except Exception:
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       str(traceback.format_exc().strip().splitlines()))
            event.reject("Invalid (v)mem size requested")
    return ""


def fill_cgswap():
    selspec = None
    event = pbs.event()

    job = event.job
    job_o = None
    if event.type == pbs.MODIFYJOB:
        job_o = event.job_o

    if (hasattr(job, 'Resource_List') and 'select' in job.Resource_List
            and job.Resource_List.select):
        # either a submission using -lselect or a qalter specifying -lselect
        selspec = repr(job.Resource_List["select"])
        pbs.logmsg(pbs.EVENT_DEBUG3, "fill_cgswap: original selspec is: %s"
                   % selspec)
        newspec = []
        for chunkspec in selspec.split('+'):
            chunkspec_split = chunkspec.split(':')
            try:
                chunkspec_quantity = int(chunkspec_split[0])
            except Exception:
                event.reject("Invalid number of chunks in -lselect")
            newchunk = chunkspec
            if len(chunkspec_split) == 1:
                newspec.append(newchunk)
            else:
                # more than just a number of chunks
                # -- get resources specified
                memspecs = {}
                for t in chunkspec_split[1:]:
                    if t.startswith('mem='):
                        memstr = t.split('=')[1]
                        memspecs['mem'] = memstr
                    if t.startswith('vmem='):
                        vmemstr = t.split('=')[1]
                        memspecs['vmem'] = vmemstr
                    if t.startswith('cgswap='):
                        cgswapstr = t.split('=')[1]
                        memspecs['cgswap'] = cgswapstr
                if len(memspecs) > 2:
                    event.reject("chunk specification overconstrained, "
                                 "specify only 2 of mem/vmem/cgswap")
                elif len(memspecs) == 2:
                    to_add = missing_str(memspecs)
                    if to_add:
                        newchunk += ":" + to_add
                    newspec.append(newchunk)
                else:
                    newspec.append(newchunk)
        newselspec = '+'.join(newspec)
        pbs.logmsg(pbs.EVENT_DEBUG3, "fill_cgswap: old selspec was: %s, "
                   "new selspec is %s"
                   % (selspec, newselspec))
        job.Resource_List['select'] = pbs.select(newselspec)
        # need to remove possibly stale "global" cgswap
        # if there is a select now but if the job was originally submitted
        # with "old style" per-job resources, as cgswap chunk values are
        # not summed into Resource_List as for vmem and mem
        try:
            job.Resource_List['cgswap'] = None
        except Exception:
            pass
    else:
        # Old style submission without -lselect
        # We set 'global' per-job resources; will fail (by design)
        # if original submission used -lselect.
        to_add = None
        memspecs_o = {}
        memspecs_n = {}
        memspecs = {}
        for res in ['vmem', 'mem', 'cgswap']:
            if (hasattr(job_o, 'Resource_List')
                    and res in job_o.Resource_List
                    and job_o.Resource_List[res] is not None):
                memspecs_o[res] = job_o.Resource_List[res]
            if (hasattr(job, 'Resource_List')
                    and res in job.Resource_List
                    and job.Resource_List[res] is not None):
                memspecs_n[res] = job.Resource_List[res]
        memspecs = {**memspecs_o, **memspecs_n}
        pbs.logmsg(pbs.EVENT_DEBUG3, "memspecs read is: %s"
                   % str(memspecs))

        if len(memspecs) == 3:
            # overconstrained -- we have 3 values for mem, vmem and cgswap
            if (event.type == pbs.QUEUEJOB or len(memspecs_n) > 2):
                event.reject("chunk specification overconstrained, "
                             "specify only 2 of mem/vmem/cgswap")
            else:
                # modifyjob -- values in the new dictionary are controlling
                if len(memspecs_n) == 2:
                    # the two new values determine the third
                    to_add = missing_str(memspecs_n)
                elif len(memspecs_n) == 1:
                    # specified one new value in qalter
                    # -- pick which old one to keep
                    # if new value is cgswap, keep old mem and recompute vmem
                    # if new value is vmem, keep old mem and recompute cgswap
                    # if new value is mem, keep old vmem and recompute cgswap
                    if 'cgswap' in memspecs_n:
                        del memspecs['vmem']
                    else:
                        del memspecs['cgswap']
        if len(memspecs) == 2 and not to_add:
            to_add = missing_str(memspecs)
        if to_add:
            to_add_split = to_add.split('=')
            job.Resource_List[to_add_split[0]] = pbs.size(to_add_split[1])


#
# FUNCTION main
#
def main():
    """
    Main function for execution
    """
    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Function called' % caller_name())
    # If an exception occurs, jobutil must be set to something
    jobutil = None
    hostname = pbs.get_local_nodename()
    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Host is %s' % (caller_name(), hostname))
    # Log the hook event type
    event = pbs.event()
    pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Hook name is %s' %
               (caller_name(), event.hook_name))

    if event.type in [pbs.MODIFYJOB, pbs.QUEUEJOB]:
        fill_cgswap()
        event.accept()

    try:
        set_global_vars()
    except Exception:
        pbs.logmsg(pbs.EVENT_DEBUG,
                   '%s: Hook failed to initialize configuration properly' %
                   caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG,
                   str(traceback.format_exc().strip().splitlines()))
        event.accept()
    # Instantiate the hook utility class
    try:
        hooks = HookUtils()
        pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Hook utility class instantiated' %
                   caller_name())
    except Exception:
        pbs.logmsg(pbs.EVENT_DEBUG,
                   '%s: Failed to instantiate hook utility class' %
                   caller_name())
        pbs.logmsg(pbs.EVENT_DEBUG,
                   str(traceback.format_exc().strip().splitlines()))
        event.accept()
    # Bail out if there is no handler for this event
    if not hooks.hashandler(event.type):
        pbs.logmsg(pbs.EVENT_DEBUG, '%s: %s event not handled by this hook' %
                   (caller_name(), hooks.event_name(event.type)))
        event.accept()
    try:
        # Instantiate the job utility class first so jobutil can be accessed
        # by the exception handlers.
        if hasattr(event, 'job'):
            jobutil = JobUtils(event.job)
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: Job information class instantiated' %
                       caller_name())
        else:
            pbs.logmsg(pbs.EVENT_DEBUG4, '%s: Event does not include a job' %
                       caller_name())
        # Parse the cgroup configuration file here so we can use the file lock
        cfg = CgroupUtils.parse_config_file()
        # Instantiate the cgroup utility class
        vnode = None
        if hasattr(event, 'vnode_list'):
            if hostname in event.vnode_list:
                vnode = event.vnode_list[hostname]
        with Lock(cfg['cgroup_lock_file']):
            # Only write this once we grabbed the lock,
            # otherwise *another* event could actually win the lock
            # even though *this* event printed this message last,
            # and we'd be confused about the event that the winner services
            if event.type == pbs.EXECHOST_PERIODIC:
                loglevel = pbs.EVENT_DEBUG3
            else:
                loglevel = pbs.EVENT_DEBUG2
            if hasattr(event, 'job') and hasattr(event.job, 'id'):
                pbs.logmsg(loglevel, '%s: Event type is %s, job ID is %s'
                           % (caller_name(), hooks.event_name(event.type),
                              event.job.id))
            else:
                pbs.logmsg(loglevel, '%s: Event type is %s'
                           % (caller_name(), hooks.event_name(event.type)))

            cgroup = CgroupUtils(hostname, vnode, cfg=cfg)
            pbs.logmsg(pbs.EVENT_DEBUG4,
                       '%s: Cgroup utility class instantiated' % caller_name())

            # Bail out if there is nothing to do
            if not cgroup.subsystems:
                pbs.logmsg(pbs.EVENT_DEBUG,
                           '%s: Cgroups disabled or none to manage' %
                           caller_name())
                event.accept()

            # Call the appropriate handler
            if hooks.invoke_handler(event, cgroup, jobutil):
                pbs.logmsg(pbs.EVENT_DEBUG4,
                           '%s: Hook handler returned success for %s event' %
                           (caller_name(), hooks.event_name(event.type)))
                event.accept()
            else:
                pbs.logmsg(pbs.EVENT_DEBUG,
                           '%s: Hook handler returned failure for %s event' %
                           (caller_name(), hooks.event_name(event.type)))
                event.reject()
    except SystemExit:
        # The event.accept() and event.reject() methods generate a SystemExit
        # exception.
        pass
    except UserError as exc:
        # User must correct problem and resubmit job, job gets deleted
        msg = ('User error in %s handling %s event' %
               (event.hook_name, hooks.event_name(event.type)))
        if jobutil is not None:
            msg += (' for job %s' % (event.job.id))
            try:
                event.job.delete()
                msg += ' (deleted)'
            except Exception:
                msg += ' (deletion failed)'
        msg += (': %s %s' % (exc.__class__.__name__, str(exc.args)))
        pbs.logmsg(pbs.EVENT_ERROR, msg)
        event.reject(msg)
    except CgroupProcessingError as exc:
        # Something went wrong manipulating the cgroups
        pbs.logmsg(pbs.EVENT_DEBUG,
                   str(traceback.format_exc().strip().splitlines()))
        msg = ('Processing error in %s handling %s event' %
               (event.hook_name, hooks.event_name(event.type)))
        if jobutil is not None:
            msg += (' for job %s' % (event.job.id))
        msg += (': %s %s' % (exc.__class__.__name__, str(exc.args)))
        pbs.logmsg(pbs.EVENT_ERROR, msg)
        event.reject(msg)
    except Exception as exc:
        # Catch all other exceptions and report them, job gets held
        # and a stack trace is logged
        pbs.logmsg(pbs.EVENT_DEBUG,
                   str(traceback.format_exc().strip().splitlines()))
        msg = ('Unexpected error in %s handling %s event' %
               (event.hook_name, hooks.event_name(event.type)))
        if jobutil is not None:
            msg += (' for job %s' % (event.job.id))
            try:
                event.job.Hold_Types = pbs.hold_types('s')
                event.job.rerun()
                msg += ' (system hold set)'
            except Exception:
                msg += ' (system hold failed)'
        msg += (': %s %s' % (exc.__class__.__name__, str(exc.args)))
        pbs.logmsg(pbs.EVENT_ERROR, msg)
        event.reject(msg)


# The following block is skipped if this is a unit testing environment.
if (__name__ == 'builtins') or (__name__ == '__builtin__'):
    START = time.time()
    try:
        main()
    except SystemExit:
        # The event.accept() and event.reject() methods generate a
        # SystemExit exception.
        pass
    except Exception:
        # "Should never happen" since main() is supposed to catch these
        pbs.logmsg(pbs.EVENT_DEBUG,
                   str(traceback.format_exc().strip().splitlines()))
        pbs.event().reject(str(traceback.format_exc().strip().splitlines()))
    finally:
        event = pbs.event()
        if event.type == pbs.EXECHOST_PERIODIC:
            loglevel = pbs.EVENT_DEBUG3
        else:
            loglevel = pbs.EVENT_DEBUG2
        if hasattr(event, 'job') and hasattr(event.job, 'id'):
            pbs.logmsg(loglevel, 'Hook ended: %s, job ID %s, '
                       'event_type %s (elapsed time: %0.4lf)' %
                       (pbs.event().hook_name,
                        event.job.id,
                        str(pbs.event().type),
                        (time.time() - START)))
        else:
            pbs.logmsg(loglevel, 'Hook ended: %s, '
                       'event_type %s (elapsed time: %0.4lf)' %
                       (pbs.event().hook_name,
                        str(pbs.event().type),
                        (time.time() - START)))
