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

/**
 * @file   gitutil.cpp
 * @author John Wiegley
 *
 * @brief Core utilities for working with Git in C++
 *
 * This library is an intelligent wrapper around libgit2, which provides
 * core facilities but very little in the way of housekeeping.
 *
 * In general you use gitutil like this:
 *
 *   Git::DumbLogger log;
 *   Git::Repository repo("/path/to/repository", log);
 *   
 *   struct tm then;
 *   strptime("2005-04-07T22:13:13", "%Y-%m-%dT%H:%M:%S", &then);
 *   
 *   Git::CommitPtr commit = repo.create_commit();
 *   commit->update("foo/bar/baz.c",
 *                  repo.create_blob("baz.c", "#include <stdio.h>\n", 19));
 *   commit->update("foo/bar/bar.c",
 *                  repo.create_blob("bar.c", "#include <stdlib.h>\n", 20));
 *   commit->set_author("John Wiegley", "johnw@boostpro.com",
 *                      std::mktime(&then));
 *   commit->set_message("This is a sample commit.\n");
 *   commit->write();
 *   
 *   // Update the 'master' branch to refer to this commit
 *   repo.master_branch->commit = commit;
 *   repo.master_branch->update();
 */

#include "gitutil.h"

#ifndef ASSERTS
#undef assert
#define assert(x)
#endif

namespace Git {

#ifdef ASSERTS
/**
 * Verify that the size of the tree in memory matches the size of the
 * Git tree on disk.
 */
bool check_size(const Tree& tree)
{
  if (tree.written && tree.entries.size() != git_tree_entrycount(tree)) {
#ifdef DEBUG
    std::cerr << std::endl;
    std::cerr << "Mismatch in written entries for " << tree.name
              << " (" << &tree << ")" << std::endl;

    for (Tree::entries_map::const_iterator i = tree.entries.begin();
         i != tree.entries.end();
         ++i) {
      assert((*i).first == (*i).second->name);
      std::cerr << "entry = " << (*i).first << std::endl;
    }

    int len = git_tree_entrycount(tree);
    for (int i = 0; i < len; ++i) {
      git_tree_entry * entry = git_tree_entry_byindex(tree, i);
      if (! entry)
        throw std::logic_error("Could not read Git tree entry");

      std::cerr << "git entry = " << git_tree_entry_name(entry)
                << std::endl;
    }
#endif
    return false;
  }
  return true;
}
#endif

/**
 * Given a pair of path iterators describing segments of a path, update
 * the current tree so the Git entry corresponding to that path is set
 * to 'obj'.
 *
 * Trees use a copy-on-write optimization, and share as much structure
 * as possible with previous versions of the tree.
 */
bool Tree::do_update(filesystem::path::iterator segment,
                     filesystem::path::iterator end, ObjectPtr obj)
{
  assert(check_size(*this));

  std::string entry_name = (*segment).string();

  entries_map::iterator i = entries.find(entry_name);
  if (++segment == end) {
    assert(entry_name == obj->name);

    if (i == entries.end()) {
      entries.insert(entries_pair(entry_name, obj));

      written = false;        // force the whole tree to be rewritten
    } else {
      // If the object we're updating is just a blob, the tree doesn't
      // need to be regnerated entirely, it will just get updated by
      // git_object_write.
      if (written && obj->is_blob()) {
        ObjectPtr curr_obj((*i).second);
        assert(git_object_id(*curr_obj));

        git_tree_entry_set_id(curr_obj->tree_entry, *obj);
        git_tree_entry_set_attributes(curr_obj->tree_entry, obj->attributes);

        if (entry_name != obj->name) {
          entries.erase(i);
#ifdef ASSERTS
          std::pair<entries_map::iterator, bool> result =
#endif
            entries.insert(entries_pair(obj->name, obj));
          assert(result.second);

          git_tree_entry_set_name(curr_obj->tree_entry, obj->name.c_str());
        } else {
          (*i).second = obj;
        }

        obj->tree_entry = curr_obj->tree_entry;
      } else {
        (*i).second = obj;

        written = false;      // force the whole tree to be rewritten
      }
    }
  } else {
    TreePtr tree;
    if (i == entries.end()) {
      tree = repository->create_tree(entry_name);
      entries.insert(entries_pair(entry_name, tree));
    } else {
      tree = dynamic_cast<Tree *>((*i).second.get());
      (*i).second = tree = tree->copy();
    }
    assert(tree->is_tree());

    written = tree->do_update(segment, end, obj);
  }

  assert(check_size(*this));

  modified = true;

  return written;
}

/**
 * Given a pair of path iterators describing segments of a path, remove
 * the object in the current tree corresponding to that path.
 */
void Tree::do_remove(filesystem::path::iterator segment,
                     filesystem::path::iterator end)
{
  assert(check_size(*this));

  std::string entry_name = (*segment).string();

  entries_map::iterator i   = entries.find(entry_name);
  entries_map::iterator del = entries.end();

  // It's OK for remove not to find what it's looking for, because it
  // may be that Subversion wishes to remove an empty directory, which
  // would never have been added in the first place.
  if (i != del) {
    if (++segment == end) {
      del = i;
    } else {
      assert((*i).second->is_tree());
      TreePtr subtree = dynamic_cast<Tree *>((*i).second.get());
      (*i).second = subtree = subtree->copy();

      subtree->do_remove(segment, end);

      if (subtree->empty())
        del = i;
    }

    if (del != entries.end()) {
      (*del).second->tree_entry = NULL;
      entries.erase(del);

      if (written)
        git_check(git_tree_remove_entry_byname(*this, entry_name.c_str()));
    }

    assert(check_size(*this));

    modified = true;
  }
}

/**
 * Git::Tree constructor.
 *
 * This allocates the Git tree, but nothing is written to disk until
 * Tree::write is called.
 */
Tree::Tree(const Tree& other)
  : Object(other.repository, NULL, other.name, other.attributes),
    entries(other.entries), written(false), modified(false)
{
  allocate();
}

void Tree::allocate()
{
  git_tree * tree_obj;
  git_check(git_tree_new(&tree_obj, *repository));

  git_obj = reinterpret_cast<git_object *>(tree_obj);
}

/**
 * Write out a Git tree to disk.
 *
 * It is only at this time that we bother sorting the tree entries to
 * match Git's expectations, to save time.  Plus, if we've written this
 * tree in the past and the *structure* of the tree has not changed --
 * just the contents of some of its files -- we don't have to sort at
 * all.  We just write out the same structure, only with modified
 * hashes.
 */
void Tree::write()
{
  if (empty()) return;

  if (written) {
    if (modified) {
      assert(! entries.empty());
      assert(check_size(*this));
      git_check(git_object_write(*this));
    }
  } else {
    assert(check_size(*this));

    // `written' may be false now because of a change which requires us
    // to rewrite the entire tree.  To be on the safe side, just clear
    // out any existing entries in the git tree, and rewrite the entries
    // known to the Tree object.
    git_tree_clear_entries(*this);

    for (entries_map::const_iterator i = entries.begin();
         i != entries.end();
         ++i) {
      ObjectPtr obj((*i).second);
      assert(obj->name == (*i).first);

      if (! obj->is_written())
        obj->write();

      git_check(git_tree_add_entry_unsorted(&obj->tree_entry, *this, *obj,
                                            obj->name.c_str(),
                                            obj->attributes));
    }

    assert(check_size(*this));

    git_tree_sort_entries(*this);
    git_check(git_object_write(*this));

    assert(check_size(*this));

    written = true;
  }

  modified = false;
}

/**
 * Debug routine that dumps a tree's contents to an output stream.
 */
void Tree::dump_tree(std::ostream& out, int depth)
{
  for (entries_map::const_iterator i = entries.begin();
       i != entries.end();
       ++i) {
    for (int j = 0; j < depth; ++j)
      out << "  ";

    out << (*i).first;

    if ((*i).second->is_tree()) {
      out << "/\n";
      dynamic_cast<Tree *>((*i).second.get())->dump_tree(out, depth + 1);
    } else {
      out << '\n';
    }
  }
}

Commit::Commit(RepositoryPtr repo, git_commit * commit, CommitPtr _parent,
           const std::string& name, int attributes)
  : Object(repo, reinterpret_cast<git_object *>(commit),
           name, attributes), parent(_parent), new_branch(false)
{
  if (parent) {
    if (! git_object_id(*parent))
      parent->write();

    if (git_commit_add_parent(*this, *parent))
      throw std::logic_error("Could not add parent to commit");
  }
}

/**
 * Given a pathname and a Git object, update the tree relating to this
 * commit so it now refers to this object.
 */
void Commit::update(const filesystem::path& pathname, ObjectPtr obj)
{
  if (! tree)
    tree = repository->create_tree();
  tree->update(pathname, obj);
}

void Commit::remove(const filesystem::path& pathname)
{
  if (tree) {
    tree->remove(pathname);
    if (tree->empty())
       tree = NULL;
  }
}

/**
 * Does this commit have a tree associated with it?  If no objects have
 * been updated within it, the answer will be no.
 */
bool Commit::has_tree() const
{
#if 1
  return tree;
#else
  if (branch->prefix.empty())
    return tree;
  else
    return tree->lookup(branch->prefix);
#endif
}

/**
 * Clone a commit so we can work on a child of that commit.
 *
 * @param with_copy
 *   If true, copy the underlying Tree right away, rather than relying
 *   on the copy-on-write optimization.
 */
CommitPtr Commit::clone(bool with_copy)
{
  if (! is_written())
    write();

  CommitPtr new_commit = repository->create_commit(this);
  new_commit->tree = with_copy ? new Tree(*tree) : tree;
  return new_commit;
}

void Commit::write()
{
  assert(! is_written());
  assert(tree);

#if 1
  TreePtr subtree = tree;
#else
  TreePtr subtree;
  if (branch->prefix.empty()) {
    subtree = tree;
  } else {
    // Lookup the part of this commit's tree which actually belongs to
    // this commit.  This is because in Subversion, each full tree
    // contains many, many branches.  Rather than splitting up this tree
    // into many sub-trees, we associate the full tree with the commits
    // in all the branches, but use the branch prefix to extract the
    // subtree within this main tree relating to the commit.
    ObjectPtr obj(tree->lookup(branch->prefix));
    if (! obj)
      return;                 // don't write commits with empty trees
    assert(obj->is_tree());
    subtree = dynamic_cast<Tree *>(obj.get());
  }
  if (! subtree)
    subtree = tree = repository->create_tree();
#endif

  assert(! subtree->empty());
  if (! subtree->is_written())
    subtree->write();

  git_commit_set_tree(*this, *subtree);

  git_check(git_object_write(*this));

  // Once written, we no longer need the parent
  parent = NULL;
}

/**
 * Get the commit object for the given branch to which changes should be
 * applied.  It is expected that they will be applied, and so the commit
 * is added to the Repository's commit_queue right away.  If it ends up
 * not being modified, nothing will happen when the commit queue is
 * flushed.
 *
 * @param from_branch
 *   If from_branch is non-NULL, it means this branch is being created
 *   by copying a directory from a pre-existing branch.
 */
CommitPtr Branch::get_commit(BranchPtr from_branch)
{
  if (next_commit) {
    std::vector<CommitPtr>::iterator i =
      std::find(repository->commit_queue.begin(),
                repository->commit_queue.end(), next_commit);
    if (i == repository->commit_queue.end()) {
      assert(false);
      repository->commit_queue.push_back(next_commit);
    }
    return next_commit;
  }

  if (commit) {
    assert(! from_branch);
    next_commit = commit->clone();
  } else {
    // If the first action is a dir/add/copyfrom, then this will get set
    // correctly, otherwise it's a parentless branch, which is also OK.
    repository->log.debug(std::string("Branch start: ") + name);

    if (from_branch) {
      if (from_branch->commit) {
        next_commit = from_branch->commit->clone();
      } else {
        next_commit = repository->create_commit();
        repository->log.warn(std::string("Branch ") + name +
                             " starts out empty");
      }
      next_commit->new_branch = true;
    } else {
      next_commit = repository->create_commit();
      repository->log.debug(std::string("Branch ") + name +
                            " starts out empty");
    }
  }
  next_commit->branch = this;

  repository->commit_queue.push_back(next_commit);

  return next_commit;
}

/**
 * Update a branch so that it refers to either its own commit (to which
 * changes may have been made), or to a whole new commit, passed in
 * `ptr'.
 */
void Branch::update(CommitPtr ptr)
{
  if (ptr)
    commit = ptr;

  assert(repository);
  assert(commit);

  if (! commit->is_written())
    commit->write();

  repository->create_ref(commit, name);
}

BlobPtr Repository::create_blob(const std::string& name, const char * data,
                                std::size_t len, int attributes)
{
  git_blob * git_blob;
  git_check(git_blob_new(&git_blob, repo));
  git_check(git_blob_set_rawcontent(git_blob, data, len));

  Blob * blob = new Blob(this, git_blob, NULL, name, attributes);
  blob->repository = this;
  return blob;
}

TreePtr Repository::create_tree(const std::string& name, int attributes)
{
  git_tree * git_tree;
  git_check(git_tree_new(&git_tree, repo));

  Tree * tree = new Tree(this, git_tree, name, attributes);
  tree->repository = this;
  return tree;
}

#if defined(READ_EXISTING_GIT_REPOSITORY)

TreePtr Repository::read_tree(git_tree * tree_obj, const std::string& name,
                              int attributes)
{
  TreePtr tree(new Tree(this, tree_obj, name, attributes));

  std::size_t size = git_tree_entrycount(tree_obj);
  for (std::size_t i = 0; i < size; ++i) {
    git_tree_entry * entry =
      git_tree_entry_byindex(tree_obj, static_cast<int>(i));
    if (! entry)
      throw std::logic_error("Could not read Git tree entry");

    std::string entry_name(git_tree_entry_name(entry));
    int entry_attributes(static_cast<int>(git_tree_entry_attributes(entry)));

    git_object * git_obj;
    git_check(git_tree_entry_2object(&git_obj, entry));

    switch (git_object_type(git_obj)) {
    case GIT_OBJ_BLOB: {
      git_blob * blob_obj(reinterpret_cast<git_blob *>(git_obj));
      BlobPtr    blob(new Blob(this, blob_obj, NULL, entry_name,
                               entry_attributes));
      blob->tree_entry = entry;
      tree->entries.insert(Tree::entries_pair(entry_name, blob));
      break;
    }
    case GIT_OBJ_TREE: {
      git_tree * subtree_obj(reinterpret_cast<git_tree *>(git_obj));
      TreePtr    subtree(read_tree(subtree_obj, entry_name, entry_attributes));
      subtree->tree_entry = entry;
      tree->entries.insert(Tree::entries_pair(entry_name, subtree));
      break;
    }

    default:
      assert(false);
      break;
    }
  }

  assert(check_size(*tree));

  tree->written = true;

  return tree;
}

#endif // READ_EXISTING_GIT_REPOSITORY

CommitPtr Repository::create_commit(CommitPtr parent)
{
  git_commit * git_commit;
  git_check(git_commit_new(&git_commit, repo));
  return new Commit(this, git_commit, parent);
}

#if defined(READ_EXISTING_GIT_REPOSITORY)

CommitPtr Repository::read_commit(const git_oid * oid)
{
  git_commit * git_commit;
  git_check(git_commit_lookup(&git_commit, *this, oid));

  // The commit prefix is no longer important at this stage, as its
  // only used to determining which subset of the commit's tree to use
  // when it's first written.

  const git_tree * commit_tree(git_commit_tree(git_commit));
  const git_oid *  tree_id(git_tree_id(const_cast<git_tree *>(commit_tree)));
  git_tree *       tree;

  git_check(git_tree_lookup(&tree, *this, tree_id));

  CommitPtr commit(new Commit(this, git_commit));
  commit->tree = read_tree(tree);
  return commit;
}

#endif // READ_EXISTING_GIT_REPOSITORY

bool Repository::write(int related_revision,
                       function<void(BranchPtr)> on_delete_branch)
{
  std::size_t branches_modified = 0;

  for (std::vector<CommitPtr>::iterator i = commit_queue.begin();
       i != commit_queue.end();
       ++i) {
    CommitPtr commit(*i);
    if (! commit->is_modified() && ! commit->is_new_branch()) {
      if (log.debug_mode()) {
        std::ostringstream buf;
        buf << "Commit had no Git-visible modifications";
        log.debug(buf.str());
      }
      continue;
    }

    assert(commit == commit->branch->next_commit);
    commit->branch->next_commit.reset();

    if (commit->has_tree()) {
      set_commit_info(commit);

      // Only now does the commit get associated with its branch
      commit->branch->commit = commit;

      assert(! commit->is_written());
      commit->write();

      if (commit->branch->prefix.empty())
        log.debug(std::string("Updated branch ") + commit->branch->name);
      else
        log.debug(std::string("Updated branch ") + commit->branch->name +
                  " (prefix \"" + commit->branch->prefix.string() + "\")");

      ++branches_modified;
    } else {
      delete_branch(commit->branch, related_revision);
      on_delete_branch(commit->branch);
    }
  }
  commit_queue.clear();

  return branches_modified > 0;
}

/**
 * Find the branch within the repository associated with the Subversion
 * pathname.
 */
BranchPtr Repository::find_branch(const std::string& name, BranchPtr default_obj)
{
  branches_map::iterator i = branches.find(name);
  if (i != branches.end())
    return (*i).second;

  if (default_obj) {
    std::pair<branches_map::iterator, bool> result =
      branches.insert(branches_value(name, default_obj));
    if (! result.second) {
      log.warn(std::string("Branch name repeated: ") + name);
      return NULL;
    }
    return default_obj;
  }

  return NULL;
}

void Repository::delete_branch(BranchPtr branch, int related_revision)
{
  if (log.debug_mode()) {
    std::ostringstream buf;
    buf << "End of branch " << branch->name;
    log.debug(buf.str());
  }

  if (branch->commit) {
    // If the branch is to be deleted, tag the last commit on
    // that branch with a special FOO__deleted_rXXXX name so the
    // history is preserved.
    std::ostringstream buf;
    buf << branch->name << "__deleted_r" << related_revision;
    std::string tag_name(buf.str());
    create_tag(branch->commit, tag_name);
    log.debug(std::string("Wrote tag ") + tag_name);
  }

  for (branches_map::iterator b = branches.begin();
       b != branches.end();
       ++b) {
    if (branch == (*b).second) {
      (*b).second = new Branch(*branch);
      break;
    }
  }
}

void Repository::write_branches()
{
  assert(commit_queue.empty());

  for (branches_map::iterator i = branches.begin();
       i != branches.end();
       ++i) {
    if ((*i).second->commit) {
      if ((*i).second->is_tag) {
        create_tag((*i).second->commit, (*i).second->name);
        log.info(std::string("Wrote tag ") + (*i).second->name);
      } else {
        (*i).second->update();
        log.info(std::string("Wrote branch ") + (*i).second->name);
      }
    } else {
      log.debug(std::string("Branch ") + (*i).second->name + " is empty");
    }
  }
}

void Repository::garbage_collect()
{
  std::system("git config gc.autopacklimit 0");
  std::system("git config loose.compression 0");
  std::system("git config pack.compression 1");

  std::system("git gc");
}

void Repository::create_tag(CommitPtr commit, const std::string& name)
{
  git_tag * tag;
  git_check(git_tag_new(&tag, *this));

  git_tag_set_target(tag, *commit);
  git_tag_set_name(tag, name.c_str());
  git_tag_set_tagger(tag, git_commit_author(*commit));
  git_tag_set_message(tag, name.c_str());

  git_object * git_obj(reinterpret_cast<git_object *>(tag));
  git_check(git_object_write(git_obj));

  create_ref(git_obj, name, true);
}

void Repository::create_ref(git_object * obj, const std::string& name,
                            bool is_tag)
{
  create_file(filesystem::path("refs")
              / (is_tag ? "tags" : "heads") / name,
              git_sha1(git_object_id(obj)));
}

void Repository::create_ref(ObjectPtr obj, const std::string& name, bool is_tag)
{
  create_file(filesystem::path("refs")
              / (is_tag ? "tags" : "heads") / name, git_sha1(*obj));
}

void Repository::create_file(const filesystem::path& pathname,
                             const std::string& content)
{
  filesystem::path dir(filesystem::current_path());
  filesystem::path file(filesystem::path(".git") / pathname);
  filesystem::path parent(file.parent_path());

  // Make sure the directory exists for the file

  for (filesystem::path::iterator i = parent.begin();
       i != parent.end();
       ++i) {
    dir /= *i;
    if (! filesystem::is_directory(dir)) {
      filesystem::create_directory(dir);
      if (! filesystem::is_directory(dir))
        throw std::logic_error(std::string("Directory ") + dir.string() +
                               " does not exist and could not be created");
    }
  }

  // Make sure where we want to write isn't a directory or something

  file = filesystem::current_path() / file;

  if (filesystem::exists(file) &&
      ! filesystem::is_regular_file(file))
    throw std::logic_error(file.string() +
                           " already exists but is not a regular file");

  // Open the file, creating it if it doesn't already exist

  filesystem::ofstream out(file);
  out << content;
  out.close();
}

} // namespace Git
