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

#ifndef _SVNDUMP_H
#define _SVNDUMP_H

#include <string>

#include <boost/optional.hpp>

#define BOOST_FILESYSTEM_VERSION 3
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/path.hpp>

namespace SvnDump
{
  class File : public boost::noncopyable
  {
    int curr_rev;
    int last_rev;

    std::string                  rev_author;
    std::time_t                  rev_date;
    boost::optional<std::string> rev_log;

    boost::filesystem::ifstream * handle;

  public:
    class Node : public boost::noncopyable
    {
    public:
      enum Kind {
        KIND_NONE,
        KIND_FILE,
        KIND_DIR
      };

      enum Action {
        ACTION_NONE,
        ACTION_ADD,
        ACTION_DELETE,
        ACTION_CHANGE,
        ACTION_REPLACE
      };

    private:
#define STATIC_BUFLEN 65536

      int                                      curr_txn;
      boost::filesystem::path                  pathname;
      Kind                                     kind;
      Action                                   action;
      char *                                   text;
      bool                                     text_allocated;
      char                                     static_buffer[STATIC_BUFLEN];
      int                                      text_len;
      boost::optional<std::string>             md5_checksum;
      boost::optional<std::string>             sha1_checksum;
      boost::optional<int>                     copy_from_rev;
      boost::optional<boost::filesystem::path> copy_from_path;

      friend class File;

    public:
      Node() : curr_txn(-1), text(NULL), text_allocated(false) { reset(); }
      ~Node() { reset(); }

      void reset() {
        kind     = KIND_NONE;
        action   = ACTION_NONE;

        pathname.clear();

        if (text_allocated) {
          delete[] text;
          text_allocated = false;
        }
        text = NULL;

        md5_checksum   = boost::none;
        sha1_checksum  = boost::none;
        copy_from_rev  = boost::none;
        copy_from_path = boost::none;
      }

      int get_txn_nr() const {
        return curr_txn;
      }
      Action get_action() const {
        return action;
      }
      Kind get_kind() const {
        return kind;
      }
      boost::filesystem::path get_path() const {
        return pathname;
      }
      bool has_copy_from() const {
        return copy_from_rev;
      }
      boost::filesystem::path get_copy_from_path() const {
        return *copy_from_path;
      }
      int get_copy_from_rev() const {
        return *copy_from_rev;
      }
      bool has_text() const {
        return text != NULL;
      }
      const char * get_text() const {
        return text;
      }
      std::size_t get_text_length() const {
        return text_len;
      }
      bool has_md5() const {
        return md5_checksum;
      }
      std::string get_text_md5() const {
        return *md5_checksum;
      }
      bool has_sha1() const {
        return sha1_checksum;
      }
      std::string get_text_sha1() const {
        return *sha1_checksum;
      }
    };

  private:
    Node curr_node;

  public:
    File() : curr_rev(-1), handle(NULL) {}
    File(const boost::filesystem::path& file) : curr_rev(-1), handle(NULL) {
      open(file);
    }
    ~File() {
      if (handle)
        close();
    }

    void open(const boost::filesystem::path& file) {
      if (handle)
        close();

      handle = new boost::filesystem::ifstream(file);

      // Buffer up to 1 megabyte when reading the dump file; this is a
      // nearly free 3% speed gain
      static char read_buffer[1024 * 1024];
      handle->rdbuf()->pubsetbuf(read_buffer, 1024 * 1024);
    }
    void rewind() {
      handle->seekg(0, std::ios::beg);
      curr_node.reset();
      curr_node.curr_txn = -1;
      last_rev = curr_rev = -1;
    }
    void close() {
      delete handle;
      handle = NULL;
    }

    int get_rev_nr() const {
      return curr_rev;
    }
    int get_last_rev_nr() const {
      return last_rev;
    }
    const Node& get_curr_node() const {
      return curr_node;
    }
    std::string get_rev_author() const {
      return rev_author;
    }
    std::time_t get_rev_date() const {
      return rev_date;
    }
    boost::optional<std::string> get_rev_log() const {
      return rev_log;
    }

    bool read_next(const bool ignore_text = false,
                   const bool verify      = false);

  private:
    void read_tags();
  };
}

#endif // _SVNDUMP_H
