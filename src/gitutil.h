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

#ifndef _GITUTIL_H
#define _GITUTIL_H

#include <map>
#include <string>

#include "config.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/checked_delete.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/optional.hpp>

#define BOOST_FILESYSTEM_VERSION 3
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <git2.h>

namespace Git
{
  class Object;
  class Blob;
  class Tree;
  class Commit;
  class Repository;
  class Branch;

  typedef boost::intrusive_ptr<Object> ObjectPtr;

  class Object : public boost::noncopyable
  {
    friend class Repository;
    friend class Tree;
    friend class Commit;

  protected:
    Repository * repository;
    git_object * git_obj;

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

    Object(Repository * _repository, git_object * _git_obj,
           const std::string& _name, int _attributes)
      : repository(_repository), git_obj(_git_obj), refc(0),
        name(_name), attributes(_attributes), tree_entry(NULL) {}
    virtual ~Object() {
      assert(refc == 0);
    }

    operator git_object *() {
      return git_obj;
    }
    operator const git_oid *() const {
      return git_object_id(git_obj);
    }

    const git_oid * get_oid() const {
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

    virtual ObjectPtr copy_to_name(const std::string& to_name) = 0;

    virtual void write() = 0;

    friend inline void intrusive_ptr_add_ref(Object * obj) {
      obj->acquire();
    }
    friend inline void intrusive_ptr_release(Object * obj) {
      obj->release();
    }
  };

  typedef boost::intrusive_ptr<Blob> BlobPtr;

  class Blob : public Object
  {
  public:
    Blob(Repository * repository, git_blob * blob,
         const std::string& name, int attributes = 0100644)
      : Object(repository, reinterpret_cast<git_object *>(blob),
               name, attributes) {}

    operator git_blob *() const {
      return reinterpret_cast<git_blob *>(git_obj);
    }

    virtual ObjectPtr copy_to_name(const std::string&) {
      return this;
    }

    virtual void write() {}
  };

  typedef boost::intrusive_ptr<Tree> TreePtr;

  class Tree : public Object
  {
    friend class Repository;

    friend bool check_size(const Tree& tree);

  protected:
    typedef std::map<std::string, ObjectPtr>  entries_map;
    typedef std::pair<std::string, ObjectPtr> entries_pair;

    entries_map entries;
    bool        written;
    bool        modified;

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
    Tree(Repository * repository, git_tree * tree,
         const std::string& name, int attributes = 0040000)
      : Object(repository, reinterpret_cast<git_object *>(tree),
               name, attributes), written(false), modified(false) {}

    Tree(const Tree& other);

    operator git_tree *() const {
      return reinterpret_cast<git_tree *>(git_obj);
    }

    virtual bool is_blob() const {
      return false;
    }
    virtual bool is_tree() const {
      return true;
    }

    virtual ObjectPtr copy_to_name(const std::string& to_name) {
      TreePtr new_tree(new Tree(*this));
      new_tree->name = to_name;
      return new_tree;
    }

    bool empty() const {
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

    virtual void write();
  };

  typedef boost::intrusive_ptr<Commit> CommitPtr;

  class Commit : public Object
  {
    friend class Repository;

  protected:
    TreePtr                 tree;
    boost::filesystem::path prefix;

  public:
    Commit(Repository * repo, git_commit * commit,
           const std::string& name = "", int attributes = 0040000)
      : Object(repo, reinterpret_cast<git_object *>(commit),
               name, attributes) {}

    operator git_commit *() const {
      return reinterpret_cast<git_commit *>(git_obj);
    }

    virtual bool is_blob() const {
      return false;
    }
    virtual bool is_tree() const {
      return false;
    }

    virtual ObjectPtr copy_to_name(const std::string& to_name) {
      CommitPtr new_commit(clone(true));
      new_commit->name = to_name;
      return new_commit;
    }

#ifdef READ_FROM_DISK
    CommitPtr clone(bool with_copy = false);
#else
    CommitPtr clone(bool with_copy = true);
#endif

    void add_parent(CommitPtr parent) {
      if (! git_object_id(*parent))
        parent->write();

      if (git_commit_add_parent(*this, *parent))
        throw std::logic_error("Could not add parent to commit");
    }

    std::string get_message() const {
      return git_commit_message(*this);
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

    void set_prefix(const boost::filesystem::path& _prefix) {
      prefix = _prefix;
    }

    ObjectPtr lookup(const boost::filesystem::path& pathname) {
      return tree ? tree->lookup(pathname) : tree;
    }

    void update(const boost::filesystem::path& pathname, ObjectPtr obj);
    void remove(const boost::filesystem::path& pathname);

    virtual void write();
  };

  class Branch
  {
  public:
    std::string             name;
    boost::filesystem::path prefix;
    bool                    is_tag;
    CommitPtr               commit;
    //int                     final_rev;

    Branch(const std::string& _name = "master")
      : name(_name), is_tag(false), commit(NULL) /* , final_rev(0) */ {}

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

    operator git_repository *() const {
      return repo;
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

      Blob * blob = new Blob(this, git_blob, name, attributes);
      blob->repository = this;
      return blob;
    }

    TreePtr create_tree(const std::string& name = "", int attributes = 040000) {
      git_tree * git_tree;
      if (git_tree_new(&git_tree, repo) != 0)
        throw std::logic_error("Could not create Git tree");

      Tree * tree = new Tree(this, git_tree, name, attributes);
      tree->repository = this;
      return tree;
    }

    CommitPtr create_commit() {
      git_commit * git_commit;
      if (git_commit_new(&git_commit, repo) != 0)
        throw std::logic_error("Could not create Git commit");
      return new Commit(this, git_commit);
    }

    TreePtr   read_tree(git_tree * git_tree, const std::string& name = "",
                        int attributes = 0040000);
    CommitPtr read_commit(const git_oid * oid);

    struct commit_iterator {
      typedef CommitPtr value_type;

      CommitPtr                      commit;
      Repository *                   repo;
      boost::shared_ptr<git_revwalk> walk;

      commit_iterator() : commit(NULL), repo(NULL) {}
      ~commit_iterator() {}

      bool operator==(const commit_iterator& other) const {
        return commit == other.commit;
      }
      bool operator!=(const commit_iterator& other) const {
        return ! (*this == other);
      }

      CommitPtr operator *() {
        return commit;
      }
      void operator++() {
        git_commit * git_commit = git_revwalk_next(walk.get());
        commit = new Commit(repo, git_commit);
      }
      commit_iterator& operator++(int) {
        ++(*this);
        return *this;
      }
    };

    commit_iterator commits_begin() {
      commit_iterator start;
      git_revwalk *   walk;

      if (git_revwalk_new(&walk, repo) != 0)
        throw std::logic_error("Could not create repository revision walker");

      start.repo = this;
      start.walk = boost::shared_ptr<git_revwalk>(walk, git_revwalk_free);

      return start;
    }
    commit_iterator commits_end() {
      return commit_iterator();
    }

    void create_file(const boost::filesystem::path& pathname,
                     const std::string& content = "");
  };
}

#endif // _GITUTIL_H
