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

                                === USAGE ===

STEP ONE

Create a Subversion dump file, using 'svnadmin dump'.

STEP TWO

Run the 'branches' command on the dump file to attempt to guess all the
branches and tags in the repository.  For example:

  PYTHONPATH=svndumptool python SubConvert.py DUMPFILE branches > branches.txt

STEP THREE

Edit the branches.txt file from step two to correctly identify all tags and
branches.  If not used, the resulting Git repository will be flat exactly as
like the Subversion repository it came from.

STEP FOUR

Create an authors.txt file to map Subversion author names to full user names
and e-mail addresses.  This is a tab-separated values file with the following
format:

  NAME<tab>FULLNAME<tab>EMAIL

STEP FOUR

Create a manifest file to identify any and all submodules.  This step is of
course optional.  TODO: Describe the manifest file format.

STEP FIVE

Run the conversion script using all the information from above:

  PYTHONPATH=svndumptool python SubConvert.py \
      --authors=authors.txt \
      --branches=branches.txt \
      --submodules=manifest.txt \
      --verbose convert

                                 === PLAN ===

Development on this script is proceeding according to the following plan:

 1. Ability to print the transactions from a Subversion dump file.

    Using svndumptool, this was simple to accomplish.

 2. Direct translation of a Subversion flat filesystem to a Git flat
    filesystem.

    Completed: 2011-01-22.  NOTE: Requires about 8.5 GB of memory to run.

 3. With assistance from the user, translate a Subversion flat filesystem to a
    Git master branch, with a series of branches and tags representing
    non-trunk entries from that filesystem.

 4. With assistance from the user, split off submodules during conversion from
    flat Subversion to branchified Git.  This will work by having the user
    provide a "manifest" file to identify files in each submodule, with git
    log used to find the name history of each file in that submodule.

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

import Queue
import copy
import cStringIO
import csv
import hashlib
import inspect
import logging
import optparse
import os
import re
import string
import sys
import threading
import time

import svndump

from subprocess import Popen, PIPE

verbose = False
debug   = False

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
        usage = 'usage: %prog [options] [ARGS...]'
        op = self.option_parser = optparse.OptionParser(usage = usage)

        op.add_option('', '--debug',
                      action='store_true', dest='debug',
                      default=False, help='show debug messages and pass exceptions')
        op.add_option('-v', '--verbose',
                      action='store_true', dest='verbose',
                      default=False, help='show informational messages')
        op.add_option('-n', '--dry-run',
                      action='store_true', dest='dry_run',
                      default=False, help='make no changes to the filesystem')
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
            global debug
            debug = True
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

##############################################################################

class GitError(Exception):
    def __init__(self, cmd, args, kwargs, stderr = None):
        self.cmd = cmd
        self.args = args
        self.kwargs = kwargs
        self.stderr = stderr
        Exception.__init__(self)

    def __str__(self):
        if self.stderr:
            return "Git command failed: git %s %s: %s" % \
                (self.cmd, self.args, self.stderr)
        else:
            return "Git command failed: git %s %s" % (self.cmd, self.args)

def git(cmd, *args, **kwargs):
    restart = True
    while restart:
        stdin_mode = None
        if 'input' in kwargs:
            stdin_mode = PIPE

        if verbose:
            print "=> git %s %s" % (cmd, string.join(args, ' '))
            if debug and 'input' in kwargs and \
               ('bulk_input' not in kwargs or not kwargs['bulk_input']):
                print "<<EOF"
                print kwargs['input'],
                print "EOF"

        environ = os.environ.copy()

        for opt, var in [ ('author_name',     'GIT_AUTHOR_NAME')
                        , ('author_date',     'GIT_AUTHOR_DATE')
                        , ('author_email',    'GIT_AUTHOR_EMAIL')
                        , ('committer_name',  'GIT_COMMITTER_NAME')
                        , ('committer_date',  'GIT_COMMITTER_DATE')
                        , ('committer_email', 'GIT_COMMITTER_EMAIL') ]:
            if opt in kwargs and kwargs[opt]:
                environ[var] = kwargs[opt]

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

class GitTerminateQueue(object):
    def write(self): return False

class GitActionQueue(Queue.Queue):
    finishing = False

    def enqueue(self, item):
        #print 'Queue:', item
        self.put_nowait(item)

    def write(self):
        result = True
        while result:
            item = self.get()
            #print 'Write:', item
            result = item.write()
            #print 'Wrote:', item
            self.task_done()
        #print 'End of loop'

    def finish(self):
        self.finishing = True
        #print 'Enqueueing termination'
        self.enqueue(GitTerminateQueue())
        #print 'Joining'
        self.join()
        #print 'End of join'

class GitDebugQueue(object):
    def qsize(self): return 0
    def enqueue(self, obj): obj.write()
    def finish(self): pass

class GitBlob(object):
    sha1       = None
    name       = None
    executable = False
    data       = None
    posted     = False

    def __init__(self, name, data, executable = False):
        self.name       = name
        self.data       = data
        self.executable = executable

    def __repr__(self):
        return "%s = %s" % (object.__repr__(self), self.name)

    def ls(self):
        assert self.sha1
        return "%s blob %s\t%s" % ("100755" if self.executable else "100644",
                                   self.sha1, self.name)

    def post(self, q):
        if self.posted: return
        q.enqueue(self)
        self.posted = True

    def write(self):
        if self.sha1: return
        self.sha1 = git('hash-object', '-w', '--stdin', '--no-filters',
                        input = self.data, bulk_input = True)
        self.data = None        # free up memory
        return True

class GitTree(object):
    sha1    = None
    name    = None
    entries = None
    posted  = False

    def __init__(self, name):
        self.name    = name
        self.entries = {}

    def __repr__(self):
        return "%s = %s" % (object.__repr__(self), self.name)

    def ls(self):
        assert self.sha1
        return "040000 tree %s\t%s" % (self.sha1, self.name)

    def lookup(self, path):
        segments = path.split('/')
        if segments[0] not in self.entries:
            return None

        if len(segments) == 1:
            return self.entries[segments[0]]
        else:
            tree = self.entries[segments[0]]
            assert isinstance(tree, GitTree)
            return tree.lookup(string.join(segments[1:], '/'))

    def update(self, path, obj):
        self.sha1 = None
        self.posted = None
        if debug: print "tree.update:", path, obj

        self.entries = dict(self.entries)

        segments = path.split('/')
        if len(segments) == 1:
            self.entries[segments[0]] = obj
        else:
            if segments[0] not in self.entries:
                tree = GitTree(segments[0])
            else:
                tree = self.entries[segments[0]]
                assert isinstance(tree, GitTree)
                tree = copy.copy(tree)

            tree.update(string.join(segments[1:], '/'), obj)

            self.entries[segments[0]] = tree

    def remove(self, path):
        self.sha1 = None
        self.posted = None
        if debug: print "tree.remove:", path

        segments = path.split('/')
        if segments[0] not in self.entries:
            # It's OK for remove not to find what it's looking for, because it
            # may be that Subversion wishes to remove an empty directory,
            # which would never have been added in the first place.
            return

        self.entries = dict(self.entries)

        if len(segments) > 1:
            tree = self.entries[segments[0]]
            assert isinstance(tree, GitTree)
            tree = copy.copy(tree)

            tree.remove(string.join(segments[1:], '/'))

            self.entries[segments[0]] = tree
        else:
            del self.entries[segments[0]]

    def post(self, q):
        if self.posted: return
        map(lambda x: x.post(q), self.entries.values())
        q.enqueue(self)
        self.posted = True

    def write(self):
        if self.sha1: return
        table = cStringIO.StringIO()
        for entry in self.entries.values():
            if not entry.sha1:
                print >>sys.stderr, \
                    "Not written before parent %s: %s" % (self, entry)
                sys.exit(1)
            table.write(entry.ls())
            table.write('\0')
        table = table.getvalue()

        # It's possible table might be empty, which represents a tree that has
        # no files at all
        self.sha1 = git('mktree', '-z', input = table, bulk_input = True)

        return True

class GitCommit(object):
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
    posted          = False

    def __init__(self, parents = [], name = None):
        self.parents = parents
        self.name    = name

    def write_tree(self, buf, tree, depth=0):
        for entry in sorted(tree.entries.keys()):
            buf.write(' ' * depth)
            buf.write(entry)
            if isinstance(tree.entries[entry], GitTree):
                buf.write('/\n')
                self.write_tree(buf, tree.entries[entry], depth+2)
            else:
                buf.write('\n')

    def dump_str(self):
        buf = cStringIO.StringIO()
        buf.write("Commit %s\n" % (self.sha1 or ""))
        self.write_tree(buf, self.tree)
        return buf.getvalue()

    def fork(self):
        new_commit = GitCommit(parents = [self])
        new_commit.tree = copy.copy(self.tree)
        return new_commit

    def ls(self):
        assert self.sha1
        assert self.name
        return "160000 commit %s\t%s" % (self.sha1, self.name)

    def lookup(self, path):
        if not self.tree:
            return None
        return self.tree.lookup(path)

    def update(self, path, obj):
        self.sha1 = None
        self.posted = None
        if debug: print "commit.update:", path, obj
        if not self.tree:
            self.tree = GitTree(path.split('/')[0])
        self.tree.update(path, obj)

    def remove(self, path):
        self.sha1 = None
        self.posted = None
        if debug: print "commit.remove:", path
        assert self.tree
        try:
            self.tree.remove(path)
        except Exception, err:
            print >>sys.stderr, 'Exception in %s' % self.dump_str()
            raise err

    def post(self, q):
        if self.posted: return
        map(lambda x: x.post(q), self.parents)
        assert self.tree
        self.tree.post(q)
        q.enqueue(self)
        self.posted = True

    def write(self):
        if self.sha1: return
        if not self.tree.sha1:
            print >>sys.stderr, "Commit tree has no SHA1: %s" % self
            sys.exit(1)

        args = ['commit-tree', self.tree.sha1]
        for parent in self.parents:
            args.append('-p')
            assert parent.sha1
            args.append(parent.sha1)

        self.sha1 = git(*args, input = self.comment,
                         author_name     = self.author_name,
                         author_email    = self.author_email,
                         author_date     = self.author_date,
                         committer_name  = self.committer_name  or self.author_name,
                         committer_email = self.committer_email or self.author_email,
                         committer_date  = self.committer_date  or self.author_date)

        self.author_name     = None
        self.author_email    = None
        self.author_date     = None
        self.committer_name  = None
        self.committer_email = None
        self.committer_date  = None
        self.comment         = None

        return True

class GitBranch(object):
    name   = None
    commit = None
    posted = False

    def __init__(self, commit, name='master'):
        self.name   = name
        self.commit = commit

    def post(self, q):
        if self.posted: return
        self.commit.post(q)
        q.enqueue(self)
        self.posted = True

    def write(self):
        assert self.commit.sha1
        git('update-ref', 'refs/heads/%s' % self.name, self.commit.sha1)
        return True

##############################################################################

def revision_iterator(path):
    dump = svndump.file.SvnDumpFile()
    dump.open(path)
    try:
        while dump.read_next_rev():
            txn = 1
            for node in dump.get_nodes_iter():
                yield (dump, txn, node)
                txn += 1
    finally:
        dump.close()

##############################################################################

class SubConvert(CommandLineApp):
    def print_dumpfile(self, path):
        for dump, txn, node in revision_iterator(path):
            print "%9s %-7s %-4s %s%s" % \
                ("r%d:%d" % (dump.get_rev_nr(), txn),
                 node.get_action(), node.get_kind(), node.get_path(),
                 " (copied from %s [r%d])" % \
                     (node.get_copy_from_path(), node.get_copy_from_rev())
                     if node.has_copy_from() else "")

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
                branch = branches[path] = [0, 0]

            if branch[0] != rev:
                branch[0] = rev
                branch[1] += 1

    def find_branches(self, path):
        branches = {}
        last_rev = 0
        for dump, txn, node in revision_iterator(path):
            rev = dump.get_rev_nr()
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
            print "%s\t%d\t%s" % \
                ("tag" if branches[path][1] == 1 else "branch",
                 branches[path][0], path)

    def convert_repository(self, authors, path, worker):
        last_rev = 0
        commit   = None
        activity = False
        mapping  = {}

        for dump, txn, node in revision_iterator(path):
            rev = dump.get_rev_nr()
            if rev != last_rev:
                if not self.options.verbose and not self.options.debug:
                    if not self.options.dry_run:
                        print >>sys.stderr, \
                            "Converting r%6d... (%4d pending git objects)\r" % \
                            (rev, worker.qsize()),
                    else:
                        print >>sys.stderr, "Converting r%d...\r" % rev,
                else:
                    if not self.options.debug:
                        print >>sys.stderr, \
                            "Converting r%d... (%d pending git objects)" % \
                            (rev, worker.qsize())
                    else:
                        print >>sys.stderr, "Converting r%d..." % rev

                mapping[last_rev] = commit

                if commit:
                    # If no files were seen in the previous revision, don't
                    # build a commit and reuse the preceding commit object
                    if activity:
                        if not self.options.dry_run:
                            commit.post(worker)
                        commit = commit.fork()
                        activity = False
                else:
                    commit = GitCommit()

                last_rev = rev

                author = dump.get_rev_author()
                if author in authors:
                    author = authors[author]
                    commit.author_name  = author[0]
                    commit.author_email = author[1]
                else:
                    commit.author_name  = author

                commit.author_date  = re.sub('\..*', '', dump.get_rev_date_str())
                commit.comment = (dump.get_rev_log() + '\nSVN-Revision: %d' % rev)

            path   = node.get_path()
            kind   = node.get_kind()   # file, dir
            action = node.get_action() # add, change, delete, replace

            if kind == 'file' and action in ('add', 'change'):
                activity = True

                if node.has_text():
                    text   = node.text_open()
                    length = node.get_text_length()
                    data   = node.text_read(text, length)
                    assert len(data) == length
                    node.text_close(text)
                    del text

                    if node.has_md5():
                        md5 = hashlib.md5()
                        md5.update(data)
                        assert node.get_text_md5() == md5.hexdigest()
                        del md5
                else:
                    # This is an empty file
                    data = ''

                commit.update(path, GitBlob(os.path.basename(path), data))
                del data

            elif action == 'delete':
                activity = True
                commit.remove(path)

            elif (kind == 'dir' and action in 'add') and node.has_copy_from():
                from_rev = node.get_copy_from_rev()
                while from_rev > 0 and from_rev not in mapping:
                    from_rev -= 1
                assert from_rev
                tree = mapping[from_rev].lookup(node.get_copy_from_path())
                if tree:
                    activity = True
                    commit.update(path, tree)

        if not self.options.verbose and not self.options.debug:
            print

        if not self.options.dry_run:
            commit.post(worker)
        return commit

    def main(self, *args):
        if 'branches' in args:
            if len(args) < 2:
                sys.stderr.write("usage: SubConvert.py <DUMP-FILE>\n")
                sys.exit(1)

            self.find_branches(args[0])

        elif 'print' in args:
            if len(args) < 2:
                sys.stderr.write("usage: SubConvert.py <DUMP-FILE>\n")
                sys.exit(1)

            self.print_dumpfile(args[0])

        elif 'convert' in args:
            if len(args) < 3:
                sys.stderr.write("usage: SubConvert.py <AUTHORS> <DUMP-FILE>\n")
                sys.exit(1)

            authors = {}
            for row in csv.reader(open(args[0]), delimiter='\t'):
                authors[row[0]] = \
                    (row[1], re.sub('~', '.', re.sub('<>', '@', row[2])))

            if not self.options.dry_run:
                if not self.options.debug:
                    worker = GitActionQueue()
                    t = threading.Thread(target=worker.write)
                    t.start()
                else:
                    worker = GitDebugQueue()
            else:
                worker = None

            commit = self.convert_repository(authors, args[1], worker)
            branch = GitBranch(commit)

            if not self.options.dry_run:
                branch.post(worker)
                worker.finish()
                git('symbolic-ref', 'HEAD', 'refs/heads/%s' % branch.name)

        elif 'git-test' in args:

            commit = GitCommit()
            commit.update('foo/bar/baz.c', GitBlob('baz.c', '#include <stdio.h>\n'))
            commit.author_name  = 'John Wiegley'
            commit.author_email = 'johnw@boostpro.com'
            commit.author_date  = '2005-04-07T22:13:13'
            commit.comment      = "This is a sample commit.\n"


            commit = commit.fork()      # makes a new commit based on the old one
            commit.remove('foo/bar/baz.c')
            commit.author_name  = 'John Wiegley'
            commit.author_email = 'johnw@boostpro.com'
            commit.author_date  = '2005-04-10T22:13:13'
            commit.comment      = "This removes the previous file.\n"

            branch.commit = commit
            branch.post(worker)
            worker.finish()

            git('symbolic-ref', 'HEAD', 'refs/heads/%s' % branch.name)

if __name__ == "__main__":
    SubConvert().run()

### SubConvert.py ends here
