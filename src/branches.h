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

#ifndef _BRANCHES_H
#define _BRANCHES_H

#include "svndump.h"
#include "gitutil.h"

class StatusDisplay;
struct ConvertRepository;

struct Branches
{
  struct BranchInfo {
    int         last_rev;
    int         changes;
    std::time_t last_date;

    BranchInfo() : last_rev(0), changes(0), last_date(0) {}
  };

  typedef std::map<filesystem::path, BranchInfo> branches_map;
  typedef branches_map::value_type branches_value;

  branches_map   branches;      // only used for the "branches" command
  StatusDisplay& status;
  int            last_rev;

  Branches(StatusDisplay& _status) : status(_status), last_rev(-1) {}

  static int load_branches(const filesystem::path& pathname,
                           ConvertRepository& converter,
                           StatusDisplay& status);

  void apply_action(int rev, std::time_t date,
                    const filesystem::path& pathname);
  void operator()(const SvnDump::File&       dump,
                  const SvnDump::File::Node& node);
  void finish();
};

#endif // _BRANCHES_H
