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

#include "branches.h"
#include "converter.h"
#include "status.h"

int Branches::load_branches(const filesystem::path& pathname,
                            ConvertRepository& converter,
                            StatusDisplay& status)
{
  int errors = 0;

  converter.branches.clear();

  static const int MAX_LINE = 8192;
  char linebuf[MAX_LINE + 1];

  filesystem::ifstream in(pathname);

  while (in.good() && ! in.eof()) {
    in.getline(linebuf, MAX_LINE);
    if (linebuf[0] == '#')
      continue;

    Git::BranchPtr branch(new Git::Branch(converter.repository));
    int            field  = 0;

    for (const char * p = std::strtok(linebuf, "\t");
         p != NULL;
         p = std::strtok(NULL, "\t")) {
      switch (field) {
      case 0:
        if (*p == 't')
          branch->is_tag = true;
        break;
      case 1: break;
      case 2: break;
      case 3: break;
      case 4:
        branch->prefix = p;
        break;
      case 5:
        branch->name = p;
        break;
      }
      field++;
    }

    if (branch->prefix.empty() || branch->name.empty())
      continue;

    if (! converter.repository->find_branch(branch->name, branch))
      ++errors;

    std::pair<ConvertRepository::branches_map::iterator, bool> result =
      converter.branches.insert
      (ConvertRepository::branches_value(branch->prefix, branch));
    if (! result.second) {
      status.warn(std::string("Branch prefix repeated: ") +
                  branch->prefix.string());
      ++errors;
    } else {
      for (filesystem::path dirname(branch->prefix.parent_path());
           ! dirname.empty();
           dirname = dirname.parent_path()) {
        ConvertRepository::branches_map::iterator i =
          converter.branches.find(dirname);
        if (i != converter.branches.end()) {
          status.warn(std::string("Parent of branch prefix ") +
                      branch->prefix.string() + " exists: " +
                      (*i).second->prefix.string());
          ++errors;
        }
      }

      for (ConvertRepository::branches_map::iterator
             i = converter.branches.begin();
           i != converter.branches.end();
           ++i) {
        if (branch != (*i).second &&
            branch->name == (*i).second->name) {
          status.warn(std::string("Branch name repeated: ") +
                      branch->prefix.string());
          ++errors;
        }
      }
    }
  }
  return errors;
}

void Branches::apply_action(int rev, std::time_t date,
                            const filesystem::path& pathname)
{
  optional<branches_map::iterator> branch;

  branches_map::iterator i = branches.find(pathname);
  if (i != branches.end()) {
    branch = i;
  } else {
    std::vector<filesystem::path> to_remove;

    for (branches_map::iterator j = branches.begin();
         j != branches.end();
         ++j)
      if (starts_with((*j).first.string(), pathname.string() + '/'))
        to_remove.push_back((*j).first);

    for (std::vector<filesystem::path>::iterator j = to_remove.begin();
         j != to_remove.end();
         ++j)
      branches.erase(*j);

    for (branches_map::iterator j = branches.begin();
         j != branches.end();
         ++j)
      if (starts_with(pathname.string(), (*j).first.string() + '/')) {
        branch = j;
        break;
      }

    if (! branch) {
      std::pair<branches_map::iterator, bool> result =
        branches.insert(branches_value(pathname, BranchInfo()));
      assert(result.second);
      branch = result.first;
    }
  }

  if ((**branch).second.last_rev != rev) {
    (**branch).second.last_rev  = rev;
    (**branch).second.last_date = date;
    ++(**branch).second.changes;
  }
}

void Branches::operator()(const SvnDump::File&       dump,
                          const SvnDump::File::Node& node)
{
  int rev = dump.get_rev_nr();
  if (rev != last_rev) {
    status.update(rev);
    last_rev = rev;
  }

  if (node.get_action() != SvnDump::File::Node::ACTION_DELETE &&
      (node.get_kind()  == SvnDump::File::Node::KIND_FILE ||
       node.has_copy_from()))
    apply_action(rev, dump.get_rev_date(),
                 node.get_kind() == SvnDump::File::Node::KIND_DIR ?
                 node.get_path() : node.get_path().parent_path());
}

void Branches::finish()
{
  status.finish();

  for (branches_map::const_iterator i = branches.begin();
       i != branches.end();
       ++i) {
    char buf[64];
    struct tm * then = std::gmtime(&(*i).second.last_date);
    std::strftime(buf, 63, "%Y-%m-%d", then);

    status.out << ((*i).second.changes == 1 ? "tag" : "branch") << '\t'
               << (*i).second.last_rev << '\t' << buf << '\t'
               << (*i).second.changes << '\t'
               << (*i).first.string() << '\t' << (*i).first.string()
               << '\n';
  }
}
