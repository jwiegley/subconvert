#include <git2.h>

using boost::optional;
using boost::posix_time::ptime;

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
  class File : public noncopyable
  {
    int        curr_rev;
    ifstream * handle;

    std::string           rev_author;
    ptime                 rev_date;
    optional<std::string> rev_log;

    class Node : public noncopyable
    {
      int  curr_txn;
      path pathname;

      enum Kind {
        KIND_NONE,
        KIND_FILE,
        KIND_DIR
      } kind;

      enum Action {
        ACTION_NONE,
        ACTION_ADD,
        ACTION_DELETE,
        ACTION_CHANGE,
        ACTION_REPLACE
      } action;

      optional<string> text;
      optional<int>    md5_checksum;

      optional<int>    copy_from_rev;
      optional<path>   copy_from_path;

    public:
      Node() : curr_txn(-1), kind(KIND_NONE), action(ACTION_NONE) {}

      int get_txn_nr() const {
        return curr_txn;
      }
      Action get_action() const {
        //assert(action != ACTION_NONE);
        return action;
      }
      Kind get_kind() const {
        return kind;
      }
      path get_path() const {
        return pathname;
      }
      bool has_copy_from() const {
        return copy_from_rev;
      }
      path get_copy_from_path() const {
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
      bool has_md5() const {
        return md5_checksum;
      }
      int get_text_md5() const {
        return *md5_checksum;
      }
    };

    Node curr_node;

  public:
    File() : curr_rev(-1), handle(NULL) {}
    File(const path& file) : curr_rev(-1), handle(NULL) {
      open(file);
    }
    ~File() {
      if (handle)
        close();
    }

    void open(const path& file) {
      if (handle)
        close();
      handle = new ifstream(file);
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
    ptime get_rev_date() const {
      return rev_date;
    }
    optional<std::string> get_rev_log() const {
      return rev_log;
    }

    bool read_next_rev();
    bool read_next_node();
  };

  bool File::read_next_rev()
  {
    return false;
  }
  
  bool File::read_next_node()
  {
    return false;
  }

  inline void
  foreach_dump_file_node(const path& file,
                         std::function<void(const File&,
                                            const File::Node&)> actor)
  {
    File dump(file);

    while (dump.read_next_rev())
      while (dump.read_next_node())
        actor(dump, dump.get_curr_node());
  }
}

class StatusDisplay
{
};

class PrintDumpFile
{
  void operator()(const File& dump, const File::Node& node) {
  }
};

int main(int argc, char *argv[])
{
}
