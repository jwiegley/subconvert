#include "config.h"

#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/optional.hpp>

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
  class Object
  {
    int sha1;
  };

  class Blob : public Object
  {
  };

  class Tree : public Object
  {
  };

  class Commit : public Object
  {
  };

  class Branch
  {
  };
}

namespace SvnDump
{
  class File : public boost::noncopyable
  {
    int curr_rev;
    int last_rev;

    std::string                  rev_author;
    std::string                  rev_date;
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
    std::string get_rev_date() const {
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
    static const std::size_t MAX_LINE = 8192;

    char linebuf[MAX_LINE + 1];

    enum state_t {
      STATE_ERROR,
      STATE_TAGS,
      STATE_PROPS,
      STATE_BODY,
      STATE_NEXT
    } state = STATE_NEXT;

    int  prop_content_length;
    int  text_content_length;
    int  content_length;
    bool saw_node_path;

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
            else if (property == "svn:date")
              rev_date   = p;
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
            git_oid oid;
            char checksum[41];
#ifdef HAVE_OPENSSL_MD5
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
#endif // HAVE_OPENSSL_MD5
#ifdef HAVE_OPENSSL_SHA1
            if (curr_node.has_sha1()) {
              SHA1(reinterpret_cast<const unsigned char *>(curr_node.get_text()),
                   curr_node.get_text_length(), oid.id);
              git_oid_fmt(checksum, &oid);
              checksum[40] = '\0';
              assert(curr_node.get_text_sha1() == checksum);
            }
#endif // HAVE_OPENSSL_SHA1
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

  FindAuthors(StatusDisplay& _status) :
    status(_status), last_rev(-1) {}

  void operator()(const SvnDump::File&       dump,
                  const SvnDump::File::Node& node)
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
         i++)
      out << (*i).first << "\t\t\t" << (*i).second << '\n';
  }
};

struct FindBranches
{
};

struct PrintDumpFile
{
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
};

int main(int argc, char *argv[])
{
  std::ios::sync_with_stdio(false);

  // Examine any option settings made by the user.  -f is the only
  // required one.

  std::vector<std::string> args;

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == '-') {
        if (std::strcmp(&argv[i][2], "verify")) {
          opts.verify = true;
        }          
      }
      else if (std::strcmp(&argv[i][1], "v")) {
        opts.verbose = true;
      }
      else if (std::strcmp(&argv[i][1], "d")) {
        opts.debug = 1;
      }
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

  std::string   cmd(args[0]);
  SvnDump::File dump(args[1]);

  if (cmd == "print") {
    PrintDumpFile printer;

    while (dump.read_next(/* ignore_text= */ true))
      printer(dump, dump.get_curr_node());
  }
  else if (cmd == "authors") {
    StatusDisplay status(std::cerr);
    FindAuthors author_finder(status);

    while (dump.read_next(/* ignore_text= */ true))
      author_finder(dump, dump.get_curr_node());
    author_finder.report(std::cout);
  }
  else if (cmd == "branches") {
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
  else if (cmd == "git-test") {
  }

  return 0;
}
