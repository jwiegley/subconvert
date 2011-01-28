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

#include "gitutil.h"

#define OPTIMIZE (1)
//#define DEBUG    (1)

#ifdef DEBUG
#include <iostream>
#endif
#include <sstream>

#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/fstream.hpp>

#ifndef ASSERTS
#undef assert
#define assert(x)
#endif

namespace Git
{
#ifdef ASSERTS
  bool check_size(const Tree& tree) {
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

  void Tree::do_update(boost::filesystem::path::iterator segment,
                       boost::filesystem::path::iterator end, ObjectPtr obj)
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
#ifdef OPTIMIZE
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
        } else
#endif
        {
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
        (*i).second = (*i).second->copy_to_name((*i).first);
        tree = dynamic_cast<Tree *>((*i).second.get());
      }
      assert(tree->is_tree());

      written = false;          // force the whole tree to be rewritten

      tree->do_update(segment, end, obj);
    }

    assert(check_size(*this));

    modified = true;
  }

  void Tree::do_remove(boost::filesystem::path::iterator segment,
                       boost::filesystem::path::iterator end)
  {
    assert(check_size(*this));

    std::string entry_name = (*segment).string();

    entries_map::iterator i   = entries.find(entry_name);
    entries_map::iterator del = entries.end();

    // It's OK for remove not to find what it's looking for, because it
    // may be that Subversion wishes to remove an empty directory, which
    // would never have been added in the first place.
    if (i != entries.end()) {
      if (++segment == end) {
        del = i;
      } else {
        assert((*i).second->is_tree());
        (*i).second = (*i).second->copy_to_name((*i).first);
        TreePtr subtree = dynamic_cast<Tree *>((*i).second.get());

        subtree->do_remove(segment, end);

        if (subtree->empty())
          del = i;
        else
          written = false;      // force the whole tree to be rewritten
      }

      if (del != entries.end()) {
        (*del).second->tree_entry = NULL;
        entries.erase(del);

#ifndef OPTIMIZE
        written = false;        // force the whole tree to be rewritten
#else
        if (written &&
            git_tree_remove_entry_byname(*this, entry_name.c_str()) != 0)
          throw std::logic_error("Could not remove entry from tree");
#endif

        modified = true;
      }

      assert(check_size(*this));
    }
  }

  Tree::Tree(const Tree& other)
    : Object(other.repository, NULL, other.name, other.attributes),
      entries(other.entries), written(false), modified(false)
  {
    git_tree * tree_obj;
    if (git_tree_new(&tree_obj, *repository) != 0)
      throw std::logic_error("Could not create Git tree");

    git_obj = reinterpret_cast<git_object *>(tree_obj);
  }

  void Tree::write()
  {
    if (empty()) return;

#ifdef OPTIMIZE
    if (written) {
      if (modified) {
        assert(! entries.empty());
        assert(check_size(*this));

        int result = git_object_write(*this);
        if (result != 0)
          throw std::logic_error(git_strerror(result));
      }
    } else
#endif
    {
      assert(check_size(*this));

      // written may be false now because of a change which requires us
      // to rewrite the entire tree.  To be on the safe side, just clear
      // out any existing entries in the git tree, and rewrite the
      // entries known to the Tree object.
      git_tree_clear_entries(*this);

      for (entries_map::const_iterator i = entries.begin();
           i != entries.end();
           ++i) {
        ObjectPtr obj((*i).second);
        assert(obj->name == (*i).first);

        if (! obj->is_blob())
          obj->write();

        if (git_tree_add_entry2(&obj->tree_entry, *this, *obj,
                                obj->name.c_str(), obj->attributes) != 0)
          throw std::logic_error("Could not add entry to tree");
      }

      assert(check_size(*this));

      git_tree_sort_entries(*this);
      if (git_object_write(*this) != 0)
        throw std::logic_error("Could not write tree object");

      assert(check_size(*this));

      written  = true;
    }

    modified = false;
  }

  void Commit::update(const boost::filesystem::path& pathname, ObjectPtr obj)
  {
#ifdef DEBUG
    //std::cerr << "commit.update: " << pathname.string() << std::endl;
#endif
    if (! tree)
      tree = repository->create_tree();
    tree->update(pathname, obj);
  }

  void Commit::remove(const boost::filesystem::path& pathname)
  {
#ifdef DEBUG
    //std::cerr << "commit.remove: " << pathname.string() << std::endl;
#endif
    if (tree)
      tree->remove(pathname);
  }

  CommitPtr Commit::clone(bool with_copy)
  {
    CommitPtr new_commit = repository->create_commit();

    if (! git_object_id(*this))
      write();

    new_commit->tree   = with_copy ? new Tree(*tree) : tree;
    new_commit->prefix = prefix;
    new_commit->add_parent(this);

    return new_commit;
  }

  void Commit::write()
  {
    assert(git_object_id(*this) == NULL);

    assert(tree);

    TreePtr subtree;
    if (prefix.empty()) {
      subtree = tree;
    } else {
      ObjectPtr obj(tree->lookup(prefix));
      assert(obj->is_tree());
      subtree = dynamic_cast<Tree *>(obj.get());
    }
    if (! subtree)
      subtree = tree = repository->create_tree();

    assert(! subtree->empty());
    subtree->write();

    git_commit_set_tree(*this, *subtree);

    if (git_object_write(*this) != 0)
      throw std::logic_error("Could not write out Git commit");
  }

  void Branch::update(Repository& repository, CommitPtr _commit)
  {
    commit = _commit;
    assert(git_object_id(*commit));

    repository.create_file(boost::filesystem::path("refs") / "heads" / name,
                           commit->sha1());
  }

  TreePtr Repository::read_tree(git_tree * tree_obj, const std::string& name,
                                int attributes)
  {
    TreePtr tree(new Tree(this, tree_obj, name, attributes));

    std::size_t size = git_tree_entrycount(tree_obj);
    for (std::size_t i = 0; i < size; ++i) {
      git_tree_entry * entry(git_tree_entry_byindex(tree_obj, i));
      if (! entry)
        throw std::logic_error("Could not read Git tree entry");

      std::string  name(git_tree_entry_name(entry));
      int          attributes(git_tree_entry_attributes(entry));
      git_object * git_obj;
      if (git_tree_entry_2object(&git_obj, entry) != 0)
        throw std::logic_error("Could not read Git object");

      switch (git_object_type(git_obj)) {
      case GIT_OBJ_BLOB: {
        git_blob * blob_obj(reinterpret_cast<git_blob *>(git_obj));
        BlobPtr    blob(new Blob(this, blob_obj, name, attributes));
        blob->tree_entry = entry;
        tree->entries.insert(Tree::entries_pair(name, blob));
        break;
      }
      case GIT_OBJ_TREE: {
        git_tree * subtree_obj(reinterpret_cast<git_tree *>(git_obj));
        TreePtr    subtree(read_tree(subtree_obj, name, attributes));
        subtree->tree_entry = entry;
        tree->entries.insert(Tree::entries_pair(name, subtree));
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

  CommitPtr Repository::read_commit(const git_oid * oid)
  {
    git_commit * git_commit;

    int result = git_commit_lookup(&git_commit, *this, oid);
    if (result != 0)
      throw std::logic_error(git_strerror(result));

    // The commit prefix is no longer important at this stage, as its
    // only used to determining which subset of the commit's tree to use
    // when it's first written.

    const git_tree * commit_tree(git_commit_tree(git_commit));
    git_tree * tree;
    if (git_tree_lookup(&tree, *this,
                        git_tree_id(const_cast<git_tree *>(commit_tree))) != 0)
      throw std::logic_error("Could not find Git tree");

    CommitPtr commit(new Commit(this, git_commit));
    commit->tree = read_tree(tree);
    return commit;
  }

  void Repository::create_file(const boost::filesystem::path& pathname,
                               const std::string& content)
  {
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
}
