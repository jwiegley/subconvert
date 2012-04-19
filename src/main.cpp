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

namespace {
  template <typename T>
  void invoke_scanner(SvnDump::File& dump) {
    StatusDisplay status(std::cerr);
    T finder(status);

    while (dump.read_next(/* ignore_text= */ true)) {
      status.set_final_rev(dump.get_last_rev_nr());
      finder(dump, dump.get_curr_node());
    }
    finder.finish();
  }

  struct comparator {
    bool operator()(const ConvertRepository::copy_from_value& left,
                    const ConvertRepository::copy_from_value& right) {
      return left.second < right.second;
    }
  };
}

int main(int argc, char *argv[])
{
  std::ios::sync_with_stdio(false);

  // Examine any option settings made by the user.  -f is the only
  // required one.

  Options opts;

  bool skip_preflight = false;
  bool verify         = false;
  int  start          = -1;
  int  cutoff         = -1;

  filesystem::path authors_file;
  filesystem::path branches_file;
  filesystem::path modules_file;

  std::vector<std::string> args;

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      if (argv[i][1] == '-') {
        if (std::strcmp(&argv[i][2], "verify") == 0)
          verify = true;
        else if (std::strcmp(&argv[i][2], "verbose") == 0)
          opts.verbose = true;
        else if (std::strcmp(&argv[i][2], "quiet") == 0)
          opts.quiet = true;
        else if (std::strcmp(&argv[i][2], "debug") == 0)
          opts.debug = 1;
        else if (std::strcmp(&argv[i][2], "skip") == 0)
          skip_preflight = 1;
        else if (std::strcmp(&argv[i][2], "start") == 0)
          start = std::atoi(argv[++i]);
        else if (std::strcmp(&argv[i][2], "cutoff") == 0)
          cutoff = std::atoi(argv[++i]);
        else if (std::strcmp(&argv[i][2], "authors") == 0)
          authors_file = argv[++i];
        else if (std::strcmp(&argv[i][2], "branches") == 0)
          branches_file = argv[++i];
        else if (std::strcmp(&argv[i][2], "modules") == 0)
          modules_file = argv[++i];
      }
      else if (std::strcmp(&argv[i][1], "v") == 0)
        opts.verbose = true;
      else if (std::strcmp(&argv[i][1], "q") == 0)
        opts.quiet = true;
      else if (std::strcmp(&argv[i][1], "d") == 0)
        opts.debug = 1;
      else if (std::strcmp(&argv[i][1], "A") == 0)
        authors_file = argv[++i];
      else if (std::strcmp(&argv[i][1], "B") == 0)
        branches_file = argv[++i];
      else if (std::strcmp(&argv[i][1], "M") == 0)
        modules_file = argv[++i];
    } else {
      args.push_back(argv[i]);
    }
  }

  // Any remaining arguments are the command verb and its particular
  // arguments.

  if (args.size() < 2) {
    std::cerr << "usage: subconvert [options] COMMAND DUMP-FILE"
              << std::endl;
    return 1;
  }

  std::string cmd(args[0]);

  if (cmd == "git-test") {
    StatusDisplay status(std::cerr);
    Git::Repository repo(args[1], status);

    std::cerr << "Creating initial commit..." << std::endl;
    Git::CommitPtr commit = repo.create_commit();

    struct tm then;
    strptime("2005-04-07T22:13:13", "%Y-%m-%dT%H:%M:%S", &then);

    std::cerr << "Adding blob to commit..." << std::endl;
    commit->update("foo/bar/baz.c",
                   repo.create_blob("baz.c", "#include <stdio.h>\n", 19));
    commit->update("foo/bar/bar.c",
                   repo.create_blob("bar.c", "#include <stdlib.h>\n", 20));
    commit->set_author("John Wiegley", "johnw@boostpro.com", std::mktime(&then));
    commit->set_message("This is a sample commit.\n");

    Git::Branch branch(&repo, "feature");
    std::cerr << "Updating feature branch..." << std::endl;
    branch.commit = commit;
    commit->write();
    branch.update();

    std::cerr << "Cloning commit..." << std::endl;
    commit = commit->clone();   // makes a new commit based on the old one
    std::cerr << "Removing file..." << std::endl;
    commit->remove("foo/bar/baz.c");
    strptime("2005-04-10T22:13:13", "%Y-%m-%dT%H:%M:%S", &then);
    commit->set_author("John Wiegley", "johnw@boostpro.com", std::mktime(&then));
    commit->set_message("This removes the previous file.\n");

    Git::Branch master(&repo, "master");
    std::cerr << "Updating master branch..." << std::endl;
    master.commit = commit;
    commit->write();
    master.update();

    return 0;
  }

  try {
    SvnDump::File dump(args[1]);

    if (cmd == "print") {
      SvnDump::FilePrinter printer(dump);
      while (dump.read_next(/* ignore_text= */ true))
        printer(dump.get_curr_node());
    }
    else if (cmd == "authors") {
      invoke_scanner<Authors>(dump);
    }
    else if (cmd == "branches") {
      invoke_scanner<Branches>(dump);
    }
    else if (cmd == "convert") {
      StatusDisplay status(std::cerr, opts);
      ConvertRepository converter
        (dump, args.size() == 2 ? filesystem::current_path() : args[2],
         status, opts);

      // Load any information provided by the user to assist with the
      // migration.
      int errors = 0;

      if (! authors_file.empty() &&
          filesystem::is_regular_file(authors_file))
        errors += converter.authors.load_authors(authors_file);

      if (! branches_file.empty() &&
          filesystem::is_regular_file(branches_file))
        errors += Branches::load_branches(branches_file, converter, status);

      if (! modules_file.empty() &&
          filesystem::is_regular_file(modules_file))
        errors += Submodule::load_modules(modules_file, converter,
                                          converter.modules_list);

#if defined(READ_EXISTING_GIT_REPOSITORY)
      errors += converter.load_revmap();
#endif // READ_EXISTING_GIT_REPOSITORY

      // Validate this information as much as possible before possibly
      // wasting the user's time with useless work.

      if (! skip_preflight) {
        status.verb = "Scanning";
        while (dump.read_next(/* ignore_text= */ false,
                              /* verify=      */ true)) {
          int final_rev = dump.get_last_rev_nr();
          if (cutoff != -1 && cutoff < final_rev)
            final_rev = cutoff;

          status.set_final_rev(final_rev);

          int rev = dump.get_rev_nr();
          if (cutoff != -1 && rev >= cutoff)
            break;
          if (start == -1 || rev >= start)
            errors += converter.prescan(dump.get_curr_node());
        }
        status.newline();

        converter.copy_from.sort(comparator());

        if (status.debug_mode()) {
          for (ConvertRepository::copy_from_list::iterator
                 i = converter.copy_from.begin();
               i != converter.copy_from.end();
               ++i) {
            std::ostringstream buf;
            buf << (*i).first << " <- " << (*i).second;
            status.info(buf.str());
          }
        }

        if (errors > 0) {
          status.warn("Please correct the errors listed above and run again.");
          return 1;
        }
        status.warn("Note: --skip can be used to skip this pre-scan.");

        dump.rewind();
      }

      // If everything passed the preflight, perform the conversion.
      status.verb = "Converting";
      while (dump.read_next(/* ignore_text= */ false)) {
        int final_rev = dump.get_last_rev_nr();
        if (cutoff != -1 && cutoff < final_rev)
          final_rev = cutoff;

        status.set_final_rev(final_rev);

        int rev = dump.get_rev_nr();
        if (cutoff != -1 && rev >= cutoff)
          break;
        if (start == -1 || rev >= start)
          converter(dump.get_curr_node());
      }
      converter.finish();
    }
    else if (cmd == "scan") {
      StatusDisplay status(std::cerr, opts);
      while (dump.read_next(/* ignore_text= */ !verify,
                            /* verify=      */ verify)) {
        status.set_final_rev(dump.get_last_rev_nr());
        if (opts.verbose)
          status.update(dump.get_rev_nr());
      }
      if (opts.verbose)
        status.finish();
    }
  }
  catch (const std::exception& err) {
    std::cerr << "Error: " << err.what() << std::endl;
    return 1;
  }

  return 0;
}
