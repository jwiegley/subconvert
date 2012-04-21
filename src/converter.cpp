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

#include "converter.h"

#ifndef ASSERTS
#undef assert
#define assert(x)
#endif

void ConvertRepository::free_past_trees()
{
  // jww (2012-04-20): We could also free branches here that we know
  // will never receive another commit.

  // We no longer need copy-from target revisions if we're passed it,
  // _and_ we're passed the revision that needed it.
  int popped = -1;
  while (! copy_from.empty() &&
         last_rev > copy_from.front().second &&
         last_rev > copy_from.front().first) {
    if (status.debug_mode()) {
      std::ostringstream buf;
      buf << "r" << copy_from.front().first
          << " no longer needs r" << copy_from.front().second;
      status.info(buf.str());
    }
    popped = copy_from.front().second;
    copy_from.pop_front();
  }

  if (popped >= 0) {
    if (status.debug_mode()) {
      std::ostringstream buf;
      buf << copy_from.size() << " tree reservations remain";
      status.info(buf.str());
    }

    if (status.debug_mode()) {
      std::ostringstream buf;
      buf << "rev_trees exist from r"
          << (*rev_trees.begin()).first
          << " to r"
          << (*rev_trees.rbegin()).first;
      status.info(buf.str());
    }

    // Remove all past trees except the oldest one we need to refer to.
    rev_trees_map::iterator i = rev_trees.upper_bound(popped);

    if (i != rev_trees.begin()) {
      --i;
      if (i != rev_trees.begin()) {
        if (status.debug_mode()) {
          std::ostringstream buf;
          buf << "Deleting rev_trees from r"
              << (*rev_trees.begin()).first
              << " to r" << (*i).first;
          status.info(buf.str());
        }
        rev_trees.erase(rev_trees.begin(), i);
      }
    }
  }
}

Git::TreePtr ConvertRepository::get_past_tree(const SvnDump::File::Node& node)
{
  rev_trees_map::const_iterator i =
    rev_trees.upper_bound(node.get_copy_from_rev());
  if (i == rev_trees.end()) {
    if (! rev_trees.empty())
      return (*rev_trees.rbegin()).second;
  }
  else if (i != rev_trees.begin()) {
    return (*--i).second;
  }

  std::ostringstream buf;
  buf << "Could not find tree for " << node.get_copy_from_path()
      << ", r" << node.get_copy_from_rev();
  status.error(buf.str());

  return NULL;
}

void ConvertRepository::set_commit_info(Git::CommitPtr commit)
{
  // Setup the author and commit comment
  std::string author_id(dump.get_rev_author());
  Authors::authors_map::iterator author = authors.authors.find(author_id);
  if (author != authors.authors.end())
    commit->set_author((*author).second.name, (*author).second.email,
                       dump.get_rev_date());
  else
    commit->set_author(author_id, "", dump.get_rev_date());

  optional<std::string> log(dump.get_rev_log());
  std::string::size_type beg = 0;
  std::string::size_type len = 0;
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
#if 0
  // jww (2012-04-18): We also need a "super repository" that contains a
  // .gitmodules file which is updated every time a submodule is updated.
  buf << "\nHistory-Commit: " << rev;
#endif

  commit->set_message(buf.str());
}

void ConvertRepository::applicable_branches(const filesystem::path& pathname,
                                            branches_mapping_t& branches)
{
  Git::BranchPtr branch = repository->find_branch_by_path(pathname);

  if (branch) {
    branches.push_back(std::make_pair(branch, pathname));

    for (submodule_list_t::iterator i = modules_list.begin();
         i != modules_list.end();
         ++i) {
      Submodule * submodule(*i);

      for (Submodule::module_map_t::iterator
             j = submodule->file_mappings.begin();
           j != submodule->file_mappings.end();
           ++j) {
        if (starts_with(pathname.string(), (*j).first)) {
          filesystem::path newpath;
          if (pathname.string() == (*j).first) {
            newpath = pathname;
          } else {
            newpath = filesystem::path((*j).second +
                                       std::string(pathname.string(),
                                                   (*j).first.length()));
          }
          branch = submodule->repository->find_branch_by_path(pathname);
          assert(branch);

          branches.push_back(std::make_pair(branch, newpath));
        }
      }
    }
  }
}

void ConvertRepository::update_object(const filesystem::path& pathname,
                                      Git::ObjectPtr          obj,
                                      Git::BranchPtr          from_branch)
{
  Git::BranchPtr branch        = repository->find_branch_by_path(pathname);
  Git::CommitPtr branch_commit = branch->get_commit(from_branch);

  // Even though we don't use it here, this call to branch->get_commit()
  // causes that branch's commit to be appended to the repository's
  // commit_queue.

  std::string::size_type path_len    = pathname.string().length();
  std::string::size_type subpath_len = branch->prefix.string().length();

  filesystem::path subpath(path_len == subpath_len ? pathname :
                           std::string(pathname.string(), subpath_len + 1));

  //status.debug(std::string("Updating sub-path ") + subpath.string());
  //status.debug(std::string("Updating historical path ") + pathname.string());

  Git::CommitPtr history_commit(history_branch->get_commit());
  if (obj) {
    branch_commit->update(subpath, obj);
    history_commit->update(pathname, obj);
  } else {
    branch_commit->remove(subpath);
    history_commit->remove(pathname);
  }
}

std::string
ConvertRepository::describe_change(SvnDump::File::Node::Kind   kind,
                                   SvnDump::File::Node::Action action)
{
  std::string desc;

  switch (action) {
  case SvnDump::File::Node::ACTION_NONE:
    desc = "NONE";
    break;
  case SvnDump::File::Node::ACTION_ADD:
    desc = "ADD";
    break;
  case SvnDump::File::Node::ACTION_DELETE:
    desc = "DELETE";
    break;
  case SvnDump::File::Node::ACTION_CHANGE:
    desc = "CHANGE";
    break;
  case SvnDump::File::Node::ACTION_REPLACE:
    desc = "REPLACE";
    break;
  }

  desc += " ";

  switch (kind) {
  case SvnDump::File::Node::KIND_NONE:
    desc += "NONE";
    break;
  case SvnDump::File::Node::KIND_FILE:
    desc += "FILE";
    break;
  case SvnDump::File::Node::KIND_DIR:
    desc += "DIR";
    break;
  }

  return desc;
}

bool ConvertRepository::add_file(const SvnDump::File::Node& node)
{
  filesystem::path pathname(node.get_path());

  status.debug(std::string("file.") +
               (node.get_action() == SvnDump::File::Node::ACTION_ADD ?
                "add" : "change") + ": " + pathname.string());

  Git::ObjectPtr obj;
  if (node.has_copy_from()) {
    filesystem::path from_path(node.get_copy_from_path());
    Git::TreePtr     past_tree(get_past_tree(node));

    obj = past_tree->lookup(from_path);
    if (! obj) {
      std::ostringstream buf;
      buf << "Could not find " << from_path << " in tree r"
          << node.get_copy_from_rev() << ":";
      status.warn(buf.str());

      past_tree->dump_tree(std::cerr);
    }

    assert(obj);
    assert(obj->is_blob());

    obj = obj->copy_to_name(pathname.filename().string());
    update_object(pathname, obj, repository->find_branch_by_path(from_path));

    return true;
  }
  else if (! (node.get_action() == SvnDump::File::Node::ACTION_CHANGE &&
              ! node.has_text())) {
    obj = repository->create_blob(pathname.filename().string(),
                                 node.has_text() ? node.get_text() : "",
                                 node.has_text() ? node.get_text_length() : 0);
    update_object(pathname, obj);

    return true;
  }

  return false;
}

bool ConvertRepository::add_directory(const SvnDump::File::Node& node)
{
  filesystem::path pathname(node.get_path());

  status.debug(std::string("dir.add: ") +
               node.get_copy_from_path().string() + " -> " +
               pathname.string());

  if (node.has_copy_from()) {
    filesystem::path from_path(node.get_copy_from_path());
    Git::TreePtr     past_tree(get_past_tree(node));
    Git::ObjectPtr   obj(past_tree->lookup(from_path));

    // `obj' could be NULL here, if the directory we're copying from had
    // no files in it.
    if (obj) {
      Git::BranchPtr from_branch(repository->find_branch_by_path(from_path));

      if (status.debug_mode()) {
        std::ostringstream buf;
        buf << "Starting branch from r" << node.get_copy_from_rev()
            << " in " << from_branch->name << " (prefix \""
            << from_branch->prefix.string() << "\")";
        status.debug(buf.str());
      }

      assert(obj->is_tree());
      obj = obj->copy_to_name(pathname.filename().string());
      update_object(pathname, obj, from_branch);
    }
    return true;
  }

  return false;
}

bool ConvertRepository::delete_item(const SvnDump::File::Node& node)
{
  filesystem::path pathname(node.get_path());

  status.debug(std::string("entry.delete: ") + pathname.string());

  update_object(pathname);

  return true;
}

int ConvertRepository::prescan(const SvnDump::File::Node& node)
{
  int errors = 0;

  status.update(dump.get_rev_nr());

  if (! authors.authors.empty()) {
    std::string author_id(dump.get_rev_author());
    Authors::authors_map::iterator author =
      authors.authors.find(author_id);
    if (author == authors.authors.end()) {
      std::ostringstream buf;
      buf << "Unrecognized author id: " << author_id;
      status.warn(buf.str());
      ++errors;
    }
  }

  if (node.has_copy_from()) {
    if (status.debug_mode()) {
      std::ostringstream buf;
      buf << "Copy from: " << dump.get_rev_nr()
          << " <- " << node.get_copy_from_rev();
      status.debug(buf.str());
    }

    if (copy_from.empty() ||
        ! (copy_from.back().first == dump.get_rev_nr() &&
           copy_from.back().second == node.get_copy_from_rev())) {
      copy_from.push_back(copy_from_value(dump.get_rev_nr(),
                                          node.get_copy_from_rev()));
    }
  }

  if (! repository->branches_by_path.empty()) {
    // Ignore pathname which only add or modify directories, but
    // do care about all entries which add or modify files, and
    // those which copy directories.
    if (node.get_action() == SvnDump::File::Node::ACTION_DELETE ||
        node.get_kind()   == SvnDump::File::Node::KIND_FILE ||
        node.has_copy_from()) {
      if (! repository->find_branch_by_path(node.get_path())) {
        std::ostringstream buf;
        buf << "Could not find branch for " << node.get_path()
            << " in r" << dump.get_rev_nr();
        status.warn(buf.str());
        ++errors;
      }

      if (node.has_copy_from() &&
          ! repository->find_branch_by_path(node.get_copy_from_path())) {
        std::ostringstream buf;
        buf << "Could not find branch for " << node.get_copy_from_path()
            << " in r" << dump.get_rev_nr();
        status.warn(buf.str());
        ++errors;
      }
    }
  }

  return errors;
}

void ConvertRepository::operator()(const SvnDump::File::Node& node)
{
  rev = dump.get_rev_nr();
  if (rev != last_rev) {
    // Commit any changes to the repository's index.  If there were no
    // Git-visible changes, this will be a no-op.
    if (repository->write(last_rev)) {
      // Record the state of the "historical tree", the one that mirrors
      // the entire state of the Subversion filesystem.  This is
      // necessary when we encounters revisions that copy data from
      // older states of the tree.
#ifdef ASSERTS
      std::pair<rev_trees_map::iterator, bool> result =
#endif
        rev_trees.insert(rev_trees_map::value_type
                         (last_rev, history_branch->commit->tree));
#ifdef ASSERTS
      assert(result.second);
#endif

      if (rev % 1000 == 0)
        repository->garbage_collect();
    }

    free_past_trees();

    status.update(rev);
    last_rev = rev;
  }

  bool changed = false;

  filesystem::path pathname(node.get_path());
  if (! pathname.empty()) {
    SvnDump::File::Node::Kind   kind   = node.get_kind();
    SvnDump::File::Node::Action action = node.get_action();


    if (kind == SvnDump::File::Node::KIND_FILE &&
        (action == SvnDump::File::Node::ACTION_ADD ||
         action == SvnDump::File::Node::ACTION_CHANGE)) {
      changed = add_file(node);
    }
    else if (action == SvnDump::File::Node::ACTION_DELETE) {
      changed = delete_item(node);
    }
    else if (node.has_copy_from() &&
             kind   == SvnDump::File::Node::KIND_DIR   &&
             action == SvnDump::File::Node::ACTION_ADD) {
      changed = add_directory(node);
    }

    if (! changed)
      status.debug(std::string("Change ignored: ") +
                   describe_change(kind, action));
  }
}

void ConvertRepository::finish()
{
  repository->write(last_rev);
  repository->write_branches();
  repository->garbage_collect();

  if (history_branch->commit) {
    repository->create_tag(history_branch->commit, history_branch->name);
    status.info(std::string("Wrote tag ") + history_branch->name);
  }

  status.finish();
}
