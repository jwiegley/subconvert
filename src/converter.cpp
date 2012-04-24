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

Git::TreePtr ConvertRepository::get_past_tree()
{
  rev_trees_map::const_iterator i =
    rev_trees.upper_bound(node->get_copy_from_rev());
  if (i == rev_trees.end()) {
    if (! rev_trees.empty())
      return (*rev_trees.rbegin()).second;
  }
  else if (i != rev_trees.begin()) {
    return (*--i).second;
  }

  std::ostringstream buf;
  buf << "Could not find tree for " << node->get_copy_from_path()
      << ", r" << node->get_copy_from_rev();
  status.error(buf.str());

  return nullptr;
}

void ConvertRepository::establish_commit_info()
{
  // Setup the author and commit comment
  std::string author_id(node->get_rev_author());
  if (author_id.empty())
    return;

  git_signature * sig;
  Authors::authors_map::iterator author = authors.authors.find(author_id);
  if (author != authors.authors.end()) {
    Git::git_check(git_signature_new(&sig, (*author).second.name.c_str(),
                                     (*author).second.email.c_str(),
                                     node->get_rev_date(), 0));
  } else {
    status.warn(std::string("Unrecognized author id: ") + author_id);
    Git::git_check(git_signature_new(&sig, author_id.c_str(),
                                     "unknown@unknown.org",
                                     node->get_rev_date(), 0));
  }
  signature = shared_ptr<git_signature>(sig, git_signature_free);

  optional<std::string> log(node->get_rev_log());
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

  commit_log = buf.str();
}

void ConvertRepository::set_commit_info(Git::CommitPtr commit)
{
  commit->signature = signature;
  commit->set_message(commit_log);
}

std::pair<filesystem::path, Submodule *>
ConvertRepository::find_submodule(const filesystem::path& pathname)
{
  submodules_map_t::iterator i = submodules_map.find(pathname);
  if (i != submodules_map.end())
    return (*i).second;

  for (filesystem::path dirname(pathname);
       ! dirname.empty();
       dirname = dirname.parent_path()) {
    submodules_map_t::iterator j = submodules_map.find(dirname);
    if (j != submodules_map.end()) {
      std::string suffix(pathname.string(), dirname.string().length() + 1);
      return std::make_pair((*j).second.first / filesystem::path(suffix),
                            (*j).second.second);
    }
  }

  return std::make_pair(filesystem::path(), nullptr);
}

Git::BranchPtr
ConvertRepository::find_branch(Git::Repository *       repo,
                               const filesystem::path& pathname,
                               Git::BranchPtr          related_branch)
{
  return (related_branch ?
          repo->find_branch_by_name(related_branch->name) :
          repo->find_branch_by_path(pathname));
}

void ConvertRepository::update_object(Git::Repository *       repo,
                                      const filesystem::path& pathname,
                                      Git::ObjectPtr          obj,
                                      Git::BranchPtr          from_branch,
                                      Git::BranchPtr          related_branch,
                                      std::string             debug_text)
{
  // First, add the change to the flat-history branch (which will become
  // a tag when the process is completed).

  // from_branch is never needed here, since 'obj' has already been
  // copied from the source location.
  Git::CommitPtr history_commit(history_branch->get_commit());
  if (obj)
    history_commit->update(pathname, obj);
  else
    history_commit->remove(pathname);

  //assert(history_commit->is_modified());

  // Second, add the change to the related branch, according to
  // branches.txt

  Git::BranchPtr branch(find_branch(repo, pathname, related_branch));
  Git::CommitPtr branch_commit(branch->get_commit(from_branch));

  status.info(debug_text + " <" + branch->name + ">" +
              (repo->repo_name.empty() ? "" :
               std::string(" {") + repo->repo_name + "}"));

  std::string::size_type path_len(pathname.string().length());
  std::string::size_type subpath_len(related_branch ? 0 :
                                     branch->prefix.string().length());
  filesystem::path       subpath(related_branch ? pathname :
                                 (path_len == subpath_len ?
                                  pathname : std::string(pathname.string(),
                                                         subpath_len + 1)));
  if (obj)
    branch_commit->update(subpath, obj);
  else
    branch_commit->remove(subpath);

  if (! submodules_map.empty() && ! related_branch) {
    // Add the change to any related submodule, according to
    // manifest.txt.  We actually add this to a branch in the current
    // repository for efficiency's sake, allowing it to be sifted out
    // using git-filter-branch afterward.
    Submodule *      submodule;
    filesystem::path submodule_path;

#if defined(_LIBCPP_VERSION)
    std::tie(submodule_path, submodule) = find_submodule(subpath);
#else
    std::tr1::tie(submodule_path, submodule) = find_submodule(subpath);
#endif

    if (submodule) {
      std::cerr << "  ==> matched to submodule " << submodule->pathname
                << " -> " << submodule_path << std::endl;
      process_change(submodule->repository, submodule_path, branch);
    }
  }
}

std::string
ConvertRepository::describe_change(SvnDump::File::Node::Kind   kind,
                                   SvnDump::File::Node::Action action)
{
  std::string desc;
  switch (action) {
  case SvnDump::File::Node::ACTION_NONE:    desc = "NONE";    break;
  case SvnDump::File::Node::ACTION_ADD:     desc = "ADD";     break;
  case SvnDump::File::Node::ACTION_DELETE:  desc = "DELETE";  break;
  case SvnDump::File::Node::ACTION_CHANGE:  desc = "CHANGE";  break;
  case SvnDump::File::Node::ACTION_REPLACE: desc = "REPLACE"; break;
  }

  desc += " ";
  switch (kind) {
  case SvnDump::File::Node::KIND_NONE: desc += "NONE"; break;
  case SvnDump::File::Node::KIND_FILE: desc += "FILE"; break;
  case SvnDump::File::Node::KIND_DIR:  desc += "DIR";  break;
  }

  return desc;
}

bool ConvertRepository::add_file(Git::Repository * repo,
                                 const filesystem::path& pathname,
                                 Git::BranchPtr related_branch)
{
  std::string debug_text;
  if (opts.verbose || opts.debug)
    debug_text = (std::string("F") +
                  (node->get_action() == SvnDump::File::Node::ACTION_ADD ?
                   "A" : "C") + ": " + pathname.string());

  Git::ObjectPtr obj;
  if (node->has_copy_from()) {
    filesystem::path from_path(node->get_copy_from_path());
    Git::TreePtr     past_tree(get_past_tree());

    obj = past_tree->lookup(from_path);
    if (! obj) {
      std::ostringstream buf;
      buf << "Could not find " << from_path << " in tree r"
          << node->get_copy_from_rev() << ":";
      status.warn(buf.str());

      past_tree->dump_tree(std::cerr);
    }

    assert(obj);
    assert(obj->is_blob());
    obj = obj->copy_to_name(pathname.filename().string(),
                            related_branch != nullptr);

    update_object(repo, pathname, obj,
                  find_branch(repo, from_path, related_branch),
                  related_branch, debug_text);
    return true;
  }
  else if (! (node->get_action() == SvnDump::File::Node::ACTION_CHANGE &&
              ! node->has_text())) {
    obj = repo->create_blob(pathname.filename().string(),
                            node->has_text() ? node->get_text() : "",
                            node->has_text() ? node->get_text_length() : 0);

    update_object(repo, pathname, obj, nullptr, related_branch, debug_text);
    return true;
  }
  return false;
}

bool ConvertRepository::add_directory(Git::Repository * repo,
                                      const filesystem::path& pathname,
                                      Git::BranchPtr related_branch)
{
  assert(node->has_copy_from());

  std::string debug_text;
  if (opts.verbose || opts.debug) {
    std::ostringstream buf;
    buf << "DA: " << node->get_copy_from_path().string()
        << " [r" << node->get_copy_from_rev()
        << "] -> " << pathname.string();
    debug_text = buf.str();
  }

  // `obj' could be nullptr here, if the directory we're copying from had
  // no files in it.
  filesystem::path from_path(node->get_copy_from_path());
  if (Git::ObjectPtr obj = get_past_tree()->lookup(from_path)) {
    assert(obj->is_tree());
    update_object(repo, pathname,
                  obj->copy_to_name(pathname.filename().string(),
                                    related_branch != nullptr),
                  find_branch(repo, from_path, related_branch),
                  related_branch, debug_text);
    return true;
  }
  return false;
}

bool ConvertRepository::delete_item(Git::Repository * repo,
                                    const filesystem::path& pathname,
                                    Git::BranchPtr related_branch)
{
  update_object(repo, pathname, nullptr, nullptr, related_branch,
                std::string("?D: ") + pathname.string());
  return true;
}

int ConvertRepository::prescan(SvnDump::File::Node& _node)
{
  node = &_node;

  int errors = 0;

  status.update(node->get_rev_nr());

  if (! authors.authors.empty()) {
    std::string author_id(node->get_rev_author());
    Authors::authors_map::iterator author =
      authors.authors.find(author_id);
    if (author == authors.authors.end()) {
      std::ostringstream buf;
      buf << "Unrecognized author id: " << author_id;
      status.warn(buf.str());
      ++errors;
    }
  }

  if (node->has_copy_from()) {
    if (status.debug_mode()) {
      std::ostringstream buf;
      buf << "Copy from: " << node->get_rev_nr()
          << " <- " << node->get_copy_from_rev();
      status.debug(buf.str());
    }

    if (copy_from.empty() ||
        ! (copy_from.back().first == node->get_rev_nr() &&
           copy_from.back().second == node->get_copy_from_rev())) {
      copy_from.push_back(copy_from_value(node->get_rev_nr(),
                                          node->get_copy_from_rev()));
    }
  }

  if (! repository->branches_by_path.empty()) {
    // Ignore pathname which only add or modify directories, but
    // do care about all entries which add or modify files, and
    // those which copy directories.
    if (node->get_action() == SvnDump::File::Node::ACTION_DELETE ||
        node->get_kind()   == SvnDump::File::Node::KIND_FILE ||
        node->has_copy_from()) {
      if (! repository->find_branch_by_path(node->get_path())) {
        std::ostringstream buf;
        buf << "Could not find branch for " << node->get_path()
            << " in r" << node->get_rev_nr();
        status.warn(buf.str());
        ++errors;
      }

      if (node->has_copy_from() &&
          ! repository->find_branch_by_path(node->get_copy_from_path())) {
        std::ostringstream buf;
        buf << "Could not find branch for " << node->get_copy_from_path()
            << " in r" << node->get_rev_nr();
        status.warn(buf.str());
        ++errors;
      }
    }
  }

  return errors;
}

void ConvertRepository::process_change(Git::Repository * repo,
                                       const filesystem::path& pathname,
                                       Git::BranchPtr related_branch)
{
  SvnDump::File::Node::Kind   kind   = node->get_kind();
  SvnDump::File::Node::Action action = node->get_action();

  bool changed = false;
  if (kind == SvnDump::File::Node::KIND_FILE &&
      (action == SvnDump::File::Node::ACTION_ADD ||
       action == SvnDump::File::Node::ACTION_CHANGE)) {
    changed = add_file(repo, pathname, related_branch);
  }
  else if (action == SvnDump::File::Node::ACTION_DELETE) {
    changed = delete_item(repo, pathname, related_branch);
  }
  else if (node->has_copy_from() &&
           kind   == SvnDump::File::Node::KIND_DIR   &&
           action == SvnDump::File::Node::ACTION_ADD) {
    changed = add_directory(repo, pathname, related_branch);
  }

  if (! changed)
    status.debug(std::string("Change ignored: ") +
                 describe_change(kind, action));
}

void ConvertRepository::operator()(SvnDump::File::Node& _node)
{
  node = &_node;

  const filesystem::path& pathname(node->get_path());
  if (! pathname.empty()) {
    rev = node->get_rev_nr();
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

        if (opts.collect && rev % opts.collect == 0) {
          repository->write_branches();
          repository->garbage_collect();
        }
      }

      for (submodule_list_t::iterator i = submodules_list.begin();
           i != submodules_list.end();
           ++i)
        if ((*i)->repository->write(last_rev)) {
          if (opts.collect && rev % opts.collect == 0) {
            (*i)->repository->write_branches();
            (*i)->repository->garbage_collect();
          }
        }

      free_past_trees();

      status.update(rev);
      last_rev = rev;

      establish_commit_info();
    }

    process_change(repository, pathname);
  }
}

void ConvertRepository::finish()
{
  repository->write(last_rev);
  repository->write_branches();

  for (submodule_list_t::iterator i = submodules_list.begin();
       i != submodules_list.end();
       ++i) {
    (*i)->repository->write(last_rev);
    (*i)->repository->write_branches();
  }

  if (opts.collect) {
    repository->garbage_collect();

    for (submodule_list_t::iterator i = submodules_list.begin();
         i != submodules_list.end();
         ++i)
      (*i)->repository->garbage_collect();
  }

  if (history_branch->commit) {
    repository->create_tag(history_branch->commit, history_branch->name);
    status.info(std::string("Wrote tag ") + history_branch->name);
  }

  status.finish();
}
