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
#include <boost/checked_delete.hpp>
#include <boost/intrusive_ptr.hpp>
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
  class Repository;

  class Object
  {
    friend class Repository;

  protected:
    git_object * git_obj;
    Repository * repository;

    mutable int refc;

    void acquire() const {
      assert(refc >= 0);
      refc++;
    }
    void release() const {
      assert(refc > 0);
      if (--refc == 0)
        boost::checked_delete(this);
    }

  public:
    std::string      name;
    int              attributes;
    git_tree_entry * tree_entry;

    Object(git_object * _git_obj, const std::string& _name, int _attributes)
      : git_obj(_git_obj), refc(0), name(_name), attributes(_attributes),
        tree_entry(NULL) {}
    ~Object() {
      assert(refc == 0);
    }

    operator git_object *() {
      return git_obj;
    }
    operator const git_oid *() const {
      return git_object_id(git_obj);
    }

    std::string sha1() const {
      char checksum[41];
      git_oid_fmt(checksum, *this);
      checksum[40] = '\0';
      return checksum;
    }

    virtual bool is_blob() const {
      return true;
    }
    virtual bool is_tree() const {
      return false;
    }

    virtual void write() = 0;

    friend inline void intrusive_ptr_add_ref(Object * obj) {
      obj->acquire();
    }
    friend inline void intrusive_ptr_release(Object * obj) {
      obj->release();
    }
  };

  typedef boost::intrusive_ptr<Object> ObjectPtr;

  class Blob : public Object
  {
  public:
    Blob(git_blob * blob, const std::string& name, int attributes = 0100644)
      : Object(reinterpret_cast<git_object *>(blob), name, attributes) {}

    operator git_blob *() const {
      return reinterpret_cast<git_blob *>(git_obj);
    }

    virtual void write() {}
  };

  typedef boost::intrusive_ptr<Blob> BlobPtr;

  class Tree : public Object
  {
  protected:
    typedef std::map<std::string, ObjectPtr>  entries_map;
    typedef std::pair<std::string, ObjectPtr> entries_pair;

    entries_map entries;
    bool        written;
    bool        modified;
    bool        sort_needed;

    ObjectPtr do_lookup(boost::filesystem::path::iterator segment,
                        boost::filesystem::path::iterator end) {
      std::string name = (*segment).string();

      entries_map::iterator i = entries.find(name);
      if (i == entries.end())
        return NULL;

      ObjectPtr result;
      if (++segment == end) {
        result = (*i).second;
        assert(result->name == name);
      } else {
        result = dynamic_cast<Tree *>((*i).second.get())->do_lookup(segment, end);
      }
      return result;
    }

    void do_update(boost::filesystem::path::iterator segment,
                   boost::filesystem::path::iterator end, ObjectPtr obj);

    void do_remove(boost::filesystem::path::iterator segment,
                   boost::filesystem::path::iterator end);

  public:
    Tree(git_tree * tree, const std::string& name, int attributes = 0040000)
      : Object(reinterpret_cast<git_object *>(tree), name, attributes),
        written(false), modified(false), sort_needed(false) {}

    operator git_tree *() const {
      return reinterpret_cast<git_tree *>(git_obj);
    }

    virtual bool is_blob() const {
      return false;
    }
    virtual bool is_tree() const {
      return true;
    }

    bool empty() const {
      assert(! entries.empty() || git_tree_entrycount(*this) == 0);
      return entries.empty();
    }

    ObjectPtr lookup(const boost::filesystem::path& pathname) {
      return do_lookup(pathname.begin(), pathname.end());
    }

    void update(const boost::filesystem::path& pathname, ObjectPtr obj) {
      do_update(pathname.begin(), pathname.end(), obj);
    }

    void remove(const boost::filesystem::path& pathname) {
      do_remove(pathname.begin(), pathname.end());
    }

    void clear() {
      while (git_tree_entrycount(*this) > 0)
        if (git_tree_remove_entry_byindex(*this, 0) != 0)
          throw std::logic_error("Could not remove entry from tree");
    }

    virtual void write();
  };

  typedef boost::intrusive_ptr<Tree> TreePtr;

  class Commit;
  typedef boost::intrusive_ptr<Commit> CommitPtr;

  class Commit : public Object
  {
    TreePtr                 tree;
    boost::filesystem::path prefix;

  public:
    Commit(git_commit * commit,
           const std::string& name = "", int attributes = 0040000)
      : Object(reinterpret_cast<git_object *>(commit), name, attributes) {}

    operator git_commit *() const {
      return reinterpret_cast<git_commit *>(git_obj);
    }

    virtual bool is_blob() const {
      return false;
    }
    virtual bool is_tree() const {
      return false;
    }

    CommitPtr clone();

    void add_parent(CommitPtr parent) {
      if (git_commit_add_parent(*this, *parent))
        throw std::logic_error("Could not add parent to commit");
    }

    void set_message(const std::string& message) {
      git_commit_set_message(*this, message.c_str());
    }

    void set_author(const std::string& name, const std::string& email,
                    time_t time) {
      git_signature * signature =
        git_signature_new(name.c_str(), email.c_str(), time, 0);
      if (! signature)
        throw std::logic_error("Could not create signature object");

      git_commit_set_author(*this, signature);
      git_commit_set_committer(*this, signature);
  }

    ObjectPtr lookup(const boost::filesystem::path& pathname) {
      return tree ? tree->lookup(pathname) : tree;
    }

    void update(const boost::filesystem::path& pathname, ObjectPtr obj);

    void remove(const boost::filesystem::path& pathname) {
      if (tree)
        tree->remove(pathname);
    }

    virtual void write();
  };

  class Branch
  {
    std::string             name;
    boost::filesystem::path prefix;
    //prefix_re = None
    bool                    is_tag;
    int                     final_rev;

  public:
    Branch(const std::string& _name = "master")
      : name(_name), is_tag(false), final_rev(0) {}

    void update(Repository& repository, CommitPtr commit);
  };

  class Repository
  {
    git_repository * repo;

  public:
    Repository(const boost::filesystem::path& pathname) {
      if (git_repository_open(&repo, pathname.string().c_str()) != 0)
        if (git_repository_open(&repo,
                                (pathname / ".git").string().c_str()) != 0)
          throw std::logic_error(std::string("Could not open repository: ") +
                                 pathname.string() + " or " +
                                 (pathname / ".git").string());
    }
    ~Repository() {
      git_repository_free(repo);
    }

    BlobPtr create_blob(const std::string& name,
                        const char * data, std::size_t len,
                        int attributes = 0100644) {
      git_blob * git_blob;

      if (git_blob_new(&git_blob, repo) != 0)
        throw std::logic_error("Could not create Git blob");

      if (git_blob_set_rawcontent(git_blob, data, len) != 0)
        throw std::logic_error("Could not set Git blob contents");

      if (git_object_write(reinterpret_cast<git_object *>(git_blob)) != 0)
        throw std::logic_error("Could not write Git blob");

      Blob * blob = new Blob(git_blob, name, attributes);
      blob->repository = this;
      return blob;
    }

    TreePtr create_tree(const std::string& name = "", int attributes = 040000) {
      git_tree * git_tree;
      if (git_tree_new(&git_tree, repo) != 0)
        throw std::logic_error("Could not create Git tree");

      Tree * tree = new Tree(git_tree, name, attributes);
      tree->repository = this;
      return tree;
    }

    CommitPtr create_commit() {
      git_commit * git_commit;
      if (git_commit_new(&git_commit, repo) != 0)
        throw std::logic_error("Could not create Git commit");

      Commit * commit = new Commit(git_commit);
      commit->repository = this;
      return commit;
    }

    void create_file(const boost::filesystem::path& pathname,
                     const std::string& content = "") {
      boost::filesystem::path dir(boost::filesystem::current_path());
      boost::filesystem::path file(boost::filesystem::path(".git") / pathname);
      boost::filesystem::path parent(file.parent_path());

      // Make sure the directory exists for the file

      for (boost::filesystem::path::iterator i = parent.begin();
           i != parent.end();
           ++i) {
        dir /= *i;
        if (! boost::filesystem::is_directory(dir)) {
          boost::filesystem::create_directory(dir);
          if (! boost::filesystem::is_directory(dir))
            throw std::logic_error(std::string("Directory ") + dir.string() +
                                   " does not exist and could not be created");
        }
      }

      // Make sure where we want to write isn't a directory or something

      file = boost::filesystem::current_path() / file;

      if (boost::filesystem::exists(file) &&
          ! boost::filesystem::is_regular_file(file))
        throw std::logic_error(file.string() +
                               " already exists but is not a regular file");

      // Open the file, creating it if it doesn't already exist

      boost::filesystem::ofstream out(file);
      out << content;
      out.close();
    }
  };

  void Tree::do_update(boost::filesystem::path::iterator segment,
                       boost::filesystem::path::iterator end, ObjectPtr obj)
  {
    std::string name = (*segment).string();

    modified = true;

    entries_map::iterator i = entries.find(name);
    if (++segment == end) {
      assert(name == obj->name);

      if (i == entries.end()) {
        written = false;        // force the whole tree to be rewritten
        entries.insert(entries_pair(name, obj));
      } else {
        // If the object we're updating is just a blob, the tree doesn't
        // need to be regnerated entirely, it will just get updated by
        // git_object_write.
        if (obj->is_blob()) {
          ObjectPtr curr_obj = (*i).second;
          git_tree_entry_set_id(curr_obj->tree_entry, *obj);
          git_tree_entry_set_attributes(curr_obj->tree_entry, obj->attributes);
          if (name != obj->name) {
            sort_needed = true;
            entries.erase(i);
            entries.insert(entries_pair(obj->name, obj));
            git_tree_entry_set_name(curr_obj->tree_entry, obj->name.c_str());
          } else {
            (*i).second = obj;
          }
          obj->tree_entry = curr_obj->tree_entry;
        } else {
          written = false;      // force the whole tree to be rewritten
          (*i).second = obj;
        }
      }
    } else {
      TreePtr tree;
      if (i == entries.end()) {
        tree = repository->create_tree(name);
        entries.insert(entries_pair(name, tree));
      } else {
        tree = dynamic_cast<Tree *>((*i).second.get());
      }

      tree->do_update(segment, end, obj);

      written = false;          // force the whole tree to be rewritten
    }
  }

  void Tree::do_remove(boost::filesystem::path::iterator segment,
                       boost::filesystem::path::iterator end)
  {
    std::string name = (*segment).string();

    modified = true;

    entries_map::iterator i = entries.find(name);
    // It's OK for remove not to find what it's looking for, because it
    // may be that Subversion wishes to remove an empty directory, which
    // would never have been added in the first place.
    if (i != entries.end()) {
      if (++segment == end) {
        entries.erase(i);
        std::cerr << "Removing leaf entry " << name << std::endl;
        if (git_tree_remove_entry_byname(*this, name.c_str()) != 0)
          throw std::logic_error("Could not remove entry from tree");
      } else {
        TreePtr subtree = dynamic_cast<Tree *>((*i).second.get());
        subtree->do_remove(segment, end);
        if (subtree->empty()) {
          entries.erase(i);
          std::cerr << "Removing branch entry " << name << std::endl;
          if (git_tree_remove_entry_byname(*this, name.c_str()) != 0)
            throw std::logic_error("Could not remove entry from tree");
        } else {
          written = false;      // force the whole tree to be rewritten
        }
      }
    }
  }

  void Tree::write()
  {
    if (empty()) return;

    if (written) {
      if (modified) {
        if (sort_needed)
          git_tree_sort_entries(*this);
        if (git_object_write(*this) != 0)
          throw std::logic_error("Could not write tree object");
        std::cerr << "Rewrote tree to " << sha1() << std::endl;
      }
    } else {
      // written may be false now because of a change which requires us
      // to rewrite the entire tree.  To be on the safe side, just clear
      // out any existing entries in the git tree, and rewrite the
      // entries known to the Tree object.
      clear();

      for (entries_map::const_iterator i = entries.begin();
           i != entries.end();
           ++i) {
        ObjectPtr obj((*i).second);
        if (! obj->is_blob())
          obj->write();
        std::cerr << "Adding entry: " << obj->name << std::endl;
        if (git_tree_add_entry2(&obj->tree_entry, *this, *obj,
                                obj->name.c_str(), obj->attributes) != 0)
          throw std::logic_error("Could not add entry to tree");
      }
      git_tree_sort_entries(*this);
      if (git_object_write(*this) != 0)
        throw std::logic_error("Could not write tree object");
      std::cerr << "Wrote tree to " << sha1() << std::endl;
    }
    written  = true;
    modified = false;
  }

  void Commit::update(const boost::filesystem::path& pathname, ObjectPtr obj)
  {
    if (! tree)
      tree = repository->create_tree((*pathname.begin()).string());
    tree->update(pathname, obj);
  }

  CommitPtr Commit::clone()
  {
    CommitPtr new_commit = repository->create_commit();

    new_commit->tree   = tree;
    new_commit->prefix = prefix;
    new_commit->add_parent(this);

    return new_commit;
  }

  void Commit::write()
  {
    assert(tree);

    TreePtr subtree;
    if (prefix.empty()) {
      subtree = tree;
    } else {
      ObjectPtr obj = tree->lookup(prefix);
      assert(obj->is_tree());
      subtree = dynamic_cast<Tree *>(obj.get());
    }
    if (! subtree)
      subtree = tree = repository->create_tree();

    assert(! subtree->empty());
    subtree->write();
    std::cerr << "Wrote subtree " << subtree->sha1() << std::endl;

    git_commit_set_tree(*this, *subtree);

    if (git_object_write(*this) != 0)
      throw std::logic_error("Could not write out Git commit");
  }

  void Branch::update(Repository& repository, CommitPtr commit)
  {
    commit->write();
    repository.create_file(boost::filesystem::path("refs") / "heads" / name,
                           commit->sha1());
  }
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
