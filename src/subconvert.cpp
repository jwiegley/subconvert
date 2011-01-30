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
#include <vector>
#include <set>

#ifndef ASSERTS
#undef assert
#define assert(x)
#endif

struct Options
{
  bool verbose;
  int  debug;
  bool verify;
  int  cutoff;

  boost::filesystem::path authors_file;
  boost::filesystem::path branches_file;
  boost::filesystem::path modules_file;

  Options() : verbose(false), debug(0), verify(false), cutoff(0) {}
};

class StatusDisplay : public boost::noncopyable
{
  std::ostream& out;
  std::string   verb;
  int           last_rev;
  mutable bool  need_newline;
  Options       opts;

public:
  StatusDisplay(std::ostream&      _out,
                const Options&     _opts = Options(),
                const std::string& _verb = "Scanning") :
    out(_out), verb(_verb), last_rev(-1), need_newline(false),
    opts(_opts) {}

  void set_last_rev(int _last_rev = -1) {
    last_rev = _last_rev;
  }

  void info(const std::string& message) {
    if (opts.verbose || opts.debug) {
      out << message << std::endl;
      need_newline = false;
    }
  }

  void newline() const {
    if (need_newline) {
      out << std::endl;
      need_newline = false;
    }
  }

  void warn(const std::string& message) {
    newline();
    out << message << std::endl;
    need_newline = false;
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

    //if (! opts.debug && ! opts.verbose) {
      out << '\r';
      need_newline = true;
    //} else {
    //  newline();
    //}
  }

  void finish() const {
    newline();
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

  void report(std::ostream& out)
  {
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
                    const boost::filesystem::path& pathname)
  {
    boost::optional<branches_map::iterator> branch;

    branches_map::iterator i = branches.find(pathname);
    if (i != branches.end()) {
      branch = i;
    } else {
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
    }

    if ((**branch).second.last_rev != rev) {
      (**branch).second.last_rev  = rev;
      (**branch).second.last_date = date;
      ++(**branch).second.changes;
    }
  }

  void operator()(const SvnDump::File&       dump,
                  const SvnDump::File::Node& node)
  {
    int rev = dump.get_rev_nr();
    if (rev != last_rev) {
      status.update(rev);
      last_rev = rev;
    }

    if (node.get_action() != SvnDump::File::Node::ACTION_DELETE &&
        (node.get_kind()  == SvnDump::File::Node::KIND_FILE ||
         node.has_copy_from()))
      apply_action(rev, dump.get_rev_date(),
                   node.get_kind() == SvnDump::File::Node::KIND_DIR ?
                   node.get_path() : node.get_path().parent_path());
  }

  void report(std::ostream& out)
  {
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

  void report(std::ostream&) {}
};

struct ConvertRepository
{
  struct AuthorInfo {
    std::string name;
    std::string email;
  };

  typedef std::map<std::string, AuthorInfo> authors_map;
  typedef authors_map::value_type           authors_value;

  typedef std::map<boost::filesystem::path, Git::BranchPtr> branches_map;
  typedef branches_map::value_type                          branches_value;

  StatusDisplay&              status;
  Options                     opts;
  Git::Repository&            repository;
  int                         last_rev;
  authors_map                 authors;
  std::set<std::string>       unrecognized_authors;
  branches_map                branches;
  std::vector<Git::BranchPtr> deleted_branches;
  std::vector<Git::CommitPtr> commit_queue;
  Git::BranchPtr              default_branch;

  ConvertRepository(Git::Repository& _repository, StatusDisplay& _status,
                    const Options& _opts = Options())
    : status(_status), opts(_opts), repository(_repository),
      last_rev(0), default_branch(new Git::Branch()) {}

  void load_authors(const boost::filesystem::path& pathname)
  {
    authors.clear();

    static const int MAX_LINE = 8192;
    char linebuf[MAX_LINE + 1];

    boost::filesystem::ifstream in(pathname);

    while (in.good() && ! in.eof()) {
      in.getline(linebuf, MAX_LINE);

      int         field = 0;
      std::string author_id;
      AuthorInfo  author;
      for (const char * p = std::strtok(linebuf, "\t"); p;
           p = std::strtok(NULL, "\t")) {
        switch (field) {
        case 0:
          author_id = p;
          break;
        case 1:
          author.name = p;
          break;
        case 2: {
          char buf[256];
          char * s = buf;
          for (const char * q = p; *q; ++q, ++s) {
            if (*q == '<' && *(q + 1) == '>') {
              *s = '@';
              ++q;
            }
            else if (*q == '~') {
              *s = '.';
            }
            else {
              *s = *q;
            }
          }
          *s = '\0';
          author.email = buf;
          break;
        }
        }
        field++;
      }

      authors.insert(authors_value(author_id, author));
    }
  }

  void load_branches(const boost::filesystem::path& pathname)
  {
    branches.clear();
    deleted_branches.clear();

    static const int MAX_LINE = 8192;
    char linebuf[MAX_LINE + 1];

    boost::filesystem::ifstream in(pathname);

    while (in.good() && ! in.eof()) {
      in.getline(linebuf, MAX_LINE);

      Git::BranchPtr branch(new Git::Branch);
      int            field  = 0;
      bool           is_tag = false;

      for (const char * p = std::strtok(linebuf, "\t"); p;
           p = std::strtok(NULL, "\t")) {
        switch (field) {
        case 0:
          if (*p == 't')
            is_tag = true;
          break;
        case 1: break;
        case 2: break;
        case 3: break;
        case 4:
          branch->prefix = p;
          break;
        case 5:
          branch->name = p;
          break;
        }
        field++;
      }

      branches.insert(branches_value(branch->prefix, branch));
    }
  }

  void load_modules(const boost::filesystem::path& /*pathname*/)
  {
    // jww (2011-01-29): NYI
  }

  void load_revmaps_for_branch(Git::BranchPtr branch)
  {
    // jww (2011-01-29): Need to walk through the entire repository, to
    // find all the commits.

    for (Git::Repository::commit_iterator i = repository.commits_begin();
         i != repository.commits_end();
         ++i) {
      std::string            message = (*i)->get_message();
      std::string::size_type offset  = message.find("SVN-Revision: ");
      if (offset == std::string::npos)
        throw std::logic_error("Cannot work with a repository"
                               " not created by subconvert");
      offset += 14;

      assert((*i)->get_oid());
#ifdef READ_FROM_DISK
      branch->rev_map.insert
        (Git::Branch::revs_value(std::atoi(message.c_str() + offset),
                                 (*i)->get_oid()));
#else
      branch->rev_map.insert
        (Git::Branch::revs_value(std::atoi(message.c_str() + offset),
                                 repository.read_commit((*i)->get_oid())));
#endif
    }
  }

  Git::BranchPtr find_branch(const boost::filesystem::path& pathname)
  {
    for (boost::filesystem::path dirname(pathname);
         ! dirname.empty();
         dirname = dirname.parent_path()) {
      branches_map::iterator i = branches.find(dirname);
      if (i != branches.end())
        return (*i).second;
    }
    status.warn(std::string("Could not find branch for path: ") +
                pathname.string());
    return default_branch;
  }

  Git::CommitPtr get_past_commit(const SvnDump::File::Node& node)
  {
    Git::BranchPtr other_branch(find_branch(node.get_copy_from_path()));

#if 0
    // jww (2011-01-29): NYI
    if (other_branch->rev_map.empty())
      load_revmaps_for_branch(other_branch);
#endif

    Git::Branch::revs_map::iterator i = other_branch->rev_map.end();
    for (int from_rev = node.get_copy_from_rev(); from_rev > 0; --from_rev) {
      i = other_branch->rev_map.find(from_rev);
      if (i != other_branch->rev_map.end())
        break;
    }

    if (i == other_branch->rev_map.end()) {
      std::ostringstream buf;
      buf << "Could not find commit for path "
          << node.get_copy_from_path()
          << ", rev " << node.get_copy_from_rev()
          << ", in branch: " << other_branch->name;
      throw std::logic_error(buf.str());
    }

#ifdef READ_FROM_DISK
    return repository.read_commit((*i).second);
#else
    return (*i).second;
#endif
  }

  Git::CommitPtr get_commit(const SvnDump::File&           dump,
                            const boost::filesystem::path& pathname,
                            Git::CommitPtr                 copy_from = NULL)
  {
    Git::BranchPtr branch(find_branch(pathname));
    Git::CommitPtr commit(branch->next_commit);
    if (commit)
      return commit;

    if (branch->commit) {
      commit = branch->next_commit = branch->commit->clone();
    } else {
      // If the first action is a dir/add/copyfrom, then this will get
      // set correctly, otherwise it's a parentless branch, which is
      // also OK.
      commit = branch->next_commit = (copy_from ? copy_from->clone(true) :
                                      repository.create_commit());

      status.info(std::string("Found new branch ") + branch->name);
    }
    commit->branch = branch;

    // Setup the author and commit comment
    std::string           author_id(dump.get_rev_author());
    authors_map::iterator author = authors.find(author_id);
    if (author != authors.end()) {
      // jww (2011-01-27): What about timezones?
      commit->set_author((*author).second.name, (*author).second.email,
                         dump.get_rev_date());
    } else {
      unrecognized_authors.insert(author_id);
      commit->set_author(author_id, "", dump.get_rev_date());
    }

    boost::optional<std::string> log(dump.get_rev_log());
    int len = 0;
    if (log) {
      len = log->length();
      while ((*log)[len - 1] == ' '  || (*log)[len - 1] == '\t' ||
             (*log)[len - 1] == '\n' || (*log)[len - 1] == '\r')
        --len;
    }

    std::ostringstream buf;
    if (log)
      buf << std::string(*log, 0, len) << '\n'
          << '\n';

    buf << "SVN-Revision: " << dump.get_rev_nr();
             
    commit->set_message(buf.str());

    commit_queue.push_back(commit);

    return commit;
  }

  void flush_commit_queue()
  {
    for (std::vector<Git::CommitPtr>::iterator i = commit_queue.begin();
         i != commit_queue.end();
         ++i) {
      if ((*i)->is_modified()) {
        assert(*i == (*i)->branch->next_commit);
        (*i)->branch->next_commit.reset();

        if ((*i)->has_tree()) {
          // Only now does the commit get associated with its branch
          (*i)->branch->commit = *i;
          (*i)->write();

#ifdef READ_FROM_DISK
          (*i)->branch->rev_map.insert
            (Git::Branch::revs_value(last_rev, (*i)->get_oid()));
#else
          (*i)->branch->rev_map.insert(Git::Branch::revs_value(last_rev, *i));
#endif
        } else {
          Git::BranchPtr branch((*i)->branch);

          deleted_branches.push_back(branch);
          
          for (branches_map::iterator b = branches.begin();
               b != branches.end();
               ++b)
            if (branch == (*b).second) {
              (*b).second = new Git::Branch(*branch);
              break;
            }
        }
      } else {
        (*i)->branch->next_commit.reset();
      }
    }
    commit_queue.clear();
  }

  void operator()(const SvnDump::File& dump, const SvnDump::File::Node& node)
  {
    const int rev = dump.get_rev_nr();
    if (rev != last_rev) {
      status.update(rev);
      flush_commit_queue();
      last_rev = rev;
    }

    boost::filesystem::path pathname(node.get_path());
    if (pathname.empty())
      return;

    SvnDump::File::Node::Kind   kind   = node.get_kind();
    SvnDump::File::Node::Action action = node.get_action();

    if (kind == SvnDump::File::Node::KIND_FILE &&
        (action == SvnDump::File::Node::ACTION_ADD ||
         action == SvnDump::File::Node::ACTION_CHANGE)) {
      status.info(std::string("file.") +
                  (action == SvnDump::File::Node::ACTION_ADD ?
                   "add" : "change") + ": " + pathname.string());

      Git::CommitPtr commit(get_commit(dump, pathname));
      if (node.has_copy_from()) {
        Git::CommitPtr past_commit(get_past_commit(node));
        Git::ObjectPtr obj(past_commit->lookup(node.get_copy_from_path()));
        if (! obj) {
          status.warn("Could not find object in the following commit tree:");
          past_commit->dump_tree(std::cerr);
        }
        assert(obj);
        assert(obj->is_blob());
        commit->update(pathname,
                       obj->copy_to_name(pathname.filename().string()));
      } else {
        commit->update(pathname,
                       repository.create_blob(pathname.filename().string(),
                                              node.has_text() ?
                                              node.get_text() : "",
                                              node.get_text_length()));
      }
    }
    else if (action == SvnDump::File::Node::ACTION_DELETE) {
      status.info(std::string(".delete: ") + pathname.string());

      get_commit(dump, pathname)->remove(pathname);
    }
    else if (node.has_copy_from() &&
             kind   == SvnDump::File::Node::KIND_DIR   &&
             action == SvnDump::File::Node::ACTION_ADD) {
      status.info(std::string("dir.add: ") +
                  node.get_copy_from_path().string() + " -> " +
                  pathname.string());

      Git::CommitPtr past_commit(get_past_commit(node));
      if (Git::ObjectPtr obj = past_commit->lookup(node.get_copy_from_path()))
        get_commit(dump, pathname, past_commit)
          ->update(pathname, obj->copy_to_name(pathname.filename().string()));
    }
  }

  void report(std::ostream&)
  {
    flush_commit_queue();

    for (branches_map::iterator i = branches.begin();
         i != branches.end();
         ++i)
      if ((*i).second->commit)
        (*i).second->update(repository);

    for (std::vector<Git::BranchPtr>::iterator i = deleted_branches.begin();
         i != deleted_branches.end();
         ++i)
      // jww (2011-01-29): Push deleted branches onto a tag
      std::cerr << "Deleted branch: " << (*i)->name << std::endl;

    bool newline_output = false;
    for (std::set<std::string>::const_iterator
           i = unrecognized_authors.begin();
         i != unrecognized_authors.end();
         ++i) {
      if (! newline_output) {
        status.newline();
        newline_output = true;
      }
      std::cerr << "Unrecognized author id: " << *i << std::endl;
    }

    status.finish();
  }
};

template <typename T>
void invoke_scanner(SvnDump::File& dump)
{
  StatusDisplay status(std::cerr);
  T finder(status);

  while (dump.read_next(/* ignore_text= */ true)) {
    status.set_last_rev(dump.get_last_rev_nr());
    finder(dump, dump.get_curr_node());
  }
  finder.report(std::cout);
}

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
        else if (std::strcmp(&argv[i][2], "verbose") == 0)
          opts.verbose = true;
        else if (std::strcmp(&argv[i][2], "debug") == 0)
          opts.debug = 1;
        else if (std::strcmp(&argv[i][2], "cutoff") == 0)
          opts.cutoff = std::atoi(argv[++i]);
        else if (std::strcmp(&argv[i][2], "authors") == 0)
          opts.authors_file = argv[++i];
        else if (std::strcmp(&argv[i][2], "branches") == 0)
          opts.branches_file = argv[++i];
        else if (std::strcmp(&argv[i][2], "modules") == 0)
          opts.modules_file = argv[++i];
      }
      else if (std::strcmp(&argv[i][1], "v") == 0)
        opts.verbose = true;
      else if (std::strcmp(&argv[i][1], "d") == 0)
        opts.debug = 1;
      else if (std::strcmp(&argv[i][1], "A") == 0)
        opts.authors_file = argv[++i];
      else if (std::strcmp(&argv[i][1], "B") == 0)
        opts.branches_file = argv[++i];
      else if (std::strcmp(&argv[i][1], "M") == 0)
        opts.modules_file = argv[++i];
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
    commit->set_author("John Wiegley", "johnw@boostpro.com", std::mktime(&then));
    commit->set_message("This is a sample commit.\n");

    Git::Branch branch("feature");
    std::cerr << "Updating feature branch..." << std::endl;
    branch.commit = commit;
    commit->write();
    branch.update(repo);

    std::cerr << "Cloning commit..." << std::endl;
    commit = commit->clone();   // makes a new commit based on the old one
    std::cerr << "Removing file..." << std::endl;
    commit->remove("foo/bar/baz.c");
    strptime("2005-04-10T22:13:13", "%Y-%m-%dT%H:%M:%S", &then);
    commit->set_author("John Wiegley", "johnw@boostpro.com", std::mktime(&then));
    commit->set_message("This removes the previous file.\n");

    Git::Branch master("master");
    std::cerr << "Updating master branch..." << std::endl;
    master.commit = commit;
    commit->write();
    master.update(repo);

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
    Git::Repository   repo(args.size() == 2 ?
                           boost::filesystem::current_path() : args[2]);
    StatusDisplay     status(std::cerr, opts, "Converting");
    ConvertRepository converter(repo, status, opts);

    if (! opts.authors_file.empty() &&
        boost::filesystem::is_regular_file(opts.authors_file))
      converter.load_authors(opts.authors_file);

    if (! opts.branches_file.empty() &&
        boost::filesystem::is_regular_file(opts.branches_file))
      converter.load_branches(opts.branches_file);

    if (! opts.modules_file.empty() &&
        boost::filesystem::is_regular_file(opts.modules_file))
      converter.load_branches(opts.modules_file);

    while (dump.read_next(/* ignore_text= */ false,
                          /* verify=      */ opts.verify)) {
      status.set_last_rev(dump.get_last_rev_nr());
      if (opts.cutoff && dump.get_rev_nr() > opts.cutoff)
        break;
      converter(dump, dump.get_curr_node());
    }
    converter.report(std::cout);
  }
  else if (cmd == "scan") {
    StatusDisplay status(std::cerr, opts);
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
