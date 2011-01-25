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

#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>

#include "config.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/optional.hpp>

#define BOOST_FILESYSTEM_VERSION 3
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#ifdef HAVE_OPENSSL_MD5_H
#include <openssl/md5.h>
#endif
#ifdef HAVE_OPENSSL_SHA_H
#include <openssl/sha.h>
#endif

#include <git2.h>

#define ASSERTS (1)
#ifndef ASSERTS
#undef assert
#define assert(x)
#endif

namespace Git
{
  class Tree
  {
    git_tree * tree;

  public:
    Tree(git_tree * _tree) : tree(_tree) {}
  };

  class Commit
  {
    git_commit * commit;

  public:
    Commit(git_commit * _commit) : commit(_commit) {}
  };

  class Branch
  {
  };

  class Repository
  {
    git_repository * repo;

  public:
    Repository(const boost::filesystem::path& pathname) {
      if (git_repository_open(&repo, pathname.string().c_str()) != 0)
        throw std::logic_error(std::string("Could not open Git repository: ") +
                               pathname.string());
    }
    ~Repository() {
      git_repository_free(repo);
    }

    const git_oid * create_blob(const char * data, std::size_t len) {
      git_blob *blob;
      if (git_blob_new(&blob, repo) != 0)
        throw std::logic_error("Could not create Git blob");

      if (git_blob_set_rawcontent(blob, data, len) != 0)
        throw std::logic_error("Could not set Git blob contents");

      if (git_object_write(reinterpret_cast<git_object *>(blob)) != 0)
        throw std::logic_error("Could not write Git blob");

      return git_object_id(reinterpret_cast<git_object *>(blob));
    }

    git_tree * create_tree() {
      git_tree * tree;
      if (git_tree_new(&tree, repo) != 0)
        throw std::logic_error("Could not create Git tree");
      return tree;
    }

    git_commit * create_commit() {
      git_commit * commit;
      if (git_commit_new(&commit, repo) != 0)
        throw std::logic_error("Could not create Git commit");
      return commit;
    }
  };
}

namespace SvnDump
{
  class File : public boost::noncopyable
  {
    int curr_rev;
    int last_rev;

    std::string                  rev_author;
    std::time_t                  rev_date;
    boost::optional<std::string> rev_log;

    boost::filesystem::ifstream * handle;

  public:
    class Node : public boost::noncopyable
    {
    public:
      enum Kind {
        KIND_NONE,
        KIND_FILE,
        KIND_DIR
      };

      enum Action {
        ACTION_NONE,
        ACTION_ADD,
        ACTION_DELETE,
        ACTION_CHANGE,
        ACTION_REPLACE
      };

    private:
      int                                      curr_txn;
      boost::filesystem::path                  pathname;
      Kind                                     kind;
      Action                                   action;
      char *                                   text;
      int                                      text_len;
      boost::optional<std::string>             md5_checksum;
      boost::optional<std::string>             sha1_checksum;
      boost::optional<int>                     copy_from_rev;
      boost::optional<boost::filesystem::path> copy_from_path;

      friend class File;

    public:
      Node() : curr_txn(-1), text(NULL) { reset(); }
      ~Node() { reset(); }

      void reset() {
        kind     = KIND_NONE;
        action   = ACTION_NONE;

        pathname.clear();

        if (text)
          delete[] text;
        text = NULL;

        md5_checksum   = boost::none;
        sha1_checksum  = boost::none;
        copy_from_rev  = boost::none;
        copy_from_path = boost::none;
      }

      int get_txn_nr() const {
        return curr_txn;
      }
      Action get_action() const {
        assert(action != ACTION_NONE);
        return action;
      }
      Kind get_kind() const {
        return kind;
      }
      boost::filesystem::path get_path() const {
        return pathname;
      }
      bool has_copy_from() const {
        return copy_from_rev;
      }
      boost::filesystem::path get_copy_from_path() const {
        return *copy_from_path;
      }
      int get_copy_from_rev() const {
        return *copy_from_rev;
      }
      bool has_text() const {
        return text != NULL;
      }
      const char * get_text() const {
        return text;
      }
      std::size_t get_text_length() const {
        return text_len;
      }
      bool has_md5() const {
        return md5_checksum;
      }
      std::string get_text_md5() const {
        return *md5_checksum;
      }
      bool has_sha1() const {
        return sha1_checksum;
      }
      std::string get_text_sha1() const {
        return *sha1_checksum;
      }
    };

  private:
    Node curr_node;

  public:
    File() : curr_rev(-1), handle(NULL) {}
    File(const boost::filesystem::path& file) : curr_rev(-1), handle(NULL) {
      open(file);
    }
    ~File() {
      if (handle)
        close();
    }

    void open(const boost::filesystem::path& file) {
      if (handle)
        close();

      handle = new boost::filesystem::ifstream(file);

      // Buffer up to 1 megabyte when reading the dump file; this is a
      // nearly free 3% speed gain
      static char read_buffer[1024 * 1024];
      handle->rdbuf()->pubsetbuf(read_buffer, 1024 * 1024);
    }
    void close() {
      delete handle;
      handle = NULL;
    }

    int get_rev_nr() const {
      return curr_rev;
    }
    int get_last_rev_nr() const {
      return last_rev;
    }
    const Node& get_curr_node() const {
      return curr_node;
    }
    std::string get_rev_author() const {
      return rev_author;
    }
    std::time_t get_rev_date() const {
      return rev_date;
    }
    boost::optional<std::string> get_rev_log() const {
      return rev_log;
    }

    bool read_next(const bool ignore_text = false,
                   const bool verify      = false);

  private:
    void read_tags();
  };

  bool File::read_next(const bool ignore_text, const bool verify)
  {
    static const int MAX_LINE = 8192;

    char linebuf[MAX_LINE + 1];

    enum state_t {
      STATE_ERROR,
      STATE_TAGS,
      STATE_PROPS,
      STATE_BODY,
      STATE_NEXT
    } state = STATE_NEXT;

    int  prop_content_length = -1;
    int  text_content_length = -1;
    int  content_length      = -1;
    bool saw_node_path       = false;

    while (handle->good() && ! handle->eof()) {
      switch (state) {
      case STATE_NEXT:
        prop_content_length = -1;
        text_content_length = -1;
        content_length      = -1;
        saw_node_path       = false;

        curr_node.reset();

        if (handle->peek() == '\n')
          handle->read(linebuf, 1);
        state = STATE_TAGS;

        // fall through...

      case STATE_TAGS:
        handle->getline(linebuf, MAX_LINE);

        if (const char * p = std::strchr(linebuf, ':')) {
          std::string property(linebuf, 0, p - linebuf);
          switch (property[0]) {
          case 'C':
            if (property == "Content-length")
              content_length = std::atoi(p + 2);
            break;

          case 'N':
            if (property == "Node-path") {
              curr_node.curr_txn += 1;
              curr_node.pathname = p + 2;
              saw_node_path = true;
            }
            else if (property == "Node-kind") {
              if (*(p + 2) == 'f')
                curr_node.kind = Node::KIND_FILE;
              else if (*(p + 2) == 'd')
                curr_node.kind = Node::KIND_DIR;
            }
            else if (property == "Node-action") {
              if (*(p + 2) == 'a')
                curr_node.action = Node::ACTION_ADD;
              else if (*(p + 2) == 'd')
                curr_node.action = Node::ACTION_DELETE;
              else if (*(p + 2) == 'c')
                curr_node.action = Node::ACTION_CHANGE;
              else if (*(p + 2) == 'r')
                curr_node.action = Node::ACTION_REPLACE;
            }
            else if (property == "Node-copyfrom-rev") {
              curr_node.copy_from_rev = std::atoi(p + 2);
            }
            else if (property == "Node-copyfrom-path") {
              curr_node.copy_from_path = p + 2;
            }
            break;

          case 'P':
            if (property == "Prop-content-length")
              prop_content_length = std::atoi(p + 2);
            break;

          case 'R':
            if (property == "Revision-number") {
              curr_rev  = std::atoi(p + 2);
              rev_log   = boost::none;
              curr_node.curr_txn = -1;
            }
            break;

          case 'T':
            if (property == "Text-content-length")
              text_content_length = std::atoi(p + 2);
            else if (verify && property == "Text-content-md5")
              curr_node.md5_checksum = p + 2;
            else if (verify && property == "Text-content-sha1")
              curr_node.sha1_checksum = p + 2;
            break;
          }
        }
        else if (linebuf[0] == '\0') {
          if (prop_content_length > 0)
            state = STATE_PROPS;
          else if (text_content_length > 0)
            state = STATE_BODY;
          else if (saw_node_path)
            return true;
          else
            state = STATE_NEXT;
        }
        break;
          
      case STATE_PROPS: {
        assert(prop_content_length > 0);

        char *      buf;
        bool        allocated;
        char *      p;
        char *      q;
        int         len;
        bool        is_key;
        std::string property;

        if (curr_node.curr_txn >= 0) {
          // Ignore properties that don't describe the revision itself;
          // we just don't need to know for the purposes of this
          // utility.
          handle->seekg(prop_content_length, std::ios::cur);
          goto end_props;
        }

        if (prop_content_length < MAX_LINE) {
          handle->read(linebuf, prop_content_length);
          buf = linebuf;
          allocated = false;
        } else {
          buf = new char[prop_content_length + 1];
          allocated = true;
          handle->read(buf, prop_content_length);
        }

        p = buf;
        while (p - buf < prop_content_length) {
          is_key = *p == 'K';
          if (is_key || *p == 'V') {
            q = std::strchr(p, '\n');
            assert(q != NULL);
            *q = '\0';
            len = std::atoi(p + 2);
            p = q + 1;
            q = p + len;
            *q = '\0';

            if (is_key)
              property   = p;
            else if (property == "svn:date") {
              struct tm then;
              strptime(p, "%Y-%m-%dT%H:%M:%S", &then);
              rev_date   = std::mktime(&then);
            }
            else if (property == "svn:author")
              rev_author = p;
            else if (property == "svn:log")
              rev_log    = p;
            else if (property == "svn:sync-last-merged-rev")
              last_rev   = std::atoi(p);

            p = q + 1;
          } else {
            assert(p - buf == prop_content_length - 10);
            assert(std::strncmp(p, "PROPS-END\n", 10) == 0);
            break;
          }
        }

        if (allocated)
          delete[] buf;

      end_props:
        if (text_content_length > 0)
          state = STATE_BODY;
        else if (curr_rev == -1 || curr_node.curr_txn == -1)
          state = STATE_NEXT;
        else
          return true;
        break;
      }

      case STATE_BODY:
        if (ignore_text) {
          handle->seekg(text_content_length, std::ios::cur);
        } else {
          assert(! curr_node.has_text());
          assert(text_content_length > 0);

          curr_node.text     = new char[text_content_length];
          curr_node.text_len = text_content_length;

          handle->read(curr_node.text, text_content_length);

#ifdef HAVE_LIBCRYPTO
          if (verify) {
#if defined(HAVE_OPENSSL_MD5_H) || defined(HAVE_OPENSSL_SHA_H)
            git_oid oid;
#endif
            char checksum[41];
#ifdef HAVE_OPENSSL_MD5_H
            if (curr_node.has_md5()) {
              MD5(reinterpret_cast<const unsigned char *>(curr_node.get_text()),
                  curr_node.get_text_length(), oid.id);
              std::sprintf(checksum,
                           "%02x%02x%02x%02x%02x%02x%02x%02x"
                           "%02x%02x%02x%02x%02x%02x%02x%02x",
                           oid.id[0],  oid.id[1],  oid.id[2],  oid.id[3],
                           oid.id[4],  oid.id[5],  oid.id[6],  oid.id[7],
                           oid.id[8],  oid.id[9],  oid.id[10], oid.id[11],
                           oid.id[12], oid.id[13], oid.id[14], oid.id[15]);
              assert(curr_node.get_text_md5() == checksum);
            }
#endif // HAVE_OPENSSL_MD5_H
#ifdef HAVE_OPENSSL_SHA_H
            if (curr_node.has_sha1()) {
              SHA1(reinterpret_cast<const unsigned char *>(curr_node.get_text()),
                   curr_node.get_text_length(), oid.id);
              git_oid_fmt(checksum, &oid);
              checksum[40] = '\0';
              assert(curr_node.get_text_sha1() == checksum);
            }
#endif // HAVE_OPENSSL_SHA_H
          }
#endif // HAVE_LIBCRYPTO
        }

        if (curr_rev == -1 || curr_node.curr_txn == -1)
          state = STATE_NEXT;
        else
          return true;

      case STATE_ERROR:
        assert(false);
        return false;
      }
    }
    return false;
  }
}

struct GlobalOptions
{
  bool verify;
  bool verbose;
  int  debug;

  GlobalOptions() : verify(false), verbose(false), debug(0) {}
} opts;

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

int main(int argc, char *argv[])
{
  std::ios::sync_with_stdio(false);

  // Examine any option settings made by the user.  -f is the only
  // required one.

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
#if 0
    Repository repo;

    GitCommit commit;

    commit.update('foo/bar/baz.c', repo.create_blob("#include <stdio.h>\n", 19));
    commit.author_name  = 'John Wiegley';
    commit.author_email = 'johnw@boostpro.com';
    commit.author_date  = '2005-04-07T22:13:13';
    commit.comment      = "This is a sample commit.\n";

    GitBranch branch('feature', commit);

    commit = commit.fork();      // makes a new commit based on the old one
    commit.remove('foo/bar/baz.c');
    commit.author_name  = 'John Wiegley';
    commit.author_email = 'johnw@boostpro.com';
    commit.author_date  = '2005-04-10T22:13:13';
    commit.comment      = "This removes the previous file.\n";

    GitBranch master('master', commit);

    git('symbolic-ref', 'HEAD', 'refs/heads/%s' % branch.name);
#endif
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
