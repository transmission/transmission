/*
 * This file Copyright (C) 2013-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "crypto-utils.h" // tr_base64_decode_str()
#include "error.h"
#include "file.h" // tr_sys_file_*()
#include "quark.h"
#include "platform.h" // TR_PATH_DELIMITER
#include "trevent.h" // tr_amInEventThread()
#include "torrent.h"
#include "variant.h"

#include <chrono>
#include <cstring> // strlen()
#include <memory>
#include <thread>
#include <mutex> // std::once_flag()
#include <string>
#include <cstdlib> // getenv()

#include "gtest/gtest.h"

namespace libtransmission
{

namespace test
{

auto const makeString = [](char*&& s)
    {
        auto const ret = std::string(s != nullptr ? s : "");
        tr_free(s);
        return ret;
    };

bool waitFor(std::function<bool()> const& test, int msec)
{
    auto const deadline = std::chrono::milliseconds { msec };
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
    Sandbox() :
        parent_dir_{get_default_parent_dir()},
        sandbox_dir_{create_sandbox(parent_dir_, "transmission-test-XXXXXX")}
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
        auto* path = getenv("TMPDIR");
        if (path != NULL)
        {
            return path;
        }

        tr_error* error = nullptr;
        path = tr_sys_dir_get_current(&error);
        if (path != nullptr)
        {
            std::string const ret = path;
            tr_free(path);
            return ret;
        }

        std::cerr << "tr_sys_dir_get_current error: '" << error->message << "'" << std::endl;
        tr_error_free(error);
        return {};
    }

    static std::string create_sandbox(std::string const& parent_dir, std::string const& tmpl)
    {
        std::string path = makeString(tr_buildPath(parent_dir.data(), tmpl.data(), nullptr));
        tr_sys_dir_create_temp(&path.front(), nullptr);
        tr_sys_path_native_separators(&path.front());
        return path;
    }

    static auto get_folder_files(std::string const& path)
    {
        std::vector<std::string> ret;

        tr_sys_path_info info;
        if (tr_sys_path_get_info(path.data(), 0, &info, nullptr) &&
            (info.type == TR_SYS_PATH_IS_DIRECTORY))
        {
            auto const odir = tr_sys_dir_open(path.data(), nullptr);
            if (odir != TR_BAD_SYS_DIR)
            {
                char const* name;
                while ((name = tr_sys_dir_read_name(odir, nullptr)) != nullptr)
                {
                    if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
                    {
                        ret.push_back(makeString(tr_buildPath(path.data(), name, nullptr)));
                    }
                }

                tr_sys_dir_close(odir, nullptr);
            }
        }

        return ret;
    }

    static void rimraf(std::string const& path, bool verbose = false)
    {
        for (auto const& child : get_folder_files(path))
        {
            rimraf(child, verbose);
        }

        if (verbose)
        {
            std::cerr << "cleanup: removing '" << path << "'" << std::endl;
        }

        tr_sys_path_remove(path.data(), nullptr);
    }

private:
    std::string const parent_dir_;
    std::string const sandbox_dir_;
};

class SandboxedTest : public ::testing::Test
{
protected:
    std::string sandboxDir() const { return sandbox_.path(); }

    auto currentTestName() const
    {
        auto const* i = ::testing::UnitTest::GetInstance()->current_test_info();
        auto child = std::string(i->test_suite_name());
        child += '_';
        child += i->name();
        return child;
    }

    void buildParentDir(std::string const& path) const
    {
        auto const tmperr = errno;

        auto const dir = makeString(tr_sys_path_dirname(path.c_str(), nullptr));
        tr_error* error = nullptr;
        tr_sys_dir_create(dir.data(), TR_SYS_DIR_CREATE_PARENTS, 0700, &error);
        EXPECT_EQ(nullptr, error) << "path[" << path << "] dir[" << dir << "] " << error->code << ", " << error->message;

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

    void createTmpfileWithContents(std::string& tmpl, void const* payload, size_t n) const
    {
        auto const tmperr = errno;

        buildParentDir(tmpl);

        // NOLINTNEXTLINE(clang-analyzer-cplusplus.InnerPointer)
        auto const fd = tr_sys_file_open_temp(&tmpl.front(), nullptr);
        blockingFileWrite(fd, payload, n);
        tr_sys_file_close(fd, nullptr);
        sync();

        errno = tmperr;
    }

    void createFileWithContents(std::string const& path, void const* payload, size_t n) const
    {
        auto const tmperr = errno;

        buildParentDir(path);

        auto const fd = tr_sys_file_open(path.c_str(),
            TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
            0600, nullptr);
        blockingFileWrite(fd, payload, n);
        tr_sys_file_close(fd, nullptr);
        sync();

        errno = tmperr;
    }

    void createFileWithContents(std::string const& path, void const* payload) const
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

void ensureFormattersInited()
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

    std::call_once(flag, []()
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
        size_t len;
        char const* str;
        auto q = TR_KEY_download_dir;
        auto const download_dir = tr_variantDictFindStr(settings, q, &str, &len) ?
            makeString(tr_strdup_printf("%s/%*.*s", sandboxDir().data(), (int)len, (int)len, str)) :
            makeString(tr_buildPath(sandboxDir().data(), "Downloads", nullptr));
        tr_sys_dir_create(download_dir.data(), TR_SYS_DIR_CREATE_PARENTS, 0700, nullptr);
        tr_variantDictAddStr(settings, q, download_dir.data());

        // incomplete dir
        q = TR_KEY_incomplete_dir;
        auto const incomplete_dir = tr_variantDictFindStr(settings, q, &str, &len) ?
            makeString(tr_strdup_printf("%s/%*.*s", sandboxDir().data(), (int)len, (int)len, str)) :
            makeString(tr_buildPath(sandboxDir().data(), "Incomplete", nullptr));
        tr_variantDictAddStr(settings, q, incomplete_dir.data());

        // blocklists
        auto const blocklist_dir = makeString(tr_buildPath(sandboxDir().data(), "blocklists", nullptr));
        tr_sys_dir_create(blocklist_dir.data(), TR_SYS_DIR_CREATE_PARENTS, 0700, nullptr);

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
    tr_torrent* zeroTorrentInit() const
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
        auto metainfo_len = size_t{};
        auto* metainfo = tr_base64_decode_str(metainfo_base64, &metainfo_len);
        EXPECT_NE(nullptr, metainfo);
        EXPECT_LT(size_t{ 0 }, metainfo_len);
        auto* ctor = tr_ctorNew(session_);
        tr_ctorSetMetainfo(ctor, reinterpret_cast<uint8_t*>(metainfo), metainfo_len);
        tr_ctorSetPaused(ctor, TR_FORCE, true);
        tr_free(metainfo);

        // create the torrent
        auto err = int{};
        auto* tor = tr_torrentNew(ctor, &err, nullptr);
        EXPECT_EQ(0, err);

        // cleanup
        tr_ctorFree(ctor);
        return tor;
    }

    void zeroTorrentPopulate(tr_torrent* tor, bool complete)
    {
        for (tr_file_index_t i = 0; i < tor->info.fileCount; ++i)
        {
            auto const& file = tor->info.files[i];

            auto path = (!complete && i == 0) ?
                makeString(tr_strdup_printf("%s%c%s.part", tor->currentDir, TR_PATH_DELIMITER, file.name)) :
                makeString(tr_strdup_printf("%s%c%s", tor->currentDir, TR_PATH_DELIMITER, file.name));

            auto const dirname = makeString(tr_sys_path_dirname(path.c_str(), nullptr));
            tr_sys_dir_create(dirname.data(), TR_SYS_DIR_CREATE_PARENTS, 0700, nullptr);
            auto fd = tr_sys_file_open(
                path.c_str(), TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600, nullptr);

            for (uint64_t j = 0; j < file.length; ++j)
            {
                tr_sys_file_write(fd, (!complete && i == 0 && j < tor->info.pieceSize) ? "\1" : "\0", 1, nullptr, nullptr);
            }

            tr_sys_file_close(fd, nullptr);

            path = makeString(tr_torrentFindFile(tor, i));
            auto const err = errno;
            EXPECT_TRUE(tr_sys_path_exists(path.c_str(), nullptr));
            errno = err;
        }

        sync();
        blockingTorrentVerify(tor);

        if (complete)
        {
            EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);
        }
        else
        {
            EXPECT_EQ(tor->info.pieceSize, tr_torrentStat(tor)->leftUntilDone);
        }
    }

    void blockingTorrentVerify(tr_torrent* tor)
    {
        EXPECT_NE(nullptr, tor->session);
        EXPECT_FALSE(tr_amInEventThread(tor->session));

        auto constexpr onVerifyDone = [] (tr_torrent*, bool, void* done) noexcept
        {
            *static_cast<bool*>(done) = true;
        };

        bool done = false;
        tr_torrentVerify(tor, onVerifyDone, &done);
        auto test = [&done]() { return done; };
        EXPECT_TRUE(waitFor(test, 2000));
    }

    tr_session* session_ = nullptr;

    tr_variant* settings()
    {
        if (!settings_)
        {
            auto* settings = new tr_variant {};
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
        session_ = sessionInit(settings());
        SandboxedTest::SetUp();
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
