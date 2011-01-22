#!/usr/bin/env python
# coding: utf-8

"""Faithfully convert Subversion repositories to Git.

   SubConvert.py, version 1.0

   Copyright (c) 2011, BoostPro Computing.  All rights reserved.

This script replays Subversion dump files as Git actions, yielding a Git
repository which has as close of a 1:1 correspondence with the original
Subversion repository as possible.

Some respects in which this are not possible are:

* Subversion allows for multiple transactions within a single revision, and it
  is possible that some of those transactions may affect more than one branch.
  This must be mapped as two separate Git commits if/when it occurs.

* Subversion supports revisions which modify only directories and/or
  properties of files and directories.  Since Git tracks only files, and has
  no notion of Subversion's properties, these revisions are ignored.

* Subversion models all content in a flat filesystem, such that semantically,
  there is no distinction between branches and tags, except that typically a
  "tag" is a directory which is never modified after initial creation.

  Because proper identification of branches and tags cannot faithfully be done
  hueristically, this script makes a best guess based on activity within all
  revisions, and then outputs a data file for the user to correct before
  performing the final conversion.

* Subversion also tracks version history for multiple projects within this
  same, single filesystem.  This script, if provided with a submodules
  "manifest" file, can create multiple repositories in parallel: one to model
  the original Subversion repository as exactly as possible, with all projects
  conflated in a single filesystem; and a separate repository for each
  submodule.

Note that for efficiency's sake -- to avoid thrashing disk unnecessarily and
taking orders of magnitude more time -- this script performs Git actions
directly, completely bypassing use of a working tree.  That is, instead of
using porcelain commands such as git add, remove, commit, etc., and git
checkout to switch between branches, it uses the underlying plumbing commands:
hash-object, mktree, commit-tree, update-ref, symbolic-ref, etc.  The final
checkout to yield the working tree(s) is done only after all repositories and
their branches and tags have been finalized.

                               === LICENSE ===

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

- Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

- Neither the name of BoostPro Computing nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""

import cStringIO
import csv
import inspect
import logging
import logging.handlers
import optparse
import os
import os
import re
import string
import sys
import sys
import time

import svndump

from subprocess import Popen, PIPE

verbose = True

##############################################################################

LEVELS = {'DEBUG':    logging.DEBUG,
          'INFO':     logging.INFO,
          'WARNING':  logging.WARNING,
          'ERROR':    logging.ERROR,
          'CRITICAL': logging.CRITICAL}

class CommandLineApp(object):
    "Base class for building command line applications."

    force_exit  = True           # If true, always ends run() with sys.exit()
    log_handler = None

    options = {
        'debug':    False,
        'verbose':  False,
        'logfile':  False,
        'loglevel': False
    }

    def __init__(self):
        "Initialize CommandLineApp."
        # Create the logger
        self.log = logging.getLogger(os.path.basename(sys.argv[0]))
        ch = logging.StreamHandler()
        formatter = logging.Formatter("%(name)s: %(levelname)s: %(message)s")
        ch.setFormatter(formatter)
        self.log.addHandler(ch)
        self.log_handler = ch

        # Setup the options parser
        usage = 'usage: %prog [options] <BOUND-IP-ADDRESS>'
        op = self.option_parser = optparse.OptionParser(usage = usage)

        op.add_option('', '--debug',
                      action='store_true', dest='debug',
                      default=False, help='show debug messages and pass exceptions')
        op.add_option('-v', '--verbose',
                      action='store_true', dest='verbose',
                      default=False, help='show informational messages')
        op.add_option('-q', '--quiet',
                      action='store_true', dest='quiet',
                      default=False, help='do not show log messages on console')
        op.add_option('', '--log', metavar='FILE',
                      type='string', action='store', dest='logfile',
                      default=False, help='append logging data to FILE')
        op.add_option('', '--loglevel', metavar='LEVEL',
                      type='string', action='store', dest='loglevel',
                      default=False, help='set log level: DEBUG, INFO, WARNING, ERROR, CRITICAL')
        return

    def main(self, *args):
        """Main body of your application.

        This is the main portion of the app, and is run after all of the
        arguments are processed.  Override this method to implment the primary
        processing section of your application."""
        pass

    def handleInterrupt(self):
        """Called when the program is interrupted via Control-C or SIGINT.
        Returns exit code."""
        self.log.error('Canceled by user.')
        return 1

    def handleMainException(self):
        "Invoked when there is an error in the main() method."
        if not self.options.debug:
            self.log.exception('Caught exception')
        return 1

    ## INTERNALS (Subclasses should not need to override these methods)

    def run(self):
        """Entry point.

        Process options and execute callback functions as needed.  This method
        should not need to be overridden, if the main() method is defined."""
        # Process the options supported and given
        self.options, main_args = self.option_parser.parse_args()

        if self.options.logfile:
            fh = logging.handlers.RotatingFileHandler(self.options.logfile,
                                                      maxBytes = (1024 * 1024),
                                                      backupCount = 5)
            formatter = logging.Formatter("%(asctime)s - %(levelname)s: %(message)s")
            fh.setFormatter(formatter)
            self.log.addHandler(fh)

        if self.options.quiet:
            self.log.removeHandler(self.log_handler)
            ch = logging.handlers.SysLogHandler()
            formatter = logging.Formatter("%(name)s: %(levelname)s: %(message)s")
            ch.setFormatter(formatter)
            self.log.addHandler(ch)
            self.log_handler = ch

        if self.options.loglevel:
            self.log.setLevel(LEVELS[self.options.loglevel])
        elif self.options.debug:
            self.log.setLevel(logging.DEBUG)
        elif self.options.verbose:
            global verbose
            verbose = True
            self.log.setLevel(logging.INFO)
        
        exit_code = 0
        try:
            # We could just call main() and catch a TypeError, but that would
            # not let us differentiate between application errors and a case
            # where the user has not passed us enough arguments.  So, we check
            # the argument count ourself.
            argspec = inspect.getargspec(self.main)
            expected_arg_count = len(argspec[0]) - 1

            if len(main_args) >= expected_arg_count:
                exit_code = self.main(*main_args)
            else:
                self.log.debug('Incorrect argument count (expected %d, got %d)' %
                               (expected_arg_count, len(main_args)))
                self.option_parser.print_help()
                exit_code = 1

        except KeyboardInterrupt:
            exit_code = self.handleInterrupt()

        except SystemExit, msg:
            exit_code = msg.args[0]

        except Exception:
            exit_code = self.handleMainException()
            if self.options.debug:
                raise
            
        if self.force_exit:
            sys.exit(exit_code)
        return exit_code

    def http_uptest(self, proc, hostname = 'localhost', port = 80, url = u'/'):
        import httplib
        conn = httplib.HTTPConnection(hostname, port = port)
        try:
            conn.request("GET", url)
        except Exception:
            self.log.exception('-- http_uptest exception:')
            return False

        resp = conn.getresponse()
        conn.close()
        if resp.status == 200:
            self.log.debug('-- http_uptest succeeded: %s' %
                           (resp.status, resp.reason))
            return True
        else:
            self.log.warning('-- http_uptest FAILED: %s %s' %
                             (resp.status, resp.reason))
            return False

    def spawn_and_wait(self, cmd, *args, **kwargs):
        from subprocess import Popen

        p = Popen((cmd,) + args)

        while True:
            # Wait a bit before checking on the process
            if kwargs.has_key('poll'):
                time.sleep(kwargs['poll'])
            else:
                time.sleep(1)

            # Check whether the process aborted entirely for any reason.  If
            # so, log the fact and then let our outer loop run it again.
            sts = p.poll()
            if sts is not None:
                self.log.info('-- %s exited: %d' % (cmd, sts))
                return sts

            # The process is still running.  Check whether it is still viable
            # by calling the given callback.
            death = False
            try:
                if kwargs.has_key('uptest') and \
                   callable(kwargs['uptest']) and \
                   not kwargs['uptest'](p):
                    death = True

            except Exception:
                self.log.exception('-- %s exception:' % cmd)

            # If the process is no longer viable, we kill it and exit
            if death is True:
                try:
                    import win32api
                    import win32con
                    import win32process

                    handle = win32api.OpenProcess(win32con.PROCESS_ALL_ACCESS,
                                                  True, p.pid)
                    exitcode = win32process.GetExitCodeProcess(handle)
                    if exitcode == win32con.STILL_ACTIVE:
                        win32api.TerminateProcess(handle, 0)
                        self.log.warning('-- %s killed' % cmd)
                except:
                    import signal
                    try: os.kill(p.pid, signal.SIGHUP)
                    except: pass
                    try: os.kill(p.pid, signal.SIGINT)
                    except: pass
                    try: os.kill(p.pid, signal.SIGQUIT)
                    except: pass
                    try: os.kill(p.pid, signal.SIGKILL)
                    except: pass
                    self.log.warning('-- %s killed' % cmd)

                return -1

##############################################################################

def git(cmd, *args, **kwargs):
    restart = True
    while restart:
        stdin_mode = None
        if 'input' in kwargs:
            stdin_mode = PIPE

        if verbose:
            print "=> git %s %s" % (cmd, string.join(args, ' '))
            if 'input' in kwargs:
                print "<<EOF"
                print kwargs['input'],
                print "EOF"

        environ = os.environ.copy()
        
        if 'author_name' in kwargs and kwargs['author_name']:
            environ['GIT_AUTHOR_NAME']     = kwargs['author_name']
        if 'author_email' in kwargs and kwargs['author_email']:
            environ['GIT_AUTHOR_EMAIL']    = kwargs['author_email']
        if 'author_date' in kwargs and kwargs['author_date']:
            environ['GIT_AUTHOR_DATE']     = kwargs['author_date']
        if 'committer_name' in kwargs and kwargs['committer_name']:
            environ['GIT_COMMITTER_NAME']  = kwargs['committer_name']
        if 'committer_email' in kwargs and kwargs['committer_email']:
            environ['GIT_COMMITTER_EMAIL'] = kwargs['committer_email']
        if 'committer_date' in kwargs and kwargs['committer_date']:
            environ['GIT_COMMITTER_DATE']  = kwargs['committer_date']
        if 'email' in kwargs and kwargs['email']:
            environ['EMAIL']               = kwargs['email']

        if 'repository' in kwargs:
            environ['GIT_DIR'] = kwargs['repository']

            git_dir = environ['GIT_DIR']
            if not os.path.isdir(git_dir):
                proc = Popen(('git', 'init'), env = environ,
                             stdout = PIPE, stderr = PIPE)
                if proc.wait() != 0:
                    raise GitError('init', [], {}, proc.stderr.read())

        if 'worktree' in kwargs:
            environ['GIT_WORK_TREE'] = kwargs['worktree']
            work_tree = environ['GIT_WORK_TREE']
            if not os.path.isdir(work_tree):
                os.makedirs(work_tree)

        proc = Popen(('git', cmd) + args, env = environ,
                     stdin  = stdin_mode, stdout = PIPE, stderr = PIPE)

        if 'input' in kwargs:
            input = kwargs['input']
        else:
            input = ''
       
        out, err = proc.communicate(input) 

        returncode = proc.returncode
        restart = False
        ignore_errors = 'ignore_errors' in kwargs and kwargs['ignore_errors']
        if returncode != 0:
            if 'restart' in kwargs:
                if kwargs['restart'](cmd, args, kwargs):
                    restart = True
            elif not ignore_errors:
                raise GitError(cmd, args, kwargs, err)

    if not 'ignore_output' in kwargs:
        if 'keep_newline' in kwargs:
            return out
        else:
            return out[:-1]

##############################################################################

class GitBlob:
    sha1       = None
    name       = None
    executable = False
    data       = None

    def __init__(self, name, data, executable = False):
        self.name       = name
        self.data       = data
        self.executable = executable

    def ls(self):
        assert(self.sha1)
        return "%s blob %s\t%s" % ("100755" if self.executable else "100644",
                                   self.sha1, self.name)

    def write(self):
        self.sha1 = git('hash-object', '-w', '--stdin', '--no-filters',
                        input = self.data)

class GitTree:
    sha1    = None
    name    = None
    entries = None

    def __init__(self, name):
        self.name    = name
        self.entries = {}

    def ls(self):
        assert(self.sha1)
        return "040000 tree %s\t%s" % (self.sha1, self.name)

    def update(self, path, obj):
        self.sha1 = None
        if verbose: print "tree.add:", path, obj

        segments = path.split('/')
        if len(segments) == 1:
            self.entries[segments[0]] = obj
        else:
            if segments[0] not in self.entries:
                self.entries[segments[0]] = GitTree(segments[0])
            self.entries[segments[0]].update(string.join(segments[1:], '/'), obj)

    def remove(self, path):
        self.sha1 = None
        if verbose: print "tree.remove:", path

        segments = path.split('/')
        assert segments[0] in self.entries
        if len(segments) > 1:
            self.entries[segments[0]].remove(string.join(segments[1:], '/'))
        del self.entries[segments[0]]

    def write(self):
        if self.sha1: return

        table = cStringIO.StringIO()
        for entry in self.entries.values():
            entry.write()
            table.write(entry.ls())
            table.write('\0')

        table = table.getvalue()
        # It's possible table might be empty, which represents a tree that has
        # no files at all
        self.sha1 = git('mktree', '-z', input = table)
        del table

class GitCommit:
    sha1            = None
    parents         = []
    tree            = None
    author_name     = None
    author_email    = None
    author_date     = None
    committer_name  = None
    committer_email = None
    committer_date  = None
    comment         = None
    name            = None

    def __init__(self, parents = [], name = None):
        self.parents = parents
        self.name    = name

    def fork(self):
        new_commit = GitCommit(parents = [self.sha1])
        new_commit.tree = self.tree
        return new_commit

    def ls(self):
        assert(self.sha1)
        assert(self.name)
        return "160000 commit %s\t%s" % (self.sha1, self.name)

    def update(self, path, obj):
        self.sha1 = None
        if verbose: print "commit.add:", path, obj
        if not self.tree:
            self.tree = GitTree(path.split('/')[0])
        self.tree.update(path, obj)

    def remove(self, path):
        self.sha1 = None
        if verbose: print "commit.remove:", path
        assert self.tree
        self.tree.remove(path)

    def write(self, comment):
        if self.sha1: return
        self.comment = comment

        assert(self.tree)
        self.tree.write()

        args = ['commit-tree', self.tree.sha1]
        for parent in self.parents:
            args.append('-p')
            args.append(parent)

        self.sha1 = git(*args, input = comment,
                         author_name     = self.author_name,
                         author_email    = self.author_email,
                         author_date     = self.author_date,
                         committer_name  = self.committer_name  or self.author_name,
                         committer_email = self.committer_email or self.author_email,
                         committer_date  = self.committer_date  or self.author_date)

class GitBranch:
    name = None

    def __init__(self, name = 'master'):
        self.name = name

    def update(self, commit):
        git('update-ref', 'refs/heads/%s' % self.name, commit.sha1)

##############################################################################

def revision_iterator(path):
    dump = svndump.file.SvnDumpFile()
    dump.open(path)
    try:
        while dump.read_next_rev():
            txn = 1
            for node in dump.get_nodes_iter():
                yield (dump.get_rev_nr(), txn, node)
                txn += 1
    finally:
        dump.close()

##############################################################################

class SubConvert(CommandLineApp):
    def print_dumpfile(self, path):
        for rev, txn, node in revision_iterator(path):
            path   = node.get_path()
            kind   = node.get_kind()
            action = node.get_action()

            #if action == "add":
            #    assert path not in entities
            #    entities[path] = kind
            #    if kind == "dir" and node.has_copy_from():
            #        #print "copy directory %s -> %s" % (node.get_copy_from_path(), path)
            #        for key, value in entities.items():
            #            if key.startswith(node.get_copy_from_path() + "/"):
            #                newpath = path + key[len(node.get_copy_from_path()) :]
            #                #print "copy entry %s -> %s" % (key, newpath)
            #                entities[newpath] = value
            #elif action == "delete":
            #    assert path in entities
            #
            #    kind = entities[path]
            #    del entities[path]
            #
            #    elements = []
            #    if kind == "dir":
            #        for key in entities.keys():
            #            if key.startswith(path + "/"):
            #                elements.append(key)
            #    for elem in elements:
            #        del entities[elem]
            #else:
            #    assert path not in entities

            print "%9s %-7s %-4s %s%s" % \
                ("r%d:%d" % (rev, txn), action, kind, path,
                 " (copied from %s [r%d])" % \
                     (node.get_copy_from_path(), node.get_copy_from_rev())
                     if node.has_copy_from() else "")

            if kind == "file" and action in ["add", "change"] and \
               node.has_text() and node.has_md5():
                import hashlib
                md5 = hashlib.md5()
                text = node.text_open()
                length = node.get_text_length()
                data = node.text_read(text, length)
                assert len(data) == length
                node.text_close(text); del text
                md5.update(data); del data
                assert node.get_text_md5() == md5.hexdigest()
                del md5

            #if kind == "file" and action in ["add", "change"]:
            #    text = node.text_open()
            #    length = node.get_text_length()
            #    data = node.text_read(text, length)
            #    git('hash-object', '-w', '--stdin',
            #        '--path=%s' % path, '--no-filters', input = data)
            #    del text
            #    del data

            # node.write_text_to_file(handle)

    def apply_action(self, branches, rev, path):
        if path not in branches:
            for key in branches.keys():
                if key.startswith(path + '/'):
                    del branches[key]

            branch = None
            for key in branches.keys():
                if path.startswith(key + '/'):
                    branch = branches[key]

            if not branch:
                branch = branches[path] = GitBranch()
                branch.highest_revision  = 0
                branch.revisions_applied = 0

            if branch.highest_revision != rev:
                branch.highest_revision = rev
                branch.revisions_applied += 1

    def find_branches(self, path):
        branches = {}
        last_rev = 0
        for rev, txn, node in revision_iterator(path):
            if rev != last_rev:
                print >>sys.stderr, "Scanning r%s...\r" % rev,
                last_rev = rev

            if node.get_action() != 'delete':
                if node.get_kind() == 'dir':
                    if node.has_copy_from():
                        self.apply_action(branches, rev, node.get_path())
                else:
                    self.apply_action(branches, rev,
                                      os.path.dirname(node.get_path()))

        for path in sorted(branches.keys()):
            print "%s\t%s" % \
                ("tag" if branches[path].revisions_applied == 1 else "branch", path)

    def main(self, *args):
        if len(args) < 3:
            sys.stderr.write("usage: migrate.py <AUTHORS> <FILE>\n")
            sys.exit(1)

        self.authors = {}
        for row in csv.reader(open(args[0])):
            self.authors[row[0]] = \
                (row[1], re.sub('~', '.', re.sub('<>', '@', row[2])))

        if "branches" in args:
            self.find_branches(args[1])

        elif "print" in args:
            self.print_dumpfile(args[1])

        elif "git-test" in args:
            commit = GitCommit()
            commit.update('foo/bar/baz.c', GitBlob('baz.c', '#include <stdio.h>\n'))
            commit.author_name  = 'John Wiegley'
            commit.author_email = 'johnw@boostpro.com'
            commit.author_date  = '2005-04-07T22:13:13'
            commit.write("This is a sample commit.\n")

            branch = GitBranch()
            branch.update(commit)

            commit = commit.fork()      # makes a new commit similar to the old one
            commit.remove('foo/bar/baz.c')
            commit.author_name  = 'John Wiegley'
            commit.author_email = 'johnw@boostpro.com'
            commit.author_date  = '2005-04-10T22:13:13'
            commit.write("This removes the previous file.\n")

            branch.update(commit)

            git('symbolic-ref', 'HEAD', 'refs/heads/%s' % branch.name)

if __name__ == "__main__":
    SubConvert().run()

### SubConvert.py ends here
