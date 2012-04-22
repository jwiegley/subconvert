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

#include "authors.h"
#include "status.h"

namespace {
  const char * unescape_string(const char * str)
  {
    static char buf[256];
    char * s = buf;
    for (const char * q = str; *q; ++q, ++s) {
      if (*q == '<' && *(q + 1) == '>') {
        *s = '@';
        ++q;
      }
      else if (*q == '~') {
        *s = '.';
      }
      else {
        *s = *q;
      }
    }
    *s = '\0';
    return buf;
  }
}

int Authors::load_authors(const filesystem::path& pathname)
{
  int errors = 0;

  authors.clear();

  static const int MAX_LINE = 8192;
  char linebuf[MAX_LINE + 1];

  filesystem::ifstream in(pathname);

  while (in.good() && ! in.eof()) {
    in.getline(linebuf, MAX_LINE);
    if (linebuf[0] == '#')
      continue;

    int         field = 0;
    std::string author_id;
    AuthorInfo  author;
    for (const char * p = std::strtok(linebuf, "\t"); p;
         p = std::strtok(nullptr, "\t")) {
      switch (field) {
      case 0:
        author_id = p;
        break;
      case 1:
        author.name = unescape_string(p);
        if (author.name == "Unknown")
          author.name = author_id;
        break;
      case 2:
        author.email = unescape_string(p);
        break;
      }
      field++;
    }

    std::pair<authors_map::iterator, bool> result =
      authors.insert(authors_value(author_id, author));
    if (! result.second) {
      status->warn(std::string("Author id repeated: ") + author_id);
      ++errors;
    }
  }
  return errors;
}

void Authors::operator()(const SvnDump::File& dump, const SvnDump::File::Node&)
{
  int rev = dump.get_rev_nr();
  if (rev != last_rev) {
    status->update(rev);
    last_rev = rev;

    std::string author = dump.get_rev_author();
    if (! author.empty()) {
      authors_map::iterator i = authors.find(author);
      if (i != authors.end())
        ++(*i).second.count;
      else
        authors.insert(authors_value(author, AuthorInfo(author)));
    }
  }
}

void Authors::finish()
{
  status->finish();

  for (authors_map::const_iterator i = authors.begin();
       i != authors.end();
       ++i)
    status->out << (*i).first << "\t\t\t" << (*i).second.count << '\n';
}
