#include <iostream>
#include <sstream>
#include <map>
#include <memory>
#include <string>
#include <cstring>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/function.hpp>
#include <boost/optional.hpp>

#include <git2.h>

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
      boost::optional<std::string>             text;
#ifdef USE_CHECKSUMS
      boost::optional<std::string>             md5_checksum;
      boost::optional<std::string>             sha1_checksum;
#endif
      boost::optional<int>                     copy_from_rev;
      boost::optional<boost::filesystem::path> copy_from_path;

      friend class File;

    public:
      Node() : curr_txn(-1) { reset(); }

      void reset() {
        kind     = KIND_NONE;
        action   = ACTION_NONE;

        pathname.clear();

        text           = boost::none;
#ifdef USE_CHECKSUMS
        md5_checksum   = boost::none;
        sha1_checksum  = boost::none;
#endif
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
        return text;
      }
      std::size_t get_text_length() const {
        return text->length();
      }
#if USE_CHECKSUMS
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
#endif
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

    bool read_next();

  private:
    void read_tags();
  };

  bool File::read_next()
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

    static bool sought_chars[256] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
      1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

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
          if (sought_chars[linebuf[0]]) {
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
#ifdef USE_CHECKSUMS
              else if (property == "Text-content-md5")
                curr_node.md5_checksum = p + 2;
              else if (property == "Text-content-sha1")
                curr_node.sha1_checksum = p + 2;
#endif
              break;
            }
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
        handle->seekg(text_content_length, std::ios::cur);

      end_text:
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
  
  inline void
  foreach_node(const boost::filesystem::path& file,
               boost::function<void(const File&, const File::Node&)> actor)
  {
    File dump(file);

    while (dump.read_next())
        actor(dump, dump.get_curr_node());
  }
}

struct StatusDisplay
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
  PrintDumpFile printer;

  SvnDump::foreach_node(argv[1], printer);
  return 0;
}
