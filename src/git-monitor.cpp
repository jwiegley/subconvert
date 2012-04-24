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
#include "branches.h"

#include <fnmatch.h>

using namespace std;
using namespace boost;
namespace fs = filesystem;

namespace {
  void read_submodules_file(const fs::path& pathname, vector<string>& entries) {
    static const int MAX_LINE = 1024;
    char linebuf[MAX_LINE + 1];

    filesystem::ifstream in(pathname);

    while (in.good() && ! in.eof()) {
      in.getline(linebuf, MAX_LINE);
      if (linebuf[0] == '#')
        continue;

      if (starts_with(linebuf, "\tpath = "))
        entries.push_back(string(&linebuf[8]) + "/");
    }
  }

  bool is_ignored_file(const fs::path& pathname, const vector<string>& entries) {
    for (const string& entry : entries) {
      if (starts_with(pathname.string(), entry))
        return true;
    }
    return false;
  }
}

int main(int argc, char *argv[])
{
  ios::sync_with_stdio(false);

  size_t interval = 60;

  Options opts;
  vector<string> args;

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == '-') {
        if (strcmp(&argv[i][2], "verbose") == 0)
          opts.verbose = true;
        else if (strcmp(&argv[i][2], "quiet") == 0)
          opts.quiet = true;
        else if (strcmp(&argv[i][2], "debug") == 0)
          opts.debug = 1;
        else if (strcmp(&argv[i][2], "interval") == 0)
          interval = lexical_cast<size_t>(argv[++i]);
      }
      else if (strcmp(&argv[i][1], "v") == 0)
        opts.verbose = true;
      else if (strcmp(&argv[i][1], "q") == 0)
        opts.quiet = true;
      else if (strcmp(&argv[i][1], "d") == 0)
        opts.debug = 1;
      else if (strcmp(&argv[i][1], "i") == 0)
        interval = lexical_cast<size_t>(argv[++i]);
    } else {
      args.push_back(argv[i]);
    }
  }

  StatusDisplay   status(cerr, opts);
  Git::Repository repo(args.empty() ? "." : args[0].c_str(), status);

  git_reference * head;
  Git::git_check(git_reference_lookup(&head, repo, "HEAD"));

  string target(git_reference_target(head));
  if (starts_with(target, "refs/heads"))
    target = string("refs/snapshots/") + string(target, 11);
  else
    target = string("refs/snapshots/") + target;

  Git::Branch     snapshots(&repo, target);
  Git::CommitPtr  commit(new Git::Commit(&repo, nullptr));

  git_reference_free(head);

  vector<string> refs;
  refs.push_back(target);
  refs.push_back("HEAD");

  for (auto refname : refs) {
    git_reference * ref;
    if (GIT_SUCCESS == git_reference_lookup(&ref, repo, refname.c_str())) {
      git_reference * resolved_ref;
      Git::git_check(git_reference_resolve(&resolved_ref, ref));

      if (const git_oid * oid = git_reference_oid(resolved_ref))
        commit->parent = new Git::Commit(&repo, oid);
      else
        commit->new_branch = true;

      git_reference_free(resolved_ref);
      git_reference_free(ref);
      break;
    }
  }

  vector<string> ignore_list;
  time_t         ignore_mtime(0);

  time_t latest_write_time(0);

  while (true) {
    time_t      previous_write_time(latest_write_time);
    size_t updated = 0;

#define UPD_IGN_LIST(pathvar, listvar, timevar)                 \
    if (fs::is_regular_file(pathvar)) {                         \
      time_t timevar ## now(fs::last_write_time(pathvar));      \
      if (timevar ## now != timevar) {                          \
        (listvar).clear();                                      \
        read_submodules_file((pathvar), (listvar));             \
        timevar = timevar ## now;                               \
      }                                                         \
    }

    UPD_IGN_LIST(".gitmodules", ignore_list, ignore_mtime);

    for (fs::recursive_directory_iterator end, entry("./"); 
         entry != end;
         ++entry) {
      const fs::path& pathname(string((*entry).path().string(), 2));
      if (! fs::is_regular_file(pathname))
        continue;

      const string& path_str(pathname.string());

      if (*pathname.begin() == ".git" || contains(path_str, "/.git/"))
        continue;

      vector<fs::path> paths;
      paths.push_back(pathname);
      for (fs::path dirname(pathname);
           ! dirname.empty();
           dirname = dirname.parent_path())
        paths.push_back(dirname);

      int ignored = 0;
      for (const fs::path& subpath : paths) {
        Git::git_check
          (git_status_should_ignore(repo, subpath.string().c_str(), &ignored));
        if (ignored)
          break;

        if (is_ignored_file(subpath, ignore_list)) {
          ignored = 1;
          break;
        }
      }
      if (ignored) {
        status.debug(string("Ignoring ") + path_str);
        continue;
      }

      status.debug(string("Considering regular file ") + path_str);

      time_t when = fs::last_write_time(pathname);
      if (when > previous_write_time) {
        status.info(string("Updating snapshot for ") + path_str);

        git_oid blob_oid;
        git_blob_create_fromfile(&blob_oid, repo, path_str.c_str());
        Git::BlobPtr blob
          (new Git::Blob(&repo, &blob_oid, pathname.filename().string(),
                         0100000 + ((*entry).status().permissions() &
                                    fs::owner_exe ? 0755 : 0644)));

        if (latest_write_time && ! updated)
          commit = commit->clone();
        ++updated;

        commit->update(pathname, blob);

        if (when > latest_write_time)
          latest_write_time = when;
      }
    }

    if (updated) {
      ostringstream buf;
      buf << "Checkpointed " << updated << " files";
      commit->set_message(buf.str());
      commit->set_author("git-monitor", "git-monitor@localhost", latest_write_time);
      commit->write();            // create the commit object and its trees

      snapshots.update(commit, target); // update the snapshots ref

      repo.write_branches();      // write the updated refs to disk
    } else {
      status.debug("No changes noticed...");
    }

    if (status.debug_mode()) {
      ostringstream buf;
      buf << "Sleeping for " << interval << " second(s)...";
      status.debug(buf.str());
    }
    sleep(static_cast<unsigned int>(interval));
  }

  // jww (2012-04-23): There should be a safe way to quit.  Just send
  // SIGINT or SIGTERM for now.
  return 0;
}
