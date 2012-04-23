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

int main(int argc, char *argv[])
{
  using namespace std;
  using namespace boost;
  namespace fs = filesystem;
  
  ios::sync_with_stdio(false);

  Options opts;
  opts.debug = true;
  opts.verbose = true;

  StatusDisplay   status(cerr, opts);
  Git::Repository repo(".", status);
  Git::Branch     snapshots(&repo, "snapshots");
  Git::CommitPtr  commit(new Git::Commit(&repo, nullptr));

  vector<string> refs;
  refs.push_back("HEAD");
  refs.push_back("refs/heads/snapshots");

  for (auto refname : refs) {
    git_reference * ref;
    if (GIT_SUCCESS == git_reference_lookup(&ref, repo, refname.c_str())) {
      if (const git_oid * oid = git_reference_oid(ref))
        commit->parent = new Git::Commit(&repo, oid);
      else
        commit->new_branch = true;
      git_reference_free(ref);
      break;
    }
  }

  time_t latest_write_time(0);

  while (true) {
    time_t      previous_write_time(latest_write_time);
    std::size_t updated = 0;

    for (fs::recursive_directory_iterator end, entry("./"); 
         entry != end;
         ++entry) {
      const fs::path& pathname(string((*entry).path().string(), 2));
      if (! fs::is_regular_file(pathname))
        continue;

      // jww (2012-04-23): Need to ignore the files referenced by
      // .gitignore, .git/info/exclude, and the global gitignore rules
      if (*pathname.begin() == ".git")
        continue;

      status.debug(std::string("Considering regular file ") +
                   pathname.string());

      time_t when = fs::last_write_time(pathname);
      if (when > previous_write_time) {
        status.info(std::string("Updating snapshot for ") + pathname.string());

        git_oid blob_oid;
        git_blob_create_fromfile(&blob_oid, repo, pathname.string().c_str());
        Git::BlobPtr blob
          (new Git::Blob(&repo, &blob_oid, pathname.stem().string(),
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

      snapshots.update(commit);   // update the snapshots branch

      repo.write_branches();      // write the updated refs to disk
    } else {
      status.debug("No changes noticed...");
    }

    status.debug("Sleeping for 1 second(s)...");
    sleep(1);
  }

  // jww (2012-04-23): There should be a safe way to quit
  return 0;
}
