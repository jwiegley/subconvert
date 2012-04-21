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

#include "svndump.h"
#include "config.h"

#ifndef ASSERTS
#undef assert
#define assert(x)
#endif

namespace SvnDump {

bool File::read_next(const bool ignore_text, const bool verify)
{
  static const int MAX_LINE = 8192;

  char linebuf[MAX_LINE + 1];

  enum state_t {
    STATE_ERROR,
    STATE_TAGS,
    STATE_PROPS,
    STATE_BODY,
    STATE_NEXT
  } state = STATE_NEXT;

  int  prop_content_length = -1;
  int  text_content_length = -1;
  bool saw_node_path       = false;

  while (handle->good() && ! handle->eof()) {
    switch (state) {
    case STATE_NEXT:
      prop_content_length = -1;
      text_content_length = -1;
      saw_node_path       = false;

      curr_node.reset();

      if (handle->peek() == '\n')
        handle->read(linebuf, 1);
      state = STATE_TAGS;

      // fall through...

    case STATE_TAGS:
      handle->getline(linebuf, MAX_LINE);

      if (const char * p = std::strchr(linebuf, ':')) {
        std::string property
          (linebuf, 0, static_cast<std::string::size_type>(p - linebuf));
        switch (property[0]) {
        case 'C':
          break;

        case 'N':
          if (property == "Node-path") {
            curr_node.curr_txn += 1;
            curr_node.pathname = p + 2;
            saw_node_path = true;
          }
          else if (property == "Node-kind") {
            if (*(p + 2) == 'f')
              curr_node.kind = Node::KIND_FILE;
            else if (*(p + 2) == 'd')
              curr_node.kind = Node::KIND_DIR;
          }
          else if (property == "Node-action") {
            if (*(p + 2) == 'a')
              curr_node.action = Node::ACTION_ADD;
            else if (*(p + 2) == 'd')
              curr_node.action = Node::ACTION_DELETE;
            else if (*(p + 2) == 'c')
              curr_node.action = Node::ACTION_CHANGE;
            else if (*(p + 2) == 'r')
              curr_node.action = Node::ACTION_REPLACE;
          }
          else if (property == "Node-copyfrom-rev") {
            curr_node.copy_from_rev = std::atoi(p + 2);
          }
          else if (property == "Node-copyfrom-path") {
            curr_node.copy_from_path = p + 2;
          }
          break;

        case 'P':
          if (property == "Prop-content-length")
            prop_content_length = std::atoi(p + 2);
          break;

        case 'R':
          if (property == "Revision-number") {
            curr_rev  = std::atoi(p + 2);
            rev_log   = none;
            curr_node.curr_txn = -1;
          }
          break;

        case 'T':
          if (property == "Text-content-length")
            text_content_length = std::atoi(p + 2);
          else if (verify && property == "Text-content-md5")
            curr_node.md5_checksum = p + 2;
          else if (verify && property == "Text-content-sha1")
            curr_node.sha1_checksum = p + 2;
          break;
        }
      }
      else if (linebuf[0] == '\0') {
        if (prop_content_length > 0)
          state = STATE_PROPS;
        else if (text_content_length > 0)
          state = STATE_BODY;
        else if (saw_node_path)
          return true;
        else
          state = STATE_NEXT;
      }
      break;
        
    case STATE_PROPS: {
      assert(prop_content_length > 0);

      char *      buf;
      bool        allocated;
      char *      p;
      char *      q;
      int         len;
      bool        is_key;
      std::string property;

      if (curr_node.curr_txn >= 0) {
        // Ignore properties that don't describe the revision itself;
        // we just don't need to know for the purposes of this
        // utility.
        handle->seekg(prop_content_length, std::ios::cur);
        goto end_props;
      }

      if (prop_content_length < MAX_LINE) {
        handle->read(linebuf, prop_content_length);
        buf = linebuf;
        allocated = false;
      } else {
        buf = new char[prop_content_length + 1];
        allocated = true;
        handle->read(buf, prop_content_length);
      }

      p = buf;
      while (p - buf < prop_content_length) {
        is_key = *p == 'K';
        if (is_key || *p == 'V') {
          q = std::strchr(p, '\n');
          assert(q != NULL);
          *q = '\0';
          len = std::atoi(p + 2);
          p = q + 1;
          q = p + len;
          *q = '\0';

          if (is_key)
            property   = p;
          else if (property == "svn:date") {
            struct tm then;
            strptime(p, "%Y-%m-%dT%H:%M:%S", &then);
            rev_date   = timegm(&then);
          }
          else if (property == "svn:author")
            rev_author = p;
          else if (property == "svn:log")
            rev_log    = p;
          else if (property == "svn:sync-last-merged-rev")
            last_rev   = std::atoi(p);

          p = q + 1;
        } else {
          assert(p - buf == prop_content_length - 10);
          assert(std::strncmp(p, "PROPS-END\n", 10) == 0);
          break;
        }
      }

      if (allocated)
        delete[] buf;

    end_props:
      if (text_content_length > 0)
        state = STATE_BODY;
      else if (curr_rev == -1 || curr_node.curr_txn == -1)
        state = STATE_NEXT;
      else
        return true;
      break;
    }

    case STATE_BODY:
      if (ignore_text) {
        handle->seekg(text_content_length, std::ios::cur);
      } else {
        assert(! curr_node.has_text());
        assert(text_content_length > 0);

        if (text_content_length > STATIC_BUFLEN) {
          curr_node.text = new char[text_content_length];
          curr_node.text_allocated = true;
        } else {
          curr_node.text = curr_node.static_buffer;
        }
        curr_node.text_len = text_content_length;

        handle->read(curr_node.text, text_content_length);

#ifdef HAVE_LIBCRYPTO
        if (verify) {
          unsigned char id[20];
          char          checksum[41];
#ifdef HAVE_OPENSSL_MD5_H
          if (curr_node.has_md5()) {
            MD5(reinterpret_cast<const unsigned char *>(curr_node.get_text()),
                curr_node.get_text_length(), id);
            std::sprintf(checksum,
                         "%02x%02x%02x%02x%02x%02x%02x%02x"
                         "%02x%02x%02x%02x%02x%02x%02x%02x",
                         id[0],  id[1],  id[2],  id[3],
                         id[4],  id[5],  id[6],  id[7],
                         id[8],  id[9],  id[10], id[11],
                         id[12], id[13], id[14], id[15]);
            assert(curr_node.get_text_md5() == checksum);
          }
#endif // HAVE_OPENSSL_MD5_H
#ifdef HAVE_OPENSSL_SHA_H
          if (curr_node.has_sha1()) {
            SHA1(reinterpret_cast<const unsigned char *>(curr_node.get_text()),
                 curr_node.get_text_length(), id);
            std::sprintf(checksum,
                         "%02x%02x%02x%02x%02x%02x%02x%02x"
                         "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                         id[0],  id[1],  id[2],  id[3],
                         id[4],  id[5],  id[6],  id[7],
                         id[8],  id[9],  id[10], id[11],
                         id[12], id[13], id[14], id[15],
                         id[16], id[17], id[18], id[19]);
            assert(curr_node.get_text_sha1() == checksum);
          }
#endif // HAVE_OPENSSL_SHA_H
        }
#endif // HAVE_LIBCRYPTO
      }

      if (curr_rev == -1 || curr_node.curr_txn == -1)
        state = STATE_NEXT;
      else
        return true;

    case STATE_ERROR:
      assert(false);
      return false;
    }
  }
  return false;
}

void FilePrinter::operator()(const SvnDump::File::Node& node)
{
  { std::ostringstream buf;
    buf << 'r' << dump.get_rev_nr() << ':' << (node.get_txn_nr() + 1);
    std::cout.width(9);
    std::cout << std::right << buf.str() << ' ';
  }

  std::cout.width(8);
  std::cout << std::left;
  switch (node.get_action()) {
  case SvnDump::File::Node::ACTION_NONE:    std::cout << ' ';        break;
  case SvnDump::File::Node::ACTION_ADD:     std::cout << "add ";     break;
  case SvnDump::File::Node::ACTION_DELETE:  std::cout << "delete ";  break;
  case SvnDump::File::Node::ACTION_CHANGE:  std::cout << "change ";  break;
  case SvnDump::File::Node::ACTION_REPLACE: std::cout << "replace "; break;
  }

  std::cout.width(5);
  switch (node.get_kind()) {
  case SvnDump::File::Node::KIND_NONE: std::cout << ' ';     break;
  case SvnDump::File::Node::KIND_FILE: std::cout << "file "; break;
  case SvnDump::File::Node::KIND_DIR:  std::cout << "dir ";  break;
  }

  std::cout << node.get_path();

  if (node.has_copy_from())
    std::cout << " (copied from " << node.get_copy_from_path()
              << " [r" <<  node.get_copy_from_rev() << "])";

  std::cout << '\n';
}

} // namespace SvnDump
