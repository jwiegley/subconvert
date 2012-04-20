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

#include "system.hpp"

using namespace boost;

#include "config.h"

namespace Git
{
  inline void git_check(int result) {
    if (result != 0)
      throw std::logic_error(git_strerror(result));
  }

  inline std::string git_sha1(const git_oid * git_oid) {
    char checksum[41];
    git_oid_fmt(checksum, git_oid);
    checksum[40] = '\0';
    return checksum;
  }

  class Object;
  class Blob;
  class Tree;
  class Commit;
  class Repository;
  class Branch;

  typedef Repository * RepositoryPtr;

  typedef intrusive_ptr<Object>  ObjectPtr;

  class Object : public noncopyable
  {
    friend class Repository;
    friend class Tree;
    friend class Commit;

  protected:
    RepositoryPtr repository;
    git_object *  git_obj;

    mutable int refc;

    void acquire() const {
      assert(refc >= 0);
      refc++;
    }
    void release() const {
      assert(refc > 0);
      if (--refc == 0) {
        const_cast<Object *>(this)->deallocate();
        checked_delete(this);
      }
    }

  public:
    std::string name;
    int         attributes;

    git_tree_entry * tree_entry;

    Object(RepositoryPtr _repository, git_object * _git_obj,
           const std::string& _name = "", int _attributes = 0)
      : repository(_repository), git_obj(_git_obj), refc(0),
        name(_name), attributes(_attributes), tree_entry(NULL) {}
    virtual ~Object() {
      assert(refc == 0);
    }

    virtual void allocate() = 0;
    virtual void deallocate() {
      if (git_obj != NULL) {
        // Only free the object if it's been written, because otherwise
        // libgit2 fails to remove unwritten objects properly from its
        // hash->object table, and will then try later to free it again.
        if (git_object_id(git_obj))
          git_object_free(git_obj);
        git_obj = NULL;
      }
    }

    operator git_object *() {
      return git_obj;
    }

    virtual operator const git_oid *() const {
      return git_object_id(git_obj);
    }
    virtual const git_oid * get_oid() const {
      return git_object_id(git_obj);
    }

    std::string sha1() const {
      return git_sha1(*this);
    }

    virtual bool is_blob() const {
      return true;
    }
    virtual bool is_tree() const {
      return false;
    }

    virtual bool is_modified() const {
      return false;
    }
    virtual bool is_written() const {
      return git_object_id(git_obj);
    }

    virtual ObjectPtr copy_to_name(const std::string& to_name) = 0;

    virtual void write() {
      git_check(git_object_write(*this));
    }

    friend inline void intrusive_ptr_add_ref(Object * obj) {
      obj->acquire();
    }
    friend inline void intrusive_ptr_release(Object * obj) {
      obj->release();
    }
  };

  typedef intrusive_ptr<Blob> BlobPtr;

  class Blob : public Object
  {
  protected:
    const git_oid * cached_oid;

  public:
    Blob(RepositoryPtr repository, git_blob * blob, const git_oid * transfer,
         const std::string& name, int attributes = 0100644)
      : Object(repository, reinterpret_cast<git_object *>(blob),
               name, attributes) {
      if (transfer) {
        cached_oid = transfer;
      } else {
        write();
        cached_oid = Object::get_oid();
      }
    }

    virtual void allocate() {
      // This gets allocated by Repository::create_blob, not here.
    }

    operator git_blob *() const {
      return reinterpret_cast<git_blob *>(git_obj);
    }

    virtual operator const git_oid *() const {
      return cached_oid;
    }
    virtual const git_oid * get_oid() const {
      return cached_oid;
    }

    virtual bool is_written() const {
      return true;
    }

    virtual ObjectPtr copy_to_name(const std::string& to_name) {
      if (name == to_name)
        return this;
      else
        return new Blob(repository, reinterpret_cast<git_blob *>(git_obj),
                        cached_oid, to_name, attributes);
    }
  };

  typedef intrusive_ptr<Tree> TreePtr;

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

    ObjectPtr do_lookup(filesystem::path::iterator segment,
                        filesystem::path::iterator end)
    {
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

    bool do_update(filesystem::path::iterator segment,
                   filesystem::path::iterator end, ObjectPtr obj);

    void do_remove(filesystem::path::iterator segment,
                   filesystem::path::iterator end);

  public:
    Tree(RepositoryPtr repository, git_tree * tree,
         const std::string& name, int attributes = 0040000)
      : Object(repository, reinterpret_cast<git_object *>(tree),
               name, attributes), written(false), modified(false) {}

    Tree(const Tree& other);

    virtual void allocate();

    operator git_tree *() const {
      return reinterpret_cast<git_tree *>(git_obj);
    }

    virtual bool is_blob() const {
      return false;
    }
    virtual bool is_tree() const {
      return true;
    }

    virtual bool is_modified() const {
      return modified;
    }
    virtual bool is_written() const {
      return written && ! modified;
    }

    virtual TreePtr copy() {
      return new Tree(*this);
    }
    virtual ObjectPtr copy_to_name(const std::string& to_name) {
      TreePtr new_tree(copy());
      new_tree->name = to_name;
      return new_tree;
    }

    bool empty() const {
      return entries.empty();
    }

    ObjectPtr lookup(const filesystem::path& pathname) {
      return do_lookup(pathname.begin(), pathname.end());
    }

    void update(const filesystem::path& pathname, ObjectPtr obj) {
      do_update(pathname.begin(), pathname.end(), obj);
    }

    void remove(const filesystem::path& pathname) {
      do_remove(pathname.begin(), pathname.end());
    }

    virtual void write();

    void dump_tree(std::ostream& out, int depth = 0);
  };

  typedef intrusive_ptr<Commit> CommitPtr;
  typedef intrusive_ptr<Branch> BranchPtr;

  class Commit : public Object
  {
    friend class Repository;

  public:
    CommitPtr parent;
    TreePtr   tree;
    BranchPtr branch;
    bool      new_branch;

    Commit(RepositoryPtr repo, git_commit * commit, CommitPtr _parent = NULL,
           const std::string& name = "", int attributes = 0040000);

    virtual void allocate() {
      // This gets allocated by Repository::create_commit, not here.
    }

    operator git_commit *() const {
      return reinterpret_cast<git_commit *>(git_obj);
    }

    virtual bool is_blob() const {
      return false;
    }
    virtual bool is_tree() const {
      return false;
    }

    virtual bool is_modified() const {
      return tree && tree->is_modified();
    }
    bool is_new_branch() const {
      return new_branch;
    }

    bool has_tree() const;
    void set_tree(TreePtr _tree) {
      tree = _tree;
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

    std::string get_message() const {
      return git_commit_message(*this);
    }

    void set_message(const std::string& message) {
      git_commit_set_message(*this, message.c_str());
    }

    void set_author(const std::string& name, const std::string& email,
                    time_t time)
    {
      git_signature * signature =
        git_signature_new(name.c_str(), email.c_str(), time, 0);
      if (! signature)
        throw std::logic_error("Could not create signature object");

      git_commit_set_author(*this, signature);
      git_commit_set_committer(*this, signature);
    }

    ObjectPtr lookup(const filesystem::path& pathname) {
      return tree ? tree->lookup(pathname) : tree;
    }

    void update(const filesystem::path& pathname, ObjectPtr obj);
    void remove(const filesystem::path& pathname);

    virtual void write();

    void dump_tree(std::ostream& out) {
      if (tree)
        tree->dump_tree(out);
    }
  };

  class Branch
  {
  public:
    RepositoryPtr    repository;
    std::string      name;
    filesystem::path prefix;
    bool             is_tag;
    CommitPtr        commit;
    CommitPtr        next_commit;

    Branch(RepositoryPtr repo, const std::string& _name = "master",
           bool _is_tag = false)
      : repository(repo), name(_name), is_tag(_is_tag), refc(0) {}

    Branch(const Branch& other)
      : repository(other.repository), name(other.name), prefix(other.prefix),
        is_tag(other.is_tag), refc(0) {}

    ~Branch() throw() {
      assert(refc == 0);
    }

    mutable int refc;

    void acquire() const {
      assert(refc >= 0);
      refc++;
    }
    void release() const {
      assert(refc > 0);
      if (--refc == 0)
        checked_delete(this);
    }

    CommitPtr get_commit(BranchPtr from_branch = NULL);
    void      update(CommitPtr ptr = NULL);

    friend inline void intrusive_ptr_add_ref(Branch * obj) {
      obj->acquire();
    }
    friend inline void intrusive_ptr_release(Branch * obj) {
      obj->release();
    }
  };

  struct Logger
  {
    virtual bool debug_mode() const = 0;

    virtual void debug(const std::string& message) const = 0;
    virtual void info(const std::string& message) const = 0;
    virtual void warn(const std::string& message) const = 0;
    virtual void error(const std::string& message) const = 0;

    virtual ~Logger() throw() {}
  };

  struct DumbLogger : public Logger
  {
    virtual bool debug_mode() const { return false; }

    virtual void debug(const std::string&) const {}
    virtual void info(const std::string&) const {}
    virtual void warn(const std::string& message) const {
      std::cerr << message << std::endl;
    }
    virtual void error(const std::string& message) const {
      std::cerr << message << std::endl;
    }
  };

  inline void no_commit_info(CommitPtr) {}

  class Repository
  {
    git_repository * repo;

  public:
    typedef std::map<std::string, BranchPtr> branches_map;
    typedef branches_map::value_type         branches_value;

    Logger&                   log;
    branches_map              branches;
    std::vector<CommitPtr>    commit_queue;
    function<void(CommitPtr)> set_commit_info;

    Repository(const filesystem::path& pathname, Logger& _log,
               function<void(CommitPtr)> _set_commit_info = no_commit_info)
      : log(_log), set_commit_info(_set_commit_info)
    {
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

    BlobPtr   create_blob(const std::string& name,
                          const char * data, std::size_t len,
                          int attributes = 0100644);
              
    TreePtr   create_tree(const std::string& name = "",
                          int attributes = 040000);
#if defined(READ_EXISTING_GIT_REPOSITORY)
    TreePtr   read_tree(git_tree * git_tree, const std::string& name = "",
                        int attributes = 0040000);
#endif
              
    CommitPtr create_commit(CommitPtr parent = NULL);
#if defined(READ_EXISTING_GIT_REPOSITORY)
    CommitPtr read_commit(const git_oid * oid);
#endif

    BranchPtr find_branch(const std::string& name, BranchPtr default_obj = NULL);
    void      delete_branch(BranchPtr branch, int related_revision);
    void      write_branches();
              
    bool      write(int related_revision,
                    function<void(BranchPtr)> on_delete_branch);

    void      create_tag(CommitPtr commit, const std::string& name);
    void      create_ref(git_object * obj, const std::string& name,
                         bool is_tag = false);
    void      create_ref(ObjectPtr obj, const std::string& name,
                         bool is_tag = false);
    void      create_file(const filesystem::path& pathname,
                          const std::string& content = "");

#if defined(READ_EXISTING_GIT_REPOSITORY)

    struct commit_iterator
    {
      typedef CommitPtr value_type;

      CommitPtr     commit;
      RepositoryPtr repo;

      shared_ptr<git_revwalk> walk;

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
        //git_commit * git_commit = git_revwalk_next(walk.get());
        //commit = new Commit(repo, git_commit);
      }
      commit_iterator& operator++(int) {
        ++(*this);
        return *this;
      }
    };

    commit_iterator commits_begin()
    {
      commit_iterator start;
      git_revwalk *   walk;

      git_check(git_revwalk_new(&walk, repo));

      start.repo = this;
      start.walk = shared_ptr<git_revwalk>(walk, git_revwalk_free);

      return start;
    }
    commit_iterator commits_end() {
      return commit_iterator();
    }

#endif // READ_EXISTING_GIT_REPOSITORY
  };
}

#endif // _GITUTIL_H
