// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <cstdlib> // getenv()
#include <cstring> // strlen()
#include <iostream>
#include <memory>
#include <mutex> // std::once_flag()
#include <string>
#include <string_view>
#include <thread>

#include "crypto-utils.h" // tr_base64_decode()
#include "error.h"
#include "file.h" // tr_sys_file_*()
#include "platform.h" // TR_PATH_DELIMITER
#include "quark.h"
#include "torrent.h"
#include "trevent.h" // tr_amInEventThread()
#include "utils.h"
#include "variant.h"

#include "gtest/gtest.h"

inline std::ostream& operator<<(std::ostream& os, tr_error const& err)
{
    os << err.message << ' ' << err.code;
    return os;
}

namespace libtransmission
{

namespace test
{

using file_func_t = std::function<void(char const* filename)>;

static void depthFirstWalk(char const* path, file_func_t func)
{
    auto info = tr_sys_path_info{};
    if (tr_sys_path_get_info(path, 0, &info) && (info.type == TR_SYS_PATH_IS_DIRECTORY))
    {
        if (auto const odir = tr_sys_dir_open(path); odir != TR_BAD_SYS_DIR)
        {
            char const* name;
            while ((name = tr_sys_dir_read_name(odir)) != nullptr)
            {
                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
                {
                    depthFirstWalk(tr_strvPath(path, name).c_str(), func);
                }
            }

            tr_sys_dir_close(odir);
        }
    }

    func(path);
}

inline std::string makeString(char*&& s)
{
    auto const ret = std::string(s != nullptr ? s : "");
    tr_free(s);
    return ret;
}

inline bool waitFor(std::function<bool()> const& test, int msec)
{
    auto const deadline = std::chrono::milliseconds{ msec };
    auto const begin = std::chrono::steady_clock::now();

    for (;;)
    {
        if (test())
        {
            return true;
        }

        if ((std::chrono::steady_clock::now() - begin) >= deadline)
        {
            return false;
        }

        tr_wait_msec(10);
    }
}

class Sandbox
{
public:
    Sandbox()
        : parent_dir_{ get_default_parent_dir() }
        , sandbox_dir_{ create_sandbox(parent_dir_, "transmission-test-XXXXXX") }
    {
    }

    ~Sandbox()
    {
        rimraf(sandbox_dir_);
    }

    std::string const& path() const
    {
        return sandbox_dir_;
    }

protected:
    static std::string get_default_parent_dir()
    {
        if (auto* const path = getenv("TMPDIR"); path != nullptr)
        {
            return path;
        }

        tr_error* error = nullptr;

        if (auto* path = tr_sys_dir_get_current(&error); path != nullptr)
        {
            auto ret = std::string{ path };
            tr_free(path);
            return ret;
        }

        std::cerr << "tr_sys_dir_get_current error: '" << error->message << "'" << std::endl;
        tr_error_free(error);
        return {};
    }

    static std::string create_sandbox(std::string const& parent_dir, std::string const& tmpl)
    {
        auto path = tr_strvPath(parent_dir, tmpl);
        tr_sys_dir_create_temp(std::data(path));
        tr_sys_path_native_separators(std::data(path));
        return path;
    }

    static void rimraf(std::string const& path, bool verbose = false)
    {
        auto remove = [verbose](char const* filename)
        {
            if (verbose)
            {
                std::cerr << "cleanup: removing '" << filename << "'" << std::endl;
            }

            tr_sys_path_remove(filename);
        };

        depthFirstWalk(path.c_str(), remove);
    }

private:
    std::string const parent_dir_;
    std::string const sandbox_dir_;
};

class SandboxedTest : public ::testing::Test
{
protected:
    std::string sandboxDir() const
    {
        return sandbox_.path();
    }

    auto currentTestName() const
    {
        auto const* i = ::testing::UnitTest::GetInstance()->current_test_info();
        auto child = std::string(i->test_suite_name());
        child += '_';
        child += i->name();
        return child;
    }

    void buildParentDir(std::string_view path) const
    {
        auto const tmperr = errno;

        auto dir = tr_pathbuf{ path };
        dir.popdir();
        tr_error* error = nullptr;
        tr_sys_dir_create(dir, TR_SYS_DIR_CREATE_PARENTS, 0700, &error);
        EXPECT_EQ(nullptr, error) << "path[" << path << "] dir[" << dir << "] " << *error;

        errno = tmperr;
    }

    static void blockingFileWrite(tr_sys_file_t fd, void const* data, size_t data_len)
    {
        uint64_t n_left = data_len;
        auto const* left = static_cast<uint8_t const*>(data);

        while (n_left > 0)
        {
            uint64_t n = {};
            tr_error* error = nullptr;
            if (!tr_sys_file_write(fd, left, n_left, &n, &error))
            {
                fprintf(stderr, "Error writing file: '%s'\n", error->message);
                tr_error_free(error);
                break;
            }

            left += n;
            n_left -= n;
        }
    }

    void createTmpfileWithContents(char* tmpl, void const* payload, size_t n) const
    {
        auto const tmperr = errno;

        buildParentDir(tmpl);

        // NOLINTNEXTLINE(clang-analyzer-cplusplus.InnerPointer)
        auto const fd = tr_sys_file_open_temp(tmpl);
        blockingFileWrite(fd, payload, n);
        tr_sys_file_close(fd);
        sync();

        errno = tmperr;
    }

    void createFileWithContents(std::string_view path, void const* payload, size_t n) const
    {
        auto const tmperr = errno;

        buildParentDir(path);

        auto const fd = tr_sys_file_open(
            tr_pathbuf{ path },
            TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
            0600,
            nullptr);
        blockingFileWrite(fd, payload, n);
        tr_sys_file_close(fd);
        sync();

        errno = tmperr;
    }

    void createFileWithContents(std::string_view path, std::string_view payload) const
    {
        createFileWithContents(path, std::data(payload), std::size(payload));
    }

    void createFileWithContents(std::string_view path, void const* payload) const
    {
        createFileWithContents(path, payload, strlen(static_cast<char const*>(payload)));
    }

    bool verbose = false;

    void sync() const
    {
#ifndef _WIN32
        ::sync();
#endif
    }

private:
    Sandbox sandbox_;
};

inline void ensureFormattersInited()
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

    static std::once_flag flag;

    std::call_once(
        flag,
        []()
        {
            tr_formatter_mem_init(MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR);
            tr_formatter_size_init(DISK_K, DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR);
            tr_formatter_speed_init(SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR);
        });
}

class SessionTest : public SandboxedTest
{
private:
    std::shared_ptr<tr_variant> settings_;

    tr_session* sessionInit(tr_variant* settings)
    {
        ensureFormattersInited();

        // download dir
        auto sv = std::string_view{};
        auto q = TR_KEY_download_dir;
        auto const download_dir = tr_variantDictFindStrView(settings, q, &sv) ? tr_strvPath(sandboxDir(), sv) :
                                                                                tr_strvPath(sandboxDir(), "Downloads");
        tr_sys_dir_create(download_dir, TR_SYS_DIR_CREATE_PARENTS, 0700);
        tr_variantDictAddStr(settings, q, download_dir.data());

        // incomplete dir
        q = TR_KEY_incomplete_dir;
        auto const incomplete_dir = tr_variantDictFindStrView(settings, q, &sv) ? tr_strvPath(sandboxDir(), sv) :
                                                                                  tr_strvPath(sandboxDir(), "Incomplete");
        tr_variantDictAddStr(settings, q, incomplete_dir.c_str());

        // blocklists
        tr_sys_dir_create(tr_pathbuf{ sandboxDir(), "/blocklists" }, TR_SYS_DIR_CREATE_PARENTS, 0700);

        // fill in any missing settings

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

        return tr_sessionInit(sandboxDir().data(), !verbose, settings);
    }

    void sessionClose(tr_session* session)
    {
        tr_sessionClose(session);
        tr_logFreeQueue(tr_logGetQueue());
    }

protected:
    enum class ZeroTorrentState
    {
        NoFiles,
        Partial,
        Complete
    };

    tr_torrent* zeroTorrentInit(ZeroTorrentState state) const
    {
        // 1048576 files-filled-with-zeroes/1048576
        //    4096 files-filled-with-zeroes/4096
        //     512 files-filled-with-zeroes/512
        char const* benc_base64 =
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
        auto const benc = tr_base64_decode(benc_base64);
        EXPECT_LT(0U, std::size(benc));
        auto* ctor = tr_ctorNew(session_);
        tr_error* error = nullptr;
        EXPECT_TRUE(tr_ctorSetMetainfo(ctor, std::data(benc), std::size(benc), &error));
        EXPECT_EQ(nullptr, error) << *error;
        tr_ctorSetPaused(ctor, TR_FORCE, true);

        // maybe create the files
        if (state != ZeroTorrentState::NoFiles)
        {
            auto const* const metainfo = tr_ctorGetMetainfo(ctor);
            for (tr_file_index_t i = 0, n = metainfo->fileCount(); i < n; ++i)
            {
                auto const base = state == ZeroTorrentState::Partial && tr_sessionIsIncompleteDirEnabled(session_) ?
                    tr_sessionGetIncompleteDir(session_) :
                    tr_sessionGetDownloadDir(session_);
                auto const& subpath = metainfo->fileSubpath(i);
                auto const partial = state == ZeroTorrentState::Partial && i == 0;
                auto const suffix = std::string_view{ partial ? ".part" : "" };
                auto const filename = tr_pathbuf{ base, '/', subpath, suffix };

                auto dirname = tr_pathbuf{ filename.sv() };
                dirname.popdir();
                tr_sys_dir_create(dirname, TR_SYS_DIR_CREATE_PARENTS, 0700);

                auto fd = tr_sys_file_open(filename, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600);
                auto const file_size = metainfo->fileSize(i);
                for (uint64_t j = 0; j < file_size; ++j)
                {
                    auto const ch = partial && j < metainfo->pieceSize() ? '\1' : '\0';
                    tr_sys_file_write(fd, &ch, 1, nullptr);
                }

                tr_sys_file_flush(fd);
                tr_sys_file_close(fd);
            }
        }

        // create the torrent
        auto* const tor = tr_torrentNew(ctor, nullptr);
        EXPECT_NE(nullptr, tor);
        waitForVerify(tor);

        // cleanup
        tr_ctorFree(ctor);
        return tor;
    }

    void waitForVerify(tr_torrent* tor) const
    {
        EXPECT_NE(nullptr, tor->session);
        tr_wait_msec(100);
        EXPECT_TRUE(waitFor(
            [tor]()
            {
                auto const activity = tr_torrentGetActivity(tor);
                return activity != TR_STATUS_CHECK && activity != TR_STATUS_CHECK_WAIT && tor->checked_pieces_.hasAll();
            },
            4000));
    }

    void blockingTorrentVerify(tr_torrent* tor) const
    {
        EXPECT_NE(nullptr, tor->session);
        EXPECT_FALSE(tr_amInEventThread(tor->session));
        tr_torrentVerify(tor);
        waitForVerify(tor);
    }

    tr_session* session_ = nullptr;

    tr_variant* settings()
    {
        if (!settings_)
        {
            auto* settings = new tr_variant{};
            tr_variantInitDict(settings, 10);
            auto constexpr deleter = [](tr_variant* v)
            {
                tr_variantFree(v);
                delete v;
            };
            settings_.reset(settings, deleter);
        }

        return settings_.get();
    }

    virtual void SetUp() override
    {
        SandboxedTest::SetUp();

        session_ = sessionInit(settings());
    }

    virtual void TearDown() override
    {
        sessionClose(session_);
        session_ = nullptr;
        settings_.reset();

        SandboxedTest::TearDown();
    }
};

} // namespace test

} // namespace libtransmission
