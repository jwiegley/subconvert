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
  bool quiet;
  int  debug;
  bool verify;
  bool skip_preflight;
  int  start;
  int  cutoff;

  boost::filesystem::path authors_file;
  boost::filesystem::path branches_file;
  boost::filesystem::path modules_file;

  Options() : verbose(false), quiet(false), debug(0), verify(false),
              skip_preflight(false), start(0), cutoff(0) {}
};

class StatusDisplay : public boost::noncopyable
{
  std::ostream& out;
  int           last_rev;
  mutable bool  need_newline;
  Options       opts;

public:
  std::string   verb;

  StatusDisplay(std::ostream&      _out,
                const Options&     _opts = Options(),
                const std::string& _verb = "Scanning")
    : out(_out), last_rev(-1), need_newline(false), opts(_opts),
      verb(_verb) {}

  void set_last_rev(int _last_rev = -1) {
    last_rev = _last_rev;
  }

  void debug(const std::string& message) {
    if (opts.debug) {
      newline();
      out << message << std::endl;
      need_newline = false;
    }
  }
  void info(const std::string& message) {
    if (opts.verbose || opts.debug) {
      newline();
      out << message << std::endl;
      need_newline = false;
    }
  }

  void newline() const {
    if (need_newline && ! opts.quiet) {
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
    if (opts.quiet) return;

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
    if (need_newline && ! opts.quiet) {
      out << ", done." << std::endl;
      need_newline = false;
    }
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
      struct tm * then = std::gmtime(&(*i).second.last_date);
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

  typedef std::map<boost::filesystem::path,
                   Git::BranchPtr>          branches_map;
  typedef branches_map::value_type          branches_value;

  const SvnDump::File&        dump;
  Git::Repository&            repository;
  StatusDisplay&              status;
  Options                     opts;
  int                         last_rev;
  int                         rev;
  std::vector<Git::TreePtr>   rev_trees;
  Git::TreePtr                rev_tree;
  authors_map                 authors;
  branches_map                branches;
  std::vector<Git::CommitPtr> commit_queue;
  Git::BranchPtr              master_branch;
  Git::BranchPtr              history_branch;
  bool                        create_history_commit;
  Git::BranchPtr              orphan_branch;

  ConvertRepository(const SvnDump::File& _dump,
                    Git::Repository&     _repository,
                    StatusDisplay&       _status,
                    const Options&       _opts = Options())
    : dump(_dump), repository(_repository), status(_status), opts(_opts),
      last_rev(-1), master_branch(new Git::Branch("master")),
      history_branch(new Git::Branch("flat-history", true)),
      create_history_commit(true),
      orphan_branch(new Git::Branch("orphan-history", true)) {}

  const char * unescape_string(const char * str)
  {
    static char buf[256];
    char * s = buf;
    for (const char * q = str; *q; ++q, ++s) {
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
    return buf;
  }

  int load_authors(const boost::filesystem::path& pathname)
  {
    int errors = 0;

    authors.clear();

    static const int MAX_LINE = 8192;
    char linebuf[MAX_LINE + 1];

    boost::filesystem::ifstream in(pathname);

    while (in.good() && ! in.eof()) {
      in.getline(linebuf, MAX_LINE);
      if (linebuf[0] == '#')
        continue;

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
          author.name = unescape_string(p);
          if (author.name == "Unknown")
            author.name = author_id;
          break;
        case 2:
          author.email = unescape_string(p);
          break;
        }
        field++;
      }

      std::pair<authors_map::iterator, bool> result =
        authors.insert(authors_value(author_id, author));
      if (! result.second) {
        status.warn(std::string("Author id repeated: ") + author_id);
        ++errors;
      }
    }
    return errors;
  }

  int load_branches(const boost::filesystem::path& pathname)
  {
    int errors = 0;

    branches.clear();

    static const int MAX_LINE = 8192;
    char linebuf[MAX_LINE + 1];

    boost::filesystem::ifstream in(pathname);

    while (in.good() && ! in.eof()) {
      in.getline(linebuf, MAX_LINE);
      if (linebuf[0] == '#')
        continue;

      Git::BranchPtr branch(new Git::Branch);
      int            field  = 0;

      for (const char * p = std::strtok(linebuf, "\t");
           p != NULL;
           p = std::strtok(NULL, "\t")) {
        switch (field) {
        case 0:
          if (*p == 't')
            branch->is_tag = true;
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

      if (branch->prefix.empty() || branch->name.empty())
        continue;

      std::pair<branches_map::iterator, bool> result =
        branches.insert(branches_value(branch->prefix, branch));
      if (! result.second) {
        status.warn(std::string("Branch prefix repeated: ") +
                    branch->prefix.string());
        ++errors;
      } else {
        for (boost::filesystem::path dirname(branch->prefix.parent_path());
             ! dirname.empty();
             dirname = dirname.parent_path()) {
          branches_map::iterator i = branches.find(dirname);
          if (i != branches.end()) {
            status.warn(std::string("Parent of branch prefix ") +
                        branch->prefix.string() + " exists: " +
                        (*i).second->prefix.string());
            ++errors;
          }
        }

        for (branches_map::iterator i = branches.begin();
             i != branches.end();
             ++i) {
          if (branch != (*i).second &&
              branch->name == (*i).second->name) {
            status.warn(std::string("Branch name repeated: ") +
                        branch->prefix.string());
            ++errors;
          }
        }
      }
    }
    return errors;
  }

  int load_modules(const boost::filesystem::path& /*pathname*/)
  {
    // jww (2011-01-29): NYI
    return 0;
  }

  int load_revmap()
  {
    int errors = 0;

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
#if 0
#ifdef READ_FROM_DISK
      branch->rev_map.insert
        (Git::Branch::revs_value(std::atoi(message.c_str() + offset),
                                 (*i)->get_oid()));
#else
      branch->rev_map.insert
        (Git::Branch::revs_value(std::atoi(message.c_str() + offset),
                                 repository.read_commit((*i)->get_oid())));
#endif
#endif
    }
    return errors;
  }

  Git::BranchPtr find_branch(const boost::filesystem::path& pathname)
  {
    if (branches.empty()) {
      return master_branch;
    } else {
      for (boost::filesystem::path dirname(pathname);
           ! dirname.empty();
           dirname = dirname.parent_path()) {
        branches_map::iterator i = branches.find(dirname);
        if (i != branches.end())
          return (*i).second;
      }
      return NULL;
    }
  }

  Git::TreePtr get_past_tree(const SvnDump::File::Node& node)
  {
    for (int i = node.get_copy_from_rev(); i >= 0; --i)
      if (Git::TreePtr tree = rev_trees[i])
        return tree;

    std::ostringstream buf;
    buf << "Could not find tree for " << node.get_copy_from_path()
        << ", r" << node.get_copy_from_rev();
    throw std::logic_error(buf.str());
  }

  void set_commit_info(Git::CommitPtr commit)
  {
    // Setup the author and commit comment
    std::string           author_id(dump.get_rev_author());
    authors_map::iterator author = authors.find(author_id);
    if (author != authors.end())
      commit->set_author((*author).second.name, (*author).second.email,
                         dump.get_rev_date());
    else
      commit->set_author(author_id, "", dump.get_rev_date());

    boost::optional<std::string> log(dump.get_rev_log());
    int beg = 0;
    int len = 0;
    if (log) {
      len = log->length();
      while (beg < len &&
             ((*log)[beg] == ' '  || (*log)[beg] == '\t' ||
              (*log)[beg] == '\n' || (*log)[beg] == '\r'))
        ++beg;
      while ((*log)[len - 1] == ' '  || (*log)[len - 1] == '\t' ||
             (*log)[len - 1] == '\n' || (*log)[len - 1] == '\r')
        --len;
    }

    std::ostringstream buf;
    if (log && len)
      buf << std::string(*log, beg, len) << '\n'
          << '\n';
    buf << "SVN-Revision: " << rev;

    commit->set_message(buf.str());
  }

  Git::CommitPtr get_commit(const boost::filesystem::path& pathname)
  {
    Git::BranchPtr branch(find_branch(pathname));
    Git::CommitPtr commit(branch->next_commit);
    if (commit) {
      std::vector<Git::CommitPtr>::iterator i =
        std::find(commit_queue.begin(), commit_queue.end(), commit);
      if (i == commit_queue.end()) {
        set_commit_info(commit);
        commit_queue.push_back(commit);
      }
      return commit;
    }

    if (branch->commit) {
      commit = branch->next_commit = branch->commit->clone();
    } else {
      // If the first action is a dir/add/copyfrom, then this will get
      // set correctly, otherwise it's a parentless branch, which is
      // also OK.
      commit = branch->next_commit = repository.create_commit();

      if (! dump.get_curr_node().has_copy_from())
        status.info(std::string("Branch starts out empty: ") + branch->name);

      if (opts.debug) {
        std::ostringstream buf;
        buf << "Branch start r" << rev << ": " << branch->name;
        if (dump.get_curr_node().has_copy_from()) {
          Git::BranchPtr other_branch
            (find_branch(dump.get_curr_node().get_copy_from_path()));
          buf << " (from r" << dump.get_curr_node().get_copy_from_rev()
              << ' ' << other_branch->name << ')';
        }
        status.debug(buf.str());
      }
    }
    commit->branch = branch;

    set_commit_info(commit);
    commit_queue.push_back(commit);

    if (create_history_commit && ! branches.empty()) {
      // Also add this commit
      Git::CommitPtr history_commit =
        (history_branch->commit ?
         history_branch->commit->clone() : repository.create_commit());

      set_commit_info(history_commit);

      history_commit->tree   = rev_tree;
      history_commit->branch = history_branch;
      history_commit->write();

      history_branch->last_rev = rev;
      history_branch->commit   = history_commit;

      create_history_commit = false;
    }

    return commit;
  }

  void flush_commit_queue()
  {
    int branches_modified = 0;
    for (std::vector<Git::CommitPtr>::iterator i = commit_queue.begin();
         i != commit_queue.end();
         ++i) {
      if (! (*i)->is_modified()) {
        if (opts.debug) {
          std::ostringstream buf;
          buf << "Commit for r" << rev << " had no Git-visible modifications";
          status.debug(buf.str());
        }
        continue;
      }

      assert(*i == (*i)->branch->next_commit);
      (*i)->branch->next_commit.reset();

      if ((*i)->has_tree()) {
        if (opts.debug) {
          std::ostringstream buf;
          buf << "Writing commit for r" << rev
              << " on branch " << (*i)->branch->name;
          status.debug(buf.str());
        }

        // Only now does the commit get associated with its branch
        (*i)->branch->last_rev = rev;
        (*i)->branch->commit   = *i;
        (*i)->write();

        ++branches_modified;
      } else {
        Git::BranchPtr branch((*i)->branch);

        if (opts.debug) {
          std::ostringstream buf;
          buf << "Branch end r" << rev << ": " << branch->name;
          status.debug(buf.str());
        }
        branch->last_rev = rev;

        if (branch->commit) {
          // If the branch is to be deleted, tag the last commit on
          // that branch with a special FOO__deleted_rXXXX name so the
          // history is preserved.
          std::ostringstream buf;
          buf << branch->name << "__deleted_r" << rev;
          std::string tag_name(buf.str());
          repository.create_tag((*i)->branch->commit, tag_name);
          status.debug(std::string("Wrote tag ") + tag_name);
        }
          
        for (branches_map::iterator b = branches.begin();
             b != branches.end();
             ++b) {
          if (branch == (*b).second) {
            (*b).second = new Git::Branch(*branch);
            break;
          }
        }
      }
    }
    commit_queue.clear();

    if (branches_modified > 1) {
      std::ostringstream buf;
      buf << "Revision " << rev << " modified "
          << branches_modified << " branches";
      status.info(buf.str());
    }
  }

  void next_revision()
  {
    status.update(rev);

    flush_commit_queue();

    for (int i = static_cast<int>(rev_trees.size()) - 1; i < rev - 1; ++i)
      rev_trees.push_back(NULL);

    if (rev_tree)
      rev_tree = rev_tree->copy();
    else
      rev_tree = repository.create_tree();
    rev_trees.push_back(rev_tree);

    create_history_commit = true;
  }

  bool add_file(const SvnDump::File::Node& node)
  {
    boost::filesystem::path pathname(node.get_path());

    status.debug(std::string("file.") +
                 (node.get_action() == SvnDump::File::Node::ACTION_ADD ?
                  "add" : "change") + ": " + pathname.string());

    Git::ObjectPtr obj;
    if (node.has_copy_from()) {
      Git::TreePtr past_tree(get_past_tree(node));
      obj = past_tree->lookup(node.get_copy_from_path());
      if (! obj) {
        std::ostringstream buf;
        buf << "Could not find " << node.get_copy_from_path()
            << " in tree r" << node.get_copy_from_rev() << ":";
        status.warn(buf.str());
        past_tree->dump_tree(std::cerr);
      }
      assert(obj);
      assert(obj->is_blob());
      obj = obj->copy_to_name(pathname.filename().string());
      rev_tree->update(pathname, obj);
    }
    else if (! (node.get_action() == SvnDump::File::Node::ACTION_CHANGE &&
                ! node.has_text())) {
      obj = repository.create_blob(pathname.filename().string(),
                                   node.has_text() ? node.get_text() : "",
                                   node.has_text() ? node.get_text_length() : 0);
      rev_tree->update(pathname, obj);
    }
    return true;
  }

  bool add_directory(const SvnDump::File::Node& node)
  {
    boost::filesystem::path pathname(node.get_path());

    status.debug(std::string("dir.add: ") +
                 node.get_copy_from_path().string() + " -> " +
                 pathname.string());

    if (Git::ObjectPtr obj =
        get_past_tree(node)->lookup(node.get_copy_from_path())) {
      obj = obj->copy_to_name(pathname.filename().string());
      rev_tree->update(pathname, obj);
      return true;
    }
    return false;
  }

  bool delete_item(const SvnDump::File::Node& node)
  {
    boost::filesystem::path pathname(node.get_path());

    status.debug(std::string(".delete: ") + pathname.string());

    rev_tree->remove(pathname);
    return true;
  }

  void operator()(const SvnDump::File& dump, const SvnDump::File::Node& node)
  {
    rev = dump.get_rev_nr();
    if (rev != last_rev) {
      next_revision();
      last_rev = rev;
    }

    boost::filesystem::path pathname(node.get_path());
    if (! pathname.empty()) {
      SvnDump::File::Node::Kind   kind   = node.get_kind();
      SvnDump::File::Node::Action action = node.get_action();

      bool set_tree = false;
      if (kind == SvnDump::File::Node::KIND_FILE &&
          (action == SvnDump::File::Node::ACTION_ADD ||
           action == SvnDump::File::Node::ACTION_CHANGE)) {
        set_tree = add_file(node);
      }
      else if (action == SvnDump::File::Node::ACTION_DELETE) {
        set_tree = delete_item(node);
      }
      else if (node.has_copy_from() &&
               kind   == SvnDump::File::Node::KIND_DIR   &&
               action == SvnDump::File::Node::ACTION_ADD) {
        set_tree = add_directory(node);
      }

      if (set_tree)
        get_commit(pathname)->set_tree(rev_tree);
    }
  }

  void report(std::ostream&)
  {
    flush_commit_queue();

    for (branches_map::iterator i = branches.begin();
         i != branches.end();
         ++i) {
      if ((*i).second->commit) {
        assert((*i).second->last_rev != -1);
        if ((*i).second->is_tag) {
          repository.create_tag((*i).second->commit, (*i).second->name);
          status.info(std::string("Wrote tag ") + (*i).second->name);
        } else {
          (*i).second->update(repository);
          status.info(std::string("Wrote branch ") + (*i).second->name);
        }
      } else {
        status.info(std::string("Branch ") + (*i).second->name + " is empty");
      }
    }

    if (master_branch->commit) {
      repository.create_tag(master_branch->commit, master_branch->name);
      status.info(std::string("Wrote branch ") + master_branch->name);
    }
    if (history_branch->commit) {
      repository.create_tag(history_branch->commit, history_branch->name);
      status.info(std::string("Wrote branch ") + history_branch->name);
    }
    if (orphan_branch->commit) {
      repository.create_tag(orphan_branch->commit, orphan_branch->name);
      status.info(std::string("Wrote tag ") + orphan_branch->name);
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
        else if (std::strcmp(&argv[i][2], "quiet") == 0)
          opts.quiet = true;
        else if (std::strcmp(&argv[i][2], "debug") == 0)
          opts.debug = 1;
        else if (std::strcmp(&argv[i][2], "skip") == 0)
          opts.skip_preflight = 1;
        else if (std::strcmp(&argv[i][2], "start") == 0)
          opts.start = std::atoi(argv[++i]);
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
      else if (std::strcmp(&argv[i][1], "q") == 0)
        opts.quiet = true;
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

  try {
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
      StatusDisplay     status(std::cerr, opts);
      ConvertRepository converter(dump, repo, status, opts);
      int               errors = 0;

      // Load any information provided by the user to assist with the
      // migration.

      if (! opts.authors_file.empty() &&
          boost::filesystem::is_regular_file(opts.authors_file))
        errors += converter.load_authors(opts.authors_file);

      if (! opts.branches_file.empty() &&
          boost::filesystem::is_regular_file(opts.branches_file))
        errors += converter.load_branches(opts.branches_file);

      if (! opts.modules_file.empty() &&
          boost::filesystem::is_regular_file(opts.modules_file))
        errors += converter.load_modules(opts.modules_file);

      errors += converter.load_revmap();

      // Validate this information as much as possible before possibly
      // wasting the user's time with useless work.

      if (! opts.skip_preflight) {
        status.verb = "Scanning";
        while (dump.read_next(/* ignore_text= */ false,
                              /* verify=      */ true)) {
          status.set_last_rev(dump.get_last_rev_nr());
          status.update(dump.get_rev_nr());

          const SvnDump::File::Node& node(dump.get_curr_node());

          if (! converter.authors.empty()) {
            std::string author_id(dump.get_rev_author());
            ConvertRepository::authors_map::iterator author =
              converter.authors.find(author_id);
            if (author == converter.authors.end()) {
              std::ostringstream buf;
              buf << "Unrecognized author id: " << author_id;
              status.warn(buf.str());
              ++errors;
            }
          }

          if (! converter.branches.empty()) {
            // Ignore pathname which only add or modify directories, but
            // do care about all entries which add or modify files, and
            // those which copy directories.

            if (node.get_action() == SvnDump::File::Node::ACTION_DELETE ||
                node.get_kind()   == SvnDump::File::Node::KIND_FILE ||
                node.has_copy_from()) {
              if (! converter.find_branch(node.get_path())) {
                std::ostringstream buf;
                buf << "Could not find branch for " << node.get_path()
                    << " in r" << dump.get_rev_nr();
                status.warn(buf.str());
                ++errors;
              }
              if (node.has_copy_from() &&
                  ! converter.find_branch(node.get_copy_from_path())) {
                std::ostringstream buf;
                buf << "Could not find branch for " << node.get_copy_from_path()
                    << " in r" << dump.get_rev_nr();
                status.warn(buf.str());
                ++errors;
              }
            }
          }
        }
        status.newline();

        if (errors > 0) {
          status.warn("Please correct the errors listed above and run again.");
          return 1;
        }
        status.warn("Note: --skip can be used to skip this pre-scan.");

        dump.rewind();
      }

      // If everything passed the preflight, perform the conversion.

      status.verb = "Converting";
      while (dump.read_next(/* ignore_text= */ false)) {
        status.set_last_rev(dump.get_last_rev_nr());
        int rev = dump.get_rev_nr();
        if (opts.cutoff && rev > opts.cutoff)
          break;
        if (! opts.start || rev >= opts.start)
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
  }
  catch (const std::exception& err) {
    std::cerr << "Error: " << err.what() << std::endl;
    return 1;
  }

  return 0;
}
