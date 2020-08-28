/*
 * This file Copyright (C) 2013-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "tests/helpers/sandbox.h"

#include "libtransmission/crypto-utils.h" // tr_base64_decode_str()
#include "libtransmission/file.h" // tr_sys_file_*()
#include "libtransmission/platform.h" // TR_PATH_DELIMITER
#include "libtransmission/trevent.h" // tr_amInEventThread()
#include "libtransmission/torrent.h"
#include "libtransmission/variant.h"

#include <memory> // std::shared_ptr
#include <mutex> // std::call_once

#include "gtest/gtest.h"

namespace transmission
{

namespace tests
{

namespace helpers
{

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

} // namespace helpers

} // namespace tests

} // namespace transmission
