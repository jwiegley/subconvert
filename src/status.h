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

#ifndef _STATUS_H
#define _STATUS_H

#include "gitutil.h"

struct Options
{
  bool verbose = false;
  bool quiet   = false;
  int  debug   = 0;
  int  collect = 0;
};

class StatusDisplay : public Git::Logger, public noncopyable
{
  mutable int  rev;
  int          final_rev;
  mutable bool need_newline;
  Options      opts;

public:
  std::ostream& out;
  std::string   verb;

  StatusDisplay(std::ostream&      _out,
                const Options&     _opts = Options(),
                const std::string& _verb = "Scanning")
    : rev(-1), need_newline(false), opts(_opts), out(_out),
      verb(_verb) {}

  virtual ~StatusDisplay() throw() {}

  void set_final_rev(int revision = -1) {
    final_rev = revision;
  }

  bool debug_mode() const {
    return opts.debug;
  }
  void debug(const std::string& message) const {
    if (debug_mode()) {
      newline();
      out << "r" << rev << ": " << message << std::endl;
      need_newline = false;
    }
  }
  void info(const std::string& message) const {
    if (opts.verbose || debug_mode()) {
      newline();
      out << "r" << rev << ": " << message << std::endl;
      need_newline = false;
    }
  }

  void newline() const {
    if (need_newline && ! opts.quiet) {
      out << std::endl;
      need_newline = false;
    }
  }

  void warn(const std::string& message) const {
    newline();
    out << "r" << rev << ": " << message << std::endl;
    need_newline = false;
  }
  void error(const std::string& message) const {
#if 0
    newline();
    out << "r" << rev << ": " << message << std::endl;
    need_newline = false;
#else
    throw std::runtime_error(message);
#endif
  }

  void update(const int next_rev = -1) const {
    if (opts.quiet) return;

    out << verb << ": ";
    if (next_rev != -1) {
      if (final_rev) {
        out << int((next_rev * 100) / final_rev) << '%'
            << " (" << next_rev << '/' << final_rev << ')';
      } else {
        out << next_rev;
      }
    } else {
      out << ", done.";
    }

    out << '\r';
    need_newline = true;
    rev = next_rev;
  }

  void finish() const {
    if (need_newline && ! opts.quiet) {
      out << ", done." << std::endl;
      need_newline = false;
    }
  }
};

#endif // _STATUS_H
