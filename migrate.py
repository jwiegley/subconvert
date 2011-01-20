#!/usr/bin/env python
# coding: utf-8

import os
import sys
import string
import cStringIO
import csv

from subprocess import Popen, PIPE

import svndump

verbose = True

if len(sys.argv) < 3:
    sys.stderr.write("usage: migrate.py <AUTHORS> <FILE>\n")
    sys.exit(1)

authors = {}
for row in csv.reader(open(sys.argv[1])):
    authors[row[0]] = (row[1], re.sub('~', '.', re.sub('<>', '@', row[2])))

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
        if len(segments) == 1:
            del self.entries[segments[0]]
        else:
            self.entries[segments[0]].remove(string.join(segments[1:], '/'))

    def write(self):
        if self.sha1: return

        table = cStringIO.StringIO()
        for entry in self.entries.values():
            entry.write()
            table.write(entry.ls())
            table.write('\0')

        self.sha1 = git('mktree', '-z', input = table.getvalue())
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

def read_dumpfile(path):
    dump = svndump.file.SvnDumpFile()
    dump.open(path)

    while dump.read_next_rev():

        txn = 1
        for node in dump.get_nodes_iter():
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
                ("r%d:%d" % (dump.get_rev_nr(), txn),
                 action,
                 kind,
                 path,
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

            if kind == "file" and action in ["add", "change"]:
                text = node.text_open()
                length = node.get_text_length()
                data = node.text_read(text, length)
                git('hash-object', '-w', '--stdin',
                    '--path=%s' % path, '--no-filters', input = data)
                del text
                del data

            # node.write_text_to_file(handle)

            txn += 1

    dump.close()

if __name__ == "__main__":
    #if len(sys.argv) < 2:
    #    sys.stderr.write("usage: migrate.py <FILE>\n")
    #    sys.exit(1)
    #else:
    #    read_dumpfile(sys.argv[1])

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
