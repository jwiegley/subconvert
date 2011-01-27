/*
 * Copyright (c) 2011, BoostPro Computing.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the
 *   distribution.
 *
 * - Neither the name of BoostPro Computing nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "svndump.h"
#include "gitutil.h"

#include <iostream>
#include <sstream>

struct Options
{
  bool verify;
  bool verbose;
  int  debug;

  Options() : verify(false), verbose(false), debug(0) {}
};

class StatusDisplay
{
  std::ostream& out;
  std::string   verb;
  int           last_rev;
  bool          dry_run;
  bool          debug;
  bool          verbose;

public:
  StatusDisplay(std::ostream&      _out,
                const std::string& _verb     = "Scanning",
                int                _last_rev = -1,
                bool               _dry_run  = false,
                bool               _debug    = false,
                bool               _verbose  = false) :
    out(_out), verb(_verb), last_rev(_last_rev), dry_run(_dry_run),
    debug(_debug), verbose(_verbose) {}

  void set_last_rev(int _last_rev = -1) {
    last_rev = _last_rev;
  }

  void update(const int rev = -1) const {
    out << verb << ": ";
    if (rev != -1) {
      if (last_rev) {
        out << int((rev * 100) / last_rev) << '%'
            << " (" << rev << '/' << last_rev << ')';
      } else {
        out << rev;
      }
    } else {
      out << ", done.";
    }
    out << ((! debug && ! verbose) ? '\r' : '\n');
  }

  void finish() const {
    if (! verbose && ! debug)
      out << '\n';
  }
};

struct FindAuthors
{
  typedef std::map<std::string, int> authors_map;
  typedef authors_map::value_type    authors_value;

  authors_map    authors;
  StatusDisplay& status;
  int            last_rev;

  FindAuthors(StatusDisplay& _status) : status(_status), last_rev(-1) {}

  void operator()(const SvnDump::File& dump, const SvnDump::File::Node&)
  {
    int rev = dump.get_rev_nr();
    if (rev != last_rev) {
      status.update(rev);
      last_rev = rev;

      std::string author = dump.get_rev_author();
      if (! author.empty()) {
        authors_map::iterator i = authors.find(author);
        if (i != authors.end())
          ++(*i).second;
        else
          authors.insert(authors_value(author, 0));
      }
    }
  }

  void report(std::ostream& out) const {
    status.finish();

    for (authors_map::const_iterator i = authors.begin();
         i != authors.end();
         ++i)
      out << (*i).first << "\t\t\t" << (*i).second << '\n';
  }
};

struct FindBranches
{
  struct BranchInfo {
    int         last_rev;
    int         changes;
    std::time_t last_date;

    BranchInfo() : last_rev(0), changes(0), last_date(0) {}
  };

  typedef std::map<boost::filesystem::path, BranchInfo> branches_map;
  typedef branches_map::value_type branches_value;

  branches_map   branches;
  StatusDisplay& status;
  int            last_rev;

  FindBranches(StatusDisplay& _status) : status(_status), last_rev(-1) {}

  void apply_action(int rev, std::time_t date,
                    const boost::filesystem::path& pathname) {
    if (branches.find(pathname) == branches.end()) {
      std::vector<boost::filesystem::path> to_remove;

      for (branches_map::iterator i = branches.begin();
           i != branches.end();
           ++i)
        if (boost::starts_with((*i).first.string(), pathname.string() + '/'))
          to_remove.push_back((*i).first);

      for (std::vector<boost::filesystem::path>::iterator i = to_remove.begin();
           i != to_remove.end();
           ++i)
        branches.erase(*i);

      boost::optional<branches_map::iterator> branch;

      for (branches_map::iterator i = branches.begin();
           i != branches.end();
           ++i)
        if (boost::starts_with(pathname.string(), (*i).first.string() + '/')) {
          branch = i;
          break;
        }

      if (! branch) {
        std::pair<branches_map::iterator, bool> result =
          branches.insert(branches_value(pathname, BranchInfo()));
        assert(result.second);
        branch = result.first;
      }

      if ((**branch).second.last_rev != rev) {
        (**branch).second.last_rev  = rev;
        (**branch).second.last_date = date;
        ++(**branch).second.changes;
      }
    }
  }

  void operator()(const SvnDump::File&       dump,
                  const SvnDump::File::Node& node)
  {
    int rev = dump.get_rev_nr();
    if (rev != last_rev) {
      status.update(rev);
      last_rev = rev;

      if (node.get_action() != SvnDump::File::Node::ACTION_DELETE)
        if (node.get_kind() == SvnDump::File::Node::KIND_DIR) {
          if (node.has_copy_from())
            apply_action(rev, dump.get_rev_date(), node.get_path());
        } else {
          apply_action(rev, dump.get_rev_date(),
                       node.get_path().parent_path());
        }
    }
  }

  void report(std::ostream& out) const {
    status.finish();

    for (branches_map::const_iterator i = branches.begin();
         i != branches.end();
         ++i) {
      char buf[64];
      struct tm * then = std::localtime(&(*i).second.last_date);
      std::strftime(buf, 63, "%Y-%m-%d", then);

      out << ((*i).second.changes == 1 ? "tag" : "branch") << '\t'
          << (*i).second.last_rev << '\t' << buf << '\t'
          << (*i).second.changes << '\t'
          << (*i).first.string() << '\t' << (*i).first.string() << '\n';
    }
  }
};

struct PrintDumpFile
{
  PrintDumpFile(StatusDisplay&) {}

  void operator()(const SvnDump::File&       dump,

                  const SvnDump::File::Node& node)
  {
    { std::ostringstream buf;
      buf << 'r' << dump.get_rev_nr() << ':' << (node.get_txn_nr() + 1);
      std::cout.width(9);
      std::cout << std::right << buf.str() << ' ';
    }

    std::cout.width(8);
    std::cout << std::left;
    switch (node.get_action()) {
    case SvnDump::File::Node::ACTION_NONE:    std::cout << ' ';        break;
    case SvnDump::File::Node::ACTION_ADD:     std::cout << "add ";     break;
    case SvnDump::File::Node::ACTION_DELETE:  std::cout << "delete ";  break;
    case SvnDump::File::Node::ACTION_CHANGE:  std::cout << "change ";  break;
    case SvnDump::File::Node::ACTION_REPLACE: std::cout << "replace "; break;
    }

    std::cout.width(5);
    switch (node.get_kind()) {
    case SvnDump::File::Node::KIND_NONE: std::cout << ' ';     break;
    case SvnDump::File::Node::KIND_FILE: std::cout << "file "; break;
    case SvnDump::File::Node::KIND_DIR:  std::cout << "dir ";  break;
    }

    std::cout << node.get_path();

    if (node.has_copy_from())
      std::cout << " (copied from " << node.get_copy_from_path()
                << " [r" <<  node.get_copy_from_rev() << "])";

    std::cout << '\n';
  }

  void report(std::ostream&) const {}
};

template <typename T>
void invoke_scanner(SvnDump::File&     dump,
                    const std::string& verb = "Reading revisions")
{
  StatusDisplay status(std::cerr, verb);
  T finder(status);

  while (dump.read_next(/* ignore_text= */ true)) {
    status.set_last_rev(dump.get_last_rev_nr());
    finder(dump, dump.get_curr_node());
  }
  finder.report(std::cout);
}

struct ConvertRepository
{
#if 0
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
#endif
};

int main(int argc, char *argv[])
{
  std::ios::sync_with_stdio(false);

  // Examine any option settings made by the user.  -f is the only
  // required one.

  Options opts;

  std::vector<std::string> args;

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == '-') {
        if (std::strcmp(&argv[i][2], "verify") == 0)
          opts.verify = true;
      }
      else if (std::strcmp(&argv[i][1], "v") == 0)
        opts.verbose = true;
      else if (std::strcmp(&argv[i][1], "d") == 0)
        opts.debug = 1;
    } else {
      args.push_back(argv[i]);
    }
  }

  // Any remaining arguments are the command verb and its particular
  // arguments.

  if (args.size() < 2) {
    std::cerr << "usage: subconvert [options] COMMAND DUMP-FILE"
              << std::endl;
    return 1;
  }

  std::string cmd(args[0]);

  if (cmd == "git-test") {
    Git::Repository repo(args[1]);

    std::cerr << "Creating initial commit..." << std::endl;
    Git::CommitPtr commit = repo.create_commit();

    struct tm then;
    strptime("2005-04-07T22:13:13", "%Y-%m-%dT%H:%M:%S", &then);

    std::cerr << "Adding blob to commit..." << std::endl;
    commit->update("foo/bar/baz.c",
                   repo.create_blob("baz.c", "#include <stdio.h>\n", 19));
    commit->update("foo/bar/bar.c",
                   repo.create_blob("bar.c", "#include <stdlib.h>\n", 20));
    // jww (2011-01-27): What about timezones?
    commit->set_author("John Wiegley", "johnw@boostpro.com", std::mktime(&then));
    commit->set_message("This is a sample commit.\n");

    Git::Branch branch("feature");
    std::cerr << "Updating feature branch..." << std::endl;
    branch.update(repo, commit);

    std::cerr << "Cloning commit..." << std::endl;
    commit = commit->clone();   // makes a new commit based on the old one
    std::cerr << "Removing file..." << std::endl;
    commit->remove("foo/bar/baz.c");
    strptime("2005-04-10T22:13:13", "%Y-%m-%dT%H:%M:%S", &then);
    commit->set_author("John Wiegley", "johnw@boostpro.com", std::mktime(&then));
    commit->set_message("This removes the previous file.\n");

    Git::Branch master("master");
    std::cerr << "Updating master branch..." << std::endl;
    master.update(repo, commit);

    return 0;
  }

  SvnDump::File dump(args[1]);

  if (cmd == "print") {
    invoke_scanner<PrintDumpFile>(dump);
  }
  else if (cmd == "authors") {
    invoke_scanner<FindAuthors>(dump);
  }
  else if (cmd == "branches") {
    invoke_scanner<FindBranches>(dump);
  }
  else if (cmd == "convert") {
  }
  else if (cmd == "scan") {
    StatusDisplay status(std::cerr);
    while (dump.read_next(/* ignore_text= */ !opts.verify,
                          /* verify=      */ opts.verify)) {
      status.set_last_rev(dump.get_last_rev_nr());
      if (opts.verbose)
        status.update(dump.get_rev_nr());
    }
    if (opts.verbose)
      status.finish();
  }

  return 0;
}
