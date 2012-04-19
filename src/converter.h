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

  typedef std::map<filesystem::path, Git::BranchPtr> branches_map;
  typedef branches_map::value_type                   branches_value;

  typedef std::pair<int, int>        copy_from_value;
  typedef std::list<copy_from_value> copy_from_list;

  const SvnDump::File&        dump;
  StatusDisplay&              status;
  Options                     opts;
  Authors                     authors;
  int                         rev;
  int                         last_rev;
  rev_trees_map               rev_trees;
  branches_map                branches;
  copy_from_list              copy_from;
  shared_ptr<Git::Repository> repository;
  Git::BranchPtr              history_branch;
  submodule_list_t            modules_list;

  ConvertRepository(const SvnDump::File&           _dump,
                    const filesystem::path& pathname,
                    StatusDisplay&                 _status,
                    const Options&                 _opts = Options())
    : dump(_dump), status(_status), opts(_opts), last_rev(-1),
      repository(new Git::Repository
                 (pathname, status,
                  bind(&ConvertRepository::set_commit_info, this, _1))),
      history_branch(new Git::Branch(repository.get(), "flat-history", true)) {}

  void           free_past_trees();
  Git::TreePtr   get_past_tree(const SvnDump::File::Node& node);
  Git::BranchPtr find_branch(const filesystem::path& pathname);

  void set_commit_info(Git::CommitPtr commit);

  void update_object(const filesystem::path& pathname,
                     Git::ObjectPtr obj = NULL,
                     Git::BranchPtr from_branch = NULL);

  bool add_file(const SvnDump::File::Node& node);
  bool add_directory(const SvnDump::File::Node& node);
  bool delete_item(const SvnDump::File::Node& node);

  void delete_branch(Git::BranchPtr branch);

  int  prescan(const SvnDump::File::Node& node);
  void operator()(const SvnDump::File::Node& node);

  void finish();
};

#endif // _CONVERTER_H
