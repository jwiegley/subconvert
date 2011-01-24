#!/usr/bin/env python
# coding: utf-8

version = "1.0"

"""SubConvert.py: Faithfully convert Subversion repositories to Git.

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
import bz2
import gzip
import cPickle
import cStringIO
import copy
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

log     = None
verbose = False
debug   = False

##############################################################################

LEVELS = {'DEBUG':    logging.DEBUG,
          'INFO':     logging.INFO,
          'WARNING':  logging.WARNING,
          'ERROR':    logging.ERROR,
          'CRITICAL': logging.CRITICAL}

class OptionParserExt(optparse.OptionParser):
    def print_help(self):
        optparse.OptionParser.print_help(self)

        print '''
Commands:
  authors               Print initial content for the --authors file
  branches              Print initial content for the --branches file
  convert               Replay dump file (-f) into Git repository in *current directory*
  print                 Show a summary of the dump file's contents
'''

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
        global log
        log = self.log = logging.getLogger(os.path.basename(sys.argv[0]))
        ch = logging.StreamHandler()
        formatter = logging.Formatter("%(name)s: %(levelname)s: %(message)s")
        ch.setFormatter(formatter)
        self.log.addHandler(ch)
        self.log_handler = ch

        # Setup the options parser
        usage = 'usage: %prog [options] [ARGS...] <COMMAND>'
        op = self.option_parser = \
            OptionParserExt(usage = usage,
                            version = "%%prog %s, by John Wiegley" % version)

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
                      default=False,
                      help='set log level: DEBUG, INFO, WARNING, ERROR, CRITICAL')

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

        global debug, verbose

        if self.options.loglevel:
            self.log.setLevel(LEVELS[self.options.loglevel])
        elif self.options.debug:
            debug   = True
            verbose = True
            self.log.setLevel(logging.DEBUG)
        elif self.options.verbose:
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
    def __init__(self, msg):
        self.msg = msg
        Exception.__init__(self)

    def __str__(self):
        return "Exception: %s" % self.msg

def git(cmd, *args, **kwargs):
    restart = True
    while restart:
        stdin_mode = None
        if 'input' in kwargs:
            stdin_mode = PIPE

        log.info("=> git %s %s" % (cmd, string.join(args, ' ')))
        if debug and 'input' in kwargs and \
           ('bulk_input' not in kwargs or not kwargs['bulk_input']):
            log.debug("<<EOF")
            log.debug(kwargs['input'])
            log.debug("EOF")

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

class GitThreadedQueue(Queue.Queue):
    finishing = False
    status    = None

    def enqueue(self, item):
        self.put_nowait(item)

    def write(self):
        result = True
        while result:
            item = self.get()
            result = item.write()
            self.task_done()

            if self.status and \
               not isinstance(item, GitBlob) and \
               not isinstance(item, GitTree):
                self.status.update(self)

    def finish(self, status=None):
        self.status    = status
        self.finishing = True
        self.enqueue(GitTerminateQueue())
        self.join()

class GitLinearQueue(object):
    pending = None

    def __init__(self):
        self.pending = []

    def qsize(self):
        return len(self.pending)

    def enqueue(self, item):
        log.debug('Queuing %s' % item)
        self.pending.append(item)

    def finish(self, status=None):
        while self.qsize() > 0:
            item = self.pending[0]
            self.pending.pop(0)

            log.debug('Writing %s' % item)
            item.write()

            if status and \
               not isinstance(item, GitBlob) and \
               not isinstance(item, GitTree):
                status.update(self)

##############################################################################

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
        return "%s[%s]" % (object.__repr__(self), self.name)

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
        assert self.sha1
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
        return "%s[%s]" % (object.__repr__(self), self.name)

    def ls(self):
        assert self.sha1
        return "040000 tree %s\t%s" % (self.sha1, self.name)

    def lookup(self, path):
        segments = path.split('/')
        if segments[0] not in self.entries:
            return None

        if len(segments) == 1:
            result = self.entries[segments[0]]
            assert result.name == segments[0]
        else:
            tree = self.entries[segments[0]]
            assert isinstance(tree, GitTree)
            result = tree.lookup(string.join(segments[1:], '/'))

        return result

    def update(self, path, obj):
        self.sha1   = None
        self.posted = None

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
        self.sha1   = None
        self.posted = None

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
                raise GitError("Tree not written before parent %s: %s" % (self, entry))
            table.write(entry.ls())
            table.write('\0')
        table = table.getvalue()

        # It's possible table might be empty, which represents a tree that has
        # no files at all
        self.sha1 = git('mktree', '-z', input = table, bulk_input = True)
        assert self.sha1

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
    prefix          = ''
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
        buf.write("  Log: %s\n" % (self.comment or ""))
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
        if not self.tree: return None
        return self.tree.lookup(path)

    def update(self, path, obj):
        log.debug('commit.update: ' + path)
        self.sha1   = None
        self.posted = None
        if not self.tree:
            self.tree = GitTree(path.split('/')[0])
        self.tree.update(path, obj)

    def remove(self, path):
        log.debug('commit.remove: ' + path)
        self.sha1   = None
        self.posted = None
        if self.tree:
            self.tree.remove(path)

    def post(self, q):
        if self.posted: return
        map(lambda x: x.post(q), self.parents)
        if self.tree:
            if self.prefix:
                tree = self.tree.lookup(self.prefix)
            else:
                tree = self.tree
            if tree:
                assert isinstance(tree, GitTree)
                tree.post(q)
        q.enqueue(self)
        self.posted = True

    def write(self):
        if self.sha1: return

        if self.prefix:
            tree = self.tree.lookup(self.prefix)
        else:
            tree = self.tree

        if not tree:
            tree = GitTree(self.prefix) # create an empty tree
            tree.write()

        if not tree.sha1:
            raise GitError('Commit tree has no SHA1: %s' % self.dump_str())

        if self.prefix:
            log.debug('committing tree %s (prefix %s)' % (tree.name, self.prefix))
        else:
            log.debug('committing tree %s' % tree.name)

        args = ['commit-tree', tree.sha1]
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
        assert self.sha1

        self.author_name     = None
        self.author_email    = None
        self.author_date     = None
        self.committer_name  = None
        self.committer_email = None
        self.committer_date  = None
        self.comment         = None

        return True

class GitBranch(object):
    name      = None
    prefix    = ''
    prefix_re = None
    commit    = None
    kind      = 'branch'
    final_rev = 0
    posted    = False

    def __init__(self, name='master', commit=None):
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

class SvnDumpFile(svndump.file.SvnDumpFile):
    def __init__(self):
        svndump.file.SvnDumpFile.__init__(self)

    def open_handle( self, filename, handle ):
        """
        Open a dump file for reading and read the header.
        @type filename: string
        @param filename: Name of an existing dump file.
        """

        # check state
        if self.__state != self.ST_NONE:
            raise SvnDumpException, "invalid state %d (should be %d)" % \
                        ( self.__state, self.ST_NONE )

        # set parameters
        self.__filename = filename
        self.__file     = handle

        # check that it is a svn dump file
        tag = self.__get_tag( True )
        if tag[0] != "SVN-fs-dump-format-version:":
            raise SvnDumpException, "not a svn dump file ???"
        if tag[1] != "2":
            raise SvnDumpException, "wrong svn dump file version (expected 2 found %s)" % ( tag[1] )
        self.__skip_empty_line()

        # get UUID
        fileoffset = self.__file.tell()
        tag = self.__get_tag( True )
        if len( tag ) < 1 or tag[0] != "UUID:":
            # back to start of revision
            self.__file.seek( fileoffset )
            self.__uuid = None
        else:
            # set UUID
            self.__uuid = tag[1]
            self.__skip_empty_line()

        # done initializing
        self.__rev_start_offset = self.__file.tell()
        self.__state = self.ST_READ

def revision_iterator(path):
    dump = SvnDumpFile()

    ext = os.path.splitext(path)[1]
    if ext in ('.bz', '.bz2'):
        dump.open_handle(path, bz2.BZ2File(path, 'rb'))
    elif ext in ('.z', '.Z', '.gz'):
        dump.open_handle(path, gzip.GzipFile(path, 'rb'))
    else:
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

class StatusDisplay:
    last_rev = 0

    def __init__(self, verb, dry_run, debug, verbose):
        self.verb    = verb
        self.dry_run = dry_run
        self.debug   = debug
        self.verbose = verbose

    def update(self, worker, rev=None):
        if rev:
            self.last_rev = rev
        else:
            rev = self.last_rev

        if worker and not self.dry_run:
            sys.stderr.write("%s %8s... (%9d pending git objects)" %
                             (self.verb, 'r%d' % rev, worker.qsize()))
        else:
            sys.stderr.write("%s r%d..." % (self.verb, rev))

        sys.stderr.write('\r' if not self.debug and not self.verbose else '\n')

    def finish(self):
        if not self.verbose and not self.debug:
            sys.stderr.write('\n')

class SubConvert(CommandLineApp):
    def __init__(self):
        CommandLineApp.__init__(self)

        self.usage = "usage: %prog [OPTIONS] <DUMP-FILE> <COMMAND>"

        op = self.option_parser

        op.add_option('-f', '--file', metavar='FILE',
                      type='string', action='store', dest='dump_file',
                      default=None, help='pathname of Subversion dump file')
        op.add_option('', '--state', metavar='FILE',
                      type='string', action='store', dest='state_file',
                      default=None, help='pathname of SubConvert state file')
        op.add_option('-V', '--verify',
                      action='store_true', dest='verify',
                      default=False, help='verify dump file contents')
        op.add_option('', '--linear',
                      action='store_true', dest='linear',
                      default=False, help='do not use a threaded action queue')
        op.add_option('', '--cutoff', metavar='REV',
                      type='int', action='store', dest='cutoff_rev',
                      default=0, help='stop working at revision REV')
        op.add_option('-A', '--authors', metavar='FILE',
                      type='string', action='store', dest='authors_file',
                      default=None, help='pathname of author name mapping file')
        op.add_option('-B', '--branches', metavar='FILE',
                      type='string', action='store', dest='branches_file',
                      default=None, help='pathname of branch/tag mapping file')
        op.add_option('-M', '--modules', metavar='FILE',
                      type='string', action='store', dest='modules_file',
                      default=None, help='pathname of submodule mapping file')

    def print_dumpfile(self):
        for dump, txn, node in revision_iterator(self.options.dump_file):
            print "%9s %-7s %-4s %s%s" % \
                ("r%d:%d" % (dump.get_rev_nr(), txn),
                 node.get_action(), node.get_kind(), node.get_path(),
                 " (copied from %s [r%d])" % \
                     (node.get_copy_from_path(), node.get_copy_from_rev())
                     if node.has_copy_from() else "")

    def find_authors(self):
        authors  = {}
        last_rev = 0
        status   = StatusDisplay('Scanning', True, self.options.debug,
                                 self.options.verbose)

        for dump, txn, node in revision_iterator(self.options.dump_file):
            rev = dump.get_rev_nr()
            if rev != last_rev:
                status.update(None, rev)
                last_rev = rev

            if dump.get_rev_author() in authors:
                authors[dump.get_rev_author()] += 1
            else:
                authors[dump.get_rev_author()] = 0

        status.finish()

        for author in sorted(authors.keys()):
            print "%s\t%s\t%s\t%d" % (author, '', '', authors[author])

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

    def find_branches(self):
        branches = {}
        last_rev = 0
        status   = StatusDisplay('Scanning', True, self.options.debug,
                                 self.options.verbose)

        for dump, txn, node in revision_iterator(self.options.dump_file):
            rev = dump.get_rev_nr()
            if rev != last_rev:
                status.update(None, rev)
                last_rev = rev

            if node.get_action() != 'delete':
                if node.get_kind() == 'dir':
                    if node.has_copy_from():
                        self.apply_action(branches, rev, node.get_path())
                else:
                    self.apply_action(branches, rev,
                                      os.path.dirname(node.get_path()))

        status.finish()

        for path in sorted(branches.keys()):
            print "%s\t%d\t%s" % \
                ("tag" if branches[path][1] == 1 else "branch",
                 branches[path][0], path)

    def current_commit(self, dump, path, copy_from=None):
        current_branch = None

        for branch in self.branches:
            if branch.prefix_re.match(path):
                assert not current_branch
                current_branch = branch
                if not self.options.verify:
                    break

        if not current_branch:
            current_branch = self.default_branch

        if not current_branch.commit:
            # If the first action is a dir/add/copyfrom, then this will get
            # set correctly, otherwise it's a parentless branch, which is also
            # completely OK.
            commit = current_branch.commit = \
                copy_from.fork() if copy_from else GitCommit()
            self.log.info('Found new branch %s' % current_branch.name)
        else:
            commit = current_branch.commit.fork()
            current_branch.commit = commit

        # This copy is done to avoid each commit having to link back to its
        # parent branch
        commit.prefix = current_branch.prefix

        # Setup the author and commit comment
        author = dump.get_rev_author()
        if author in self.authors:
            author = self.authors[author]
            commit.author_name  = author[0]
            commit.author_email = author[1]
        else:
            commit.author_name  = author

        commit.author_date = re.sub('\..*', '', dump.get_rev_date_str())
        log                = dump.get_rev_log().rstrip(' \t\n\r')
        commit.comment     = log + '\n\nSVN-Revision: %d' % dump.get_rev_nr()

        return commit

    def setup_conversion(self):
        self.authors = {}
        if self.options.authors_file:
            for row in csv.reader(open(self.options.authors_file),
                                  delimiter='\t'):
                self.authors[row[0]] = \
                    (row[1], re.sub('~', '.', re.sub('<>', '@', row[2])))

        self.branches = []
        if self.options.branches_file:
            for row in csv.reader(open(self.options.branches_file),
                                  delimiter='\t'):
                branch = GitBranch(row[2])
                branch.kind      = row[0]
                branch.final_rev = int(row[1])
                branch.prefix    = row[2]
                branch.prefix_re = re.compile(re.escape(branch.prefix) + '(/|$)')
                self.branches.append(branch)

        self.last_rev       = 0
        self.rev_mapping    = {}
        self.commit         = None
        self.default_branch = GitBranch('master')

    def convert_repository(self, worker):
        activity    = False
        status      = StatusDisplay('Converting', self.options.dry_run,
                                    self.options.debug, self.options.verbose)

        for dump, txn, node in revision_iterator(self.options.dump_file):
            rev = dump.get_rev_nr()
            if rev != self.last_rev:
                if self.options.cutoff_rev and rev > self.options.cutoff_rev:
                    self.log.info("Terminated at nearest cutoff revision %d%s" %
                                  (rev, ' ' * 20))
                    break

                status.update(worker, rev)

                if self.commit:
                    self.rev_mapping[self.last_rev] = self.commit

                    # If no activity was seen in the previous revision, don't
                    # build a commit and just reuse the preceding commit
                    # object (if there is one yet)
                    if activity:
                        if not self.options.dry_run:
                            self.commit.post(worker)
                        self.commit = None

                # Skip revisions we've already processed from a state file
                # that was cut short using --cutoff
                if rev < self.last_rev:
                    continue
                else:
                    self.last_rev = rev

            path = node.get_path()
            if not path: continue

            kind   = node.get_kind()   # file|dir
            action = node.get_action() # add|change|delete|replace

            if kind == 'file' and action in ('add', 'change'):
                if node.has_text():
                    text   = node.text_open()
                    length = node.get_text_length()
                    data   = node.text_read(text, length)
                    assert len(data) == length
                    node.text_close(text)
                    del text

                    if self.options.verify and node.has_md5():
                        md5 = hashlib.md5()
                        md5.update(data)
                        assert node.get_text_md5() == md5.hexdigest()
                        del md5
                else:
                    data = ''   # an empty file

                if not self.commit:
                    self.commit = self.current_commit(dump, path)
                self.commit.update(path, GitBlob(os.path.basename(path), data))
                del data

                activity = True

            elif action == 'delete':
                if not self.commit:
                    self.commit = self.current_commit(dump, path)
                self.commit.remove(path)
                activity = True

            elif (kind == 'dir' and action in 'add') and node.has_copy_from():
                from_rev = node.get_copy_from_rev()
                while from_rev > 0 and from_rev not in self.rev_mapping:
                    from_rev -= 1
                assert from_rev

                past_commit = self.rev_mapping[from_rev]
                if not self.commit:
                    assert past_commit
                    self.commit = self.current_commit(dump, path,
                                                      copy_from=past_commit)

                log.debug("dir add, copy from: " + node.get_copy_from_path())
                log.debug("dir add, copy to:   " + path)

                tree = past_commit.lookup(node.get_copy_from_path())
                if tree:
                    tree = copy.copy(tree)
                    tree.name = os.path.basename(path)

                    # If this tree hasn't been written yet (the SHA1 would be
                    # the exact same), make sure our copy gets written on its
                    # own terms to avoid a dependency ordering snafu
                    if not tree.sha1:
                        tree.posted = False

                    self.commit.update(path, tree)
                    activity = True
                else:
                    activity = False

        if not self.options.dry_run:
            for branch in self.branches + [self.default_branch]:
                if branch.commit:
                    branch.post(worker)

            worker.finish(status)

        status.finish()

    def main(self, *args):
        if 'print' in args and self.options.dump_file:
            self.print_dumpfile()

        elif 'authors' in args and self.options.dump_file:
            self.find_authors()

        elif 'branches' in args and self.options.dump_file:
            self.find_branches()

        elif 'convert' in args and self.options.dump_file:
            if not os.path.isdir('.git'):
                self.log.error('No .git directory in current working directory!')
                return 1

            if not self.options.dry_run:
                if not self.options.linear:
                    worker = GitThreadedQueue()
                    t = threading.Thread(target=worker.write)
                    t.start()
                else:
                    worker = GitLinearQueue()
            else:
                worker = None

            self.setup_conversion()
            self.convert_repository(worker)

            #git('symbolic-ref', 'HEAD', 'refs/heads/%s' % branch.name)

        elif 'git-test' in args:
            commit = GitCommit()
            commit.update('foo/bar/baz.c', GitBlob('#include <stdio.h>\n'))
            commit.author_name  = 'John Wiegley'
            commit.author_email = 'johnw@boostpro.com'
            commit.author_date  = '2005-04-07T22:13:13'
            commit.comment      = "This is a sample commit.\n"

            branch = GitBranch('feature', commit)
            branch.post(worker)

            commit = commit.fork()      # makes a new commit based on the old one
            commit.remove('foo/bar/baz.c')
            commit.author_name  = 'John Wiegley'
            commit.author_email = 'johnw@boostpro.com'
            commit.author_date  = '2005-04-10T22:13:13'
            commit.comment      = "This removes the previous file.\n"

            branch = GitBranch('master', commit)
            branch.post(worker)
            worker.finish()

            git('symbolic-ref', 'HEAD', 'refs/heads/%s' % branch.name)

        else:
            self.option_parser.print_help()
            self.exit_code = 1

if __name__ == "__main__":
    state_file = None
    for arg in sys.argv[1:]:
        match = re.match('--state=(%s)', arg)
        if match:
            state_file = match.group(1)
            break

    if state_file and os.path.isfile(state_file):
        with open(state_file, 'rb') as fd:
            convert = cPickle.load(fd)
    else:
        convert = SubConvert()

    convert.run()

    if state_file:
        with open(state_file, 'wb') as fd:
            cPickle.dump(convert, fd)

### SubConvert.py ends here
