/*
 * This file Copyright (C) 2013-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <memory>
#include <string>

#include "crypto-utils.h" // tr_base64_decode_str()
#include "error.h"
#include "file.h"
#include "quark.h"
#include "platform.h" // TR_PATH_DELIMITER
#include "trevent.h" // tr_amInEventThread()
#include "torrent.h"
#include "utils.h"
#include "variant.h"

#include "gtest/gtest.h"

namespace libtransmission::test
{
    struct UniqueCStrDeleter {
        void operator()(char* s) const { tr_free(s); };
    };
    
    auto constexpr unique_cstr_deleter = UniqueCStrDeleter {};
    
    auto constexpr make_unique_cstr = [](char* s){
      return std::unique_ptr<char, UniqueCStrDeleter>(s, unique_cstr_deleter);
    };

    auto constexpr make_string = [](char*&& s) {
      auto const ret = std::string(s);
      tr_free(s);
      return ret;
    };

}  // namespace libtransmission::test


class SandboxedTest : public ::testing::Test
{
private:
  char* tr_getcwd()
  {
    tr_error* error = nullptr;

    char* result = tr_sys_dir_get_current(&error);
    if (result == nullptr) {
      std::cerr << "getcwd error: '" << error->message << "'" << std::endl;
      tr_error_free(error);
      result = tr_strdup("");
    }

    return result;
  }

  char* create_sandbox()
  {
    char* path = tr_getcwd();
    char* sandbox = tr_buildPath(path, "sandbox-XXXXXX", nullptr);
    tr_free(path);
    tr_sys_dir_create_temp(sandbox, nullptr);
    return tr_sys_path_native_separators(sandbox);
  }

  void rm_rf(char const* killme)
  {
    tr_sys_path_info info;

    if (!tr_sys_path_get_info(killme, 0, &info, nullptr))
    {
      return;
    }

    tr_sys_dir_t const odir = info.type == TR_SYS_PATH_IS_DIRECTORY
      ? tr_sys_dir_open(killme, nullptr)
      : TR_BAD_SYS_DIR;

    if (odir != TR_BAD_SYS_DIR)
    {
      char const* name;

      while ((name = tr_sys_dir_read_name(odir, nullptr)) != nullptr)
      {
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
        {
          char* tmp = tr_buildPath(killme, name, nullptr);
          rm_rf(tmp);
          tr_free(tmp);
        }
      }

      tr_sys_dir_close(odir, nullptr);
    }

    if (verbose)
    {
      std::cerr << "cleanup: removing '" << killme << "'" << std::endl;
    }

    tr_sys_path_remove(killme, nullptr);
  }

protected:
  void build_parent_dir(char const* path)
  {
    char* dir;
    tr_error* error = nullptr;
    int const tmperr = errno;

    dir = tr_sys_path_dirname(path, nullptr);
    tr_sys_dir_create(dir, TR_SYS_DIR_CREATE_PARENTS, 0700, &error);
    EXPECT_EQ(nullptr, error);
    tr_free(dir);

    errno = tmperr;
  }

  void create_tmpfile_with_contents(char* tmpl, void const* payload, size_t n)
  {
      int const tmperr = errno;

      build_parent_dir(tmpl);

      auto fd = tr_sys_file_open_temp(tmpl, nullptr);

      uint64_t n_left = n;
      while (n_left > 0)
      {
          uint64_t n;

          tr_error* error = nullptr;
          if (!tr_sys_file_write(fd, payload, n_left, &n, &error))
          {
              fprintf(stderr, "Error writing '%s': %s\n", tmpl, error->message);
              tr_error_free(error);
              break;
          }

          n_left -= n;
      }

      tr_sys_file_close(fd, nullptr);

      sync();

      errno = tmperr;
  }

  bool verbose = false;
  char* sandbox_ = {};

  void sync()
  {
#ifndef _WIN32
    ::sync();
#endif
  }

  virtual void SetUp() override
  {
    sandbox_ = create_sandbox();
  }

  virtual void TearDown() override
  {
    rm_rf(sandbox_);
  }
};


class SessionTest : public SandboxedTest
{
  static constexpr int MEM_K = 1024;
  static char const constexpr* const MEM_K_STR = "KiB";
  static char const constexpr* const MEM_M_STR = "MiB";
  static char const constexpr* const MEM_G_STR = "GiB";
  static char const constexpr* const MEM_T_STR = "TiB";

  static constexpr int DISK_K = 1000;
  static char const constexpr* const DISK_K_STR = "kB";
  static char const constexpr* const DISK_M_STR = "MB";
  static char const constexpr* const DISK_G_STR = "GB";
  static char const constexpr* const DISK_T_STR = "TB";

  static constexpr int SPEED_K = 1000;
  static char const constexpr* const SPEED_K_STR = "kB/s";
  static char const constexpr* const SPEED_M_STR = "MB/s";
  static char const constexpr* const SPEED_G_STR = "GB/s";
  static char const constexpr* const SPEED_T_STR = "TB/s";

  tr_session* sessionInit(tr_variant* settings)
  {
    size_t len;
    char const* str;
    char* path;
    static bool formatters_inited = false;

    tr_variant local_settings;
    tr_variantInitDict(&local_settings, 10);

    if (settings == nullptr)
    {
        settings = &local_settings;
    }

    if (!formatters_inited)
    {
        formatters_inited = true;
        tr_formatter_mem_init(MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR);
        tr_formatter_size_init(DISK_K, DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR);
        tr_formatter_speed_init(SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR);
    }

    /* download dir */
    auto q = TR_KEY_download_dir;

    if (tr_variantDictFindStr(settings, q, &str, &len))
    {
        path = tr_strdup_printf("%s/%*.*s", sandbox_, (int)len, (int)len, str);
    }
    else
    {
        path = tr_buildPath(sandbox_, "Downloads", nullptr);
    }

    tr_sys_dir_create(path, TR_SYS_DIR_CREATE_PARENTS, 0700, nullptr);
    tr_variantDictAddStr(settings, q, path);
    tr_free(path);

    /* incomplete dir */
    q = TR_KEY_incomplete_dir;

    if (tr_variantDictFindStr(settings, q, &str, &len))
    {
        path = tr_strdup_printf("%s/%*.*s", sandbox_, (int)len, (int)len, str);
    }
    else
    {
        path = tr_buildPath(sandbox_, "Incomplete", nullptr);
    }

    tr_variantDictAddStr(settings, q, path);
    tr_free(path);

    path = tr_buildPath(sandbox_, "blocklists", nullptr);
    tr_sys_dir_create(path, TR_SYS_DIR_CREATE_PARENTS, 0700, nullptr);
    tr_free(path);

    q = TR_KEY_port_forwarding_enabled;

    if (tr_variantDictFind(settings, q) == nullptr)
    {
        tr_variantDictAddBool(settings, q, false);
    }

    q = TR_KEY_dht_enabled;

    if (tr_variantDictFind(settings, q) == nullptr)
    {
        tr_variantDictAddBool(settings, q, false);
    }

    q = TR_KEY_message_level;

    if (tr_variantDictFind(settings, q) == nullptr)
    {
        tr_variantDictAddInt(settings, q, verbose ? TR_LOG_DEBUG : TR_LOG_ERROR);
    }

    tr_session* session = tr_sessionInit(sandbox_, !verbose, settings);
    tr_variantFree(&local_settings);
    return session;
  }

  void sessionClose(tr_session* session)
  {
    tr_sessionClose(session);
    tr_logFreeQueue(tr_logGetQueue());
  }

protected:

  tr_torrent* zero_torrent_init() const
  {
      // 1048576 files-filled-with-zeroes/1048576
      //    4096 files-filled-with-zeroes/4096
      //     512 files-filled-with-zeroes/512
      char const* metainfo_base64 =
          "ZDg6YW5ub3VuY2UzMTpodHRwOi8vd3d3LmV4YW1wbGUuY29tL2Fubm91bmNlMTA6Y3JlYXRlZCBi"
          "eTI1OlRyYW5zbWlzc2lvbi8yLjYxICgxMzQwNykxMzpjcmVhdGlvbiBkYXRlaTEzNTg3MDQwNzVl"
          "ODplbmNvZGluZzU6VVRGLTg0OmluZm9kNTpmaWxlc2xkNjpsZW5ndGhpMTA0ODU3NmU0OnBhdGhs"
          "NzoxMDQ4NTc2ZWVkNjpsZW5ndGhpNDA5NmU0OnBhdGhsNDo0MDk2ZWVkNjpsZW5ndGhpNTEyZTQ6"
          "cGF0aGwzOjUxMmVlZTQ6bmFtZTI0OmZpbGVzLWZpbGxlZC13aXRoLXplcm9lczEyOnBpZWNlIGxl"
          "bmd0aGkzMjc2OGU2OnBpZWNlczY2MDpRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGExUv1726aj"
          "/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGExUv17"
          "26aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGEx"
          "Uv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJ"
          "tGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GI"
          "QxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZC"
          "S1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8K"
          "T9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9um"
          "o/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9"
          "e9umo/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRh"
          "MVL9e9umo/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMY"
          "SbRhMVL9e9umo/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLOlf5A+Tz30nMBVuNM2hpV3wg/103"
          "OnByaXZhdGVpMGVlZQ==";

      // create the torrent ctor
      auto metainfo_len = size_t {};
      auto* metainfo = tr_base64_decode_str(metainfo_base64, &metainfo_len);
      EXPECT_NE(nullptr, metainfo);
      EXPECT_LT(0, metainfo_len);
      auto* ctor = tr_ctorNew(session_);
      tr_ctorSetMetainfo(ctor, reinterpret_cast<uint8_t*>(metainfo), metainfo_len);
      tr_ctorSetPaused(ctor, TR_FORCE, true);

      // create the torrent
      auto err = int {};
      auto* tor = tr_torrentNew(ctor, &err, nullptr);
      EXPECT_EQ(0, err);

      // cleanup
      tr_ctorFree(ctor);
      return tor;
  }

  void zero_torrent_populate(tr_torrent* tor, bool complete)
  {
      using ::libtransmission::test::make_string;

      for (tr_file_index_t i = 0; i < tor->info.fileCount; ++i)
      {
          auto const& file = tor->info.files[i];

          auto path = (!complete && i == 0)
              ? make_string(tr_strdup_printf("%s%c%s.part", tor->currentDir, TR_PATH_DELIMITER, file.name))
              : make_string(tr_strdup_printf("%s%c%s", tor->currentDir, TR_PATH_DELIMITER, file.name));

          auto const dirname = make_string(tr_sys_path_dirname(std::data(path), nullptr));
          tr_sys_dir_create(std::data(dirname), TR_SYS_DIR_CREATE_PARENTS, 0700, nullptr);
          auto fd = tr_sys_file_open(std::data(path), TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600, nullptr);

          for (uint64_t j = 0; j < file.length; ++j)
          {
              tr_sys_file_write(fd, (!complete && i == 0 && j < tor->info.pieceSize) ? "\1" : "\0", 1, nullptr, nullptr);
          }

          tr_sys_file_close(fd, nullptr);

          path = make_string(tr_torrentFindFile(tor, i));
          auto const err = errno;
          EXPECT_TRUE(tr_sys_path_exists(std::data(path), nullptr));
          errno = err;
      }

      sync();
      blocking_torrent_verify(tor);

      if (complete)
      {
          EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);
      }
      else
      {
          EXPECT_EQ(tor->info.pieceSize, tr_torrentStat(tor)->leftUntilDone);
      }
  }

  void blocking_torrent_verify(tr_torrent* tor)
  {
      EXPECT_NE(nullptr, tor->session);
      EXPECT_FALSE(tr_amInEventThread(tor->session));

      auto constexpr onVerifyDone = [](tr_torrent*, bool, void* done) {
          *(bool*)done = true;
      };

      bool done = false;
      tr_torrentVerify(tor, onVerifyDone, &done);
      while (!done) {
          tr_wait_msec(10);
      }
  }

  void create_file_with_contents(char const* path, void const* payload, size_t n)
  {
    int const tmperr = errno;

    build_parent_dir(path);
    auto fd = tr_sys_file_open(path, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600, nullptr);
    tr_sys_file_write(fd, payload, n, nullptr, nullptr);
    tr_sys_file_close(fd, nullptr);
    sync();

    errno = tmperr;
  }

  void create_file_with_string_contents(char const* path, char const* str)
  {
    create_file_with_contents(path, str, strlen(str));
  }

  tr_session* session_ = nullptr;

  virtual void SetUp() override
  {
    SandboxedTest::SetUp();

    session_ = sessionInit(nullptr);
  }

  virtual void TearDown() override
  {
    sessionClose(session_);
    session_ = nullptr;

    SandboxedTest::TearDown();
  }
};

#if 0
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* mkstemp() */
#include <string.h> /* strcmp() */

#ifndef _WIN32
#include <unistd.h> /* sync() */
#endif

#include "transmission.h"
#include "crypto-utils.h"
#include "error.h"
#include "file.h"
#include "platform.h" /* TR_PATH_DELIMETER */
#include "torrent.h"
#include "tr-assert.h"
#include "trevent.h"
#include "variant.h"
#include "libtransmission-test.h"


/***
****
***/



/***
****
***/



/***
****
***/


void libttest_sync(void)
{
#ifndef _WIN32
    sync();
#endif
}
#endif
