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

#include "submodule.h"
#include "converter.h"

Submodule::Submodule(std::string _pathname, ConvertRepository& _parent)
  : pathname(_pathname), parent(_parent)
{
  filesystem::create_directories(pathname);

  // Initialize a Git repository for this submodule in the subdirectory
  std::system((std::string("git --git-dir=\"") +
               pathname + "/.git\" init").c_str());

  repository =
    new Git::Repository(filesystem::system_complete(pathname),
                        parent.status, function<void(Git::CommitPtr)>
                        (bind(&ConvertRepository::set_commit_info, &parent, _1)));
  repository->repo_name = pathname;

  // Copy all of the main repository's branches into the submodule,
  // which acts like a mirror for a targeted subset of that main
  // repository, we just won't know for which branches until the end.
  for (Git::Repository::branches_name_map::iterator
         i = parent.repository->branches_by_name.begin();
       i != parent.repository->branches_by_name.end();
       ++i) {
    Git::BranchPtr branch(new Git::Branch(repository));

    branch->name   = (*i).second->name;
    branch->prefix = (*i).second->prefix;

    repository->find_branch_by_name(branch->name, branch);
  }
}

int Submodule::load_modules(const filesystem::path& modules_file,
                            ConvertRepository& parent)
{
  int errors = 0;

  parent.submodules_map.clear();
  parent.submodules_list.clear();

  static const int MAX_LINE = 8192;
  char linebuf[MAX_LINE + 1];

  filesystem::ifstream in(modules_file);

  Submodule * curr_module = nullptr;

  while (in.good() && ! in.eof()) {
    in.getline(linebuf, MAX_LINE);

    if (linebuf[0] == '#') {
      continue;
    }
    else if (linebuf[0] == '[') {
      std::string module_name(linebuf, 1, std::strlen(linebuf) - 2);
      if (module_name != "<ignore>") {
        curr_module = new Submodule(module_name, parent);
        parent.submodules_list.push_back(curr_module);
      }
    }
    else if (const char * p = std::strchr(linebuf, ':')) {
      if (curr_module) {
        std::string source_path =
          std::string(linebuf, 0,
                      static_cast<std::string::size_type>(p - linebuf));

        ++p;
        while (std::isspace(*p))
          ++p;

        std::string target_path = std::string(p);
        if (target_path == ".")
          target_path = "";

        if (source_path != "<ignore>") {
          if (source_path[source_path.length() - 1] == '/')
            source_path = std::string(source_path, 0, source_path.length() - 1);

          if (target_path[target_path.length() - 1] == '/')
            target_path = std::string(target_path, 0, target_path.length() - 1);

          std::pair<ConvertRepository::submodules_map_t::iterator,
                    bool> result = parent.submodules_map.insert
            (ConvertRepository::submodules_map_t::value_type
             (source_path, std::make_pair(target_path, curr_module)));

          if (! result.second)
            std::cerr << "Failed to load from "
                      << modules_file.string() << ": "
                      << std::string("[") << curr_module->pathname << "]: "
                      << source_path << " -> " << target_path;
        }
      }
    }
  }
  return errors;
}
