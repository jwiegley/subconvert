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

#ifndef _CONVERTER_H
#define _CONVERTER_H

#include "svndump.h"
#include "gitutil.h"
#include "status.h"
#include "authors.h"
#include "submodule.h"

struct ConvertRepository
{
  typedef std::map<int, Git::TreePtr> rev_trees_map;
  typedef rev_trees_map::value_type   rev_trees_value;

  typedef std::pair<int, int>        copy_from_value;
  typedef std::list<copy_from_value> copy_from_list;

  typedef std::vector<Submodule *>          submodules_list_t;
  typedef std::map<filesystem::path,
                   std::pair<filesystem::path,
                             Submodule *> > submodules_map_t;

  SvnDump::File::Node * node;
  StatusDisplay&        status;
  Options               opts;
  Authors               authors;
  int                   rev;
  int                   last_rev;
  rev_trees_map         rev_trees;
  copy_from_list        copy_from;
  Git::Repository *     repository; // let it leak!
  Git::BranchPtr        history_branch;
  submodules_list_t     submodules_list;
  submodules_map_t      submodules_map;
  git_signature *       signature;
  std::string           commit_log;

  ConvertRepository(const filesystem::path& pathname,
                    StatusDisplay&          _status,
                    const Options&          _opts = Options())
    : status(_status), opts(_opts), last_rev(-1),
      repository(new Git::Repository
                 (pathname, status,
                  bind(&ConvertRepository::set_commit_info, this, _1))),
      history_branch(new Git::Branch(repository, "flat-history", true)),
      signature(nullptr) {}

  ~ConvertRepository() {
#ifdef ASSERTS
    checked_delete(repository);
#endif
    if (signature)
      git_signature_free(signature);

    for (submodules_list_t::iterator i = submodules_list.begin();
         i != submodules_list.end();
         ++i)
      checked_delete(*i);
  }

  void         free_past_trees();
  Git::TreePtr get_past_tree();

  void establish_commit_info();
  void set_commit_info(Git::CommitPtr commit);

  std::pair<filesystem::path, Submodule *>
  find_submodule(const filesystem::path& pathname);

  Git::BranchPtr find_branch(Git::Repository *       repo,
                             const filesystem::path& pathname,
                             Git::BranchPtr          related_branch = nullptr);

  void update_object(Git::Repository *       repo,
                     const filesystem::path& pathname,
                     Git::ObjectPtr          obj            = nullptr,
                     Git::BranchPtr          from_branch    = nullptr,
                     Git::BranchPtr          related_branch = nullptr,
                     std::string             debug_text     = "");

  void process_change(Git::Repository *       repo,
                      const filesystem::path& pathname,
                      Git::BranchPtr          related_branch = nullptr);

  std::string describe_change(SvnDump::File::Node::Kind   kind,
                              SvnDump::File::Node::Action action);

  bool add_file(Git::Repository *       repo,
                const filesystem::path& pathname,
                Git::BranchPtr          related_branch = nullptr);

  bool add_directory(Git::Repository *       repo,
                     const filesystem::path& pathname,
                     Git::BranchPtr          related_branch = nullptr);

  bool delete_item(Git::Repository *       repo,
                   const filesystem::path& pathname,
                   Git::BranchPtr          related_branch = nullptr);

  int  prescan(SvnDump::File::Node& node);
  void operator()(SvnDump::File::Node& node);

  void finish();
};

#endif // _CONVERTER_H
