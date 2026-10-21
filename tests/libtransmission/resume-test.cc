// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> // size_t
#include <cstdint> // int64_t, uint32_t, uint64_t
#include <ctime> // time_t
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/quark.h>
#include <libtransmission/session.h>
#include <libtransmission/torrent-ctor.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission::test
{
namespace
{
auto constexpr PieceSize = uint32_t{ 32768U };
auto constexpr Sha1Len = size_t{ 20U };

[[nodiscard]] std::string bencStr(std::string_view const sv)
{
    return fmt::format("{:d}:{:s}", std::size(sv), sv);
}

[[nodiscard]] std::string subpath(size_t const i)
{
    return fmt::format("root/f{:d}", i);
}

// A torrent named "root" whose files are "root/f0", "root/f1", ... with the
// given sizes. Nothing here hashes the payload, so the piece hashes are filler.
[[nodiscard]] std::string makeTorrentBenc(std::vector<uint64_t> const& file_sizes)
{
    auto files = std::string{};
    auto total_size = uint64_t{};
    for (size_t i = 0, n = std::size(file_sizes); i < n; ++i)
    {
        files += fmt::format("d6:lengthi{:d}e4:pathl{:s}ee", file_sizes[i], bencStr(fmt::format("f{:d}", i)));
        total_size += file_sizes[i];
    }

    auto const n_pieces = (total_size + PieceSize - 1U) / PieceSize;
    auto const pieces = std::string(n_pieces * Sha1Len, 'A');

    return fmt::format(
        "d4:infod5:filesl{:s}e4:name4:root12:piece lengthi{:d}e6:pieces{:s}ee",
        files,
        PieceSize,
        bencStr(pieces));
}

[[nodiscard]] std::vector<std::string> canonicalSubpaths(size_t const n_files)
{
    auto ret = std::vector<std::string>{};
    ret.reserve(n_files);
    for (size_t i = 0; i < n_files; ++i)
    {
        ret.emplace_back(subpath(i));
    }
    return ret;
}

// The filename list saved by a Transmission that left zero-length files out of
// a torrent's file list.
[[nodiscard]] std::vector<std::string> legacySubpaths(std::vector<uint64_t> const& file_sizes)
{
    auto ret = std::vector<std::string>{};
    for (size_t i = 0, n = std::size(file_sizes); i < n; ++i)
    {
        if (file_sizes[i] != 0U)
        {
            ret.emplace_back(subpath(i));
        }
    }
    return ret;
}

[[nodiscard]] tr_variant::Map savedFilenames(std::vector<std::string> const& subpaths)
{
    auto list = tr_variant::Vector{};
    list.reserve(std::size(subpaths));
    for (auto const& subpath : subpaths)
    {
        list.emplace_back(subpath);
    }

    auto map = tr_variant::Map{ 1U };
    map.try_emplace(TR_KEY_files, std::move(list));
    return map;
}

[[nodiscard]] tr_variant::Vector dndList(std::vector<bool> const& dnd)
{
    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(dnd));
    for (auto const flag : dnd)
    {
        ret.emplace_back(flag);
    }
    return ret;
}

struct Layout
{
    std::string_view name;
    std::vector<uint64_t> file_sizes;
};

// The ways a torrent's zero-length files can sit among its other files.
[[nodiscard]] std::vector<Layout> layouts()
{
    return {
        { "zero-length file first"sv, { 0U, 1U, 1U, 1U } },
        { "zero-length file in the middle"sv, { 1U, 0U, 1U, 1U } },
        { "zero-length file last, so nothing shifts"sv, { 1U, 1U, 1U, 0U } },
        { "two adjacent zero-length files"sv, { 1U, 0U, 0U, 1U } },
        { "two zero-length files apart"sv, { 0U, 1U, 0U, 1U } },
    };
}

[[nodiscard]] tr_variant::Vector priorityList(std::vector<tr_priority_t> const& priorities)
{
    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(priorities));
    for (auto const priority : priorities)
    {
        ret.emplace_back(static_cast<int64_t>(priority));
    }
    return ret;
}

class ResumeTest : public SessionTest
{
protected:
    // Add a torrent with the given file sizes, having first written `resume`
    // as its .resume file.
    [[nodiscard]] tr_torrent* torrentInit(std::vector<uint64_t> const& file_sizes, tr_variant::Map&& resume) const
    {
        auto const benc = makeTorrentBenc(file_sizes);
        auto* const ctor = tr_ctorNew(session_);
        auto error = tr_error{};
        EXPECT_TRUE(tr_ctorSetMetainfo(ctor, std::data(benc), std::size(benc), &error));
        EXPECT_FALSE(error) << error;
        tr_ctorSetPaused(ctor, TR_FORCE, true);
        auto const* const metainfo = tr_ctorGetMetainfo(ctor);

        // Model an existing torrent with resume data. Save the metainfo first
        // so tr_torrentNew() does not start background verification.
        EXPECT_TRUE(ctor->save(metainfo->torrent_file(session_->torrentDir())));

        auto serde = tr_variant_serde::benc();
        auto const filename = metainfo->resume_file(session_->resumeDir());
        EXPECT_TRUE(serde.to_file(tr_variant{ std::move(resume) }, filename)) << serde.error_;

        auto* const tor = tr_torrentNew(ctor, nullptr);
        EXPECT_NE(nullptr, tor);
        tr_ctorFree(ctor);
        return tor;
    }

    static void expectSubpaths(tr_torrent const* tor, std::vector<std::string> const& expected)
    {
        ASSERT_EQ(std::size(expected), tor->file_count());
        for (tr_file_index_t i = 0, n = tor->file_count(); i < n; ++i)
        {
            EXPECT_EQ(expected[i], tor->file_subpath(i)) << " at file " << i;
        }
    }

    static void expectWantedAndPriorities(
        tr_torrent const* tor,
        std::vector<bool> const& wanted,
        std::vector<tr_priority_t> const& priorities)
    {
        ASSERT_EQ(std::size(wanted), tor->file_count());
        ASSERT_EQ(std::size(priorities), tor->file_count());
        for (tr_file_index_t i = 0, n = tor->file_count(); i < n; ++i)
        {
            auto const file = tr_torrentFile(tor, i);
            EXPECT_EQ(wanted[i], file.wanted) << " at file " << i;
            EXPECT_EQ(priorities[i], file.priority) << " at file " << i;
        }
    }

    // Write each file to the download dir and return its mtime, so that a
    // resume file can name the mtimes the files on disk actually have.
    [[nodiscard]] std::vector<time_t> createFiles(std::vector<uint64_t> const& file_sizes) const
    {
        auto ret = std::vector<time_t>{};
        ret.reserve(std::size(file_sizes));

        for (size_t i = 0, n = std::size(file_sizes); i < n; ++i)
        {
            auto const path = tr_pathbuf{ session_->downloadDir(), '/', subpath(i) };
            createFileWithContents(path, std::string(static_cast<size_t>(file_sizes[i]), 'x'));
            auto const info = tr_sys_path_get_info(path);
            EXPECT_TRUE(info) << path.c_str();
            ret.push_back(info ? info->last_modified_at : 0);
        }

        return ret;
    }
};
} // namespace

// A full-length list is the ordinary case: apply it verbatim. This is also
// what we save for a torrent that has zero-length files, so the zero-length
// files here must not send it down the realigning path below.
TEST_F(ResumeTest, savedFilenamesFullLengthList)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 0U, 1U, 1U };
    auto saved = canonicalSubpaths(std::size(file_sizes));
    saved[2] = "root/renamed";

    expectSubpaths(torrentInit(file_sizes, savedFilenames(saved)), saved);
}

// Transmission 4.0.x omitted zero-length files from a torrent's file list, so
// the list it saved is short by exactly those files and every pathname after
// an omitted one sits at a lower position than its file index. Applying such a
// list by position shifts the names onto the wrong files and leaves the last
// name assigned to two file indices.
TEST_F(ResumeTest, savedFilenamesListWrittenWithoutZeroLengthFiles)
{
    for (auto const& [name, file_sizes] : layouts())
    {
        SCOPED_TRACE(name);

        // Rename the list's last entry. It belongs to the last non-empty
        // file, which is the one a positional application of a short list
        // would leave duplicated onto an earlier file.
        auto saved = legacySubpaths(file_sizes);
        saved.back() = "root/renamed";

        auto expected = canonicalSubpaths(std::size(file_sizes));
        for (size_t i = std::size(file_sizes); i > 0U; --i)
        {
            if (file_sizes[i - 1U] != 0U)
            {
                expected[i - 1U] = "root/renamed";
                break;
            }
        }

        expectSubpaths(torrentInit(file_sizes, savedFilenames(saved)), expected);
    }
}

// Without zero-length files to account for, a short list has no reading that
// pairs it up with the files, so none of it is applied.
TEST_F(ResumeTest, savedFilenamesShortListWithoutZeroLengthFiles)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 1U, 1U, 1U };
    auto const saved = std::vector<std::string>{ "root/renamed", "root/also-renamed", "root/f2" };

    expectSubpaths(torrentInit(file_sizes, savedFilenames(saved)), canonicalSubpaths(std::size(file_sizes)));
}

// Nor is a list applied when it's short by more than the zero-length files: a
// partial mapping would point file indices at other files' data.
TEST_F(ResumeTest, savedFilenamesListOfUnusableLength)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 0U, 1U, 1U };
    auto const saved = std::vector<std::string>{ "root/renamed", "root/also-renamed" };

    expectSubpaths(torrentInit(file_sizes, savedFilenames(saved)), canonicalSubpaths(std::size(file_sizes)));
}

// A list longer than the file count is unusable too.
TEST_F(ResumeTest, savedFilenamesListLongerThanFileCount)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 0U, 1U };
    auto const saved = std::vector<std::string>{ "root/renamed", "root/f1", "root/f2", "root/f3" };

    expectSubpaths(torrentInit(file_sizes, savedFilenames(saved)), canonicalSubpaths(std::size(file_sizes)));
}

// A client that applied a legacy-length list by position saved the result back
// at full length, so the last pathname went to two files. Its length says
// nothing; the duplicate is what gives it away. None of the list is applied,
// not even the entries ahead of the duplicate: whatever produced one wrong
// pathname leaves no reason to trust the others.
TEST_F(ResumeTest, savedFilenamesFullLengthListWithDuplicate)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 0U, 1U, 1U };
    auto const saved = std::vector<std::string>{ "root/renamed", "root/f2", "root/f3", "root/f3" };

    expectSubpaths(torrentInit(file_sizes, savedFilenames(saved)), canonicalSubpaths(std::size(file_sizes)));
}

// A file with no entry of its own still holds the pathname its metainfo gave
// it, so an entry that names that pathname collides with it.
TEST_F(ResumeTest, savedFilenamesEntryCollidingWithUnlistedFile)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 0U, 1U, 1U };
    auto const saved = std::vector<std::string>{ "root/f1", "root/f2", "root/f3" };

    expectSubpaths(torrentInit(file_sizes, savedFilenames(saved)), canonicalSubpaths(std::size(file_sizes)));
}

// An entry that isn't a usable pathname leaves that one file alone. It still
// holds a position, so the entries after it keep their alignment.
TEST_F(ResumeTest, savedFilenamesUnusableEntry)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 0U, 1U, 1U };

    auto list = tr_variant::Vector{};
    list.emplace_back("root/renamed"); // -> f0
    list.emplace_back(int64_t{ 0 }); // not a pathname; f2 keeps its own
    list.emplace_back("root/also-renamed"); // -> f3
    auto map = tr_variant::Map{ 1U };
    map.try_emplace(TR_KEY_files, std::move(list));

    auto expected = canonicalSubpaths(std::size(file_sizes));
    expected[0] = "root/renamed";
    expected[3] = "root/also-renamed";

    expectSubpaths(torrentInit(file_sizes, std::move(map)), expected);
}

// Full-length dnd and priority lists are the ordinary case: apply them
// verbatim. The zero-length file here must not send them down the realigning
// path, so it gets back the settings saved for it.
TEST_F(ResumeTest, savedDndAndPrioritiesFullLengthLists)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 0U, 1U, 1U };

    auto map = savedFilenames(canonicalSubpaths(std::size(file_sizes)));
    map.try_emplace(TR_KEY_dnd, dndList({ false, true, false, true }));
    map.try_emplace(TR_KEY_priority, priorityList({ TR_PRI_NORMAL, TR_PRI_HIGH, TR_PRI_LOW, TR_PRI_NORMAL }));

    expectWantedAndPriorities(
        torrentInit(file_sizes, std::move(map)),
        { true, false, true, false },
        { TR_PRI_NORMAL, TR_PRI_HIGH, TR_PRI_LOW, TR_PRI_NORMAL });
}

// The dnd and priority lists in a 4.0.x resume file are short by the same
// zero-length files as its filename list, and realign the same way. A file
// with no entry keeps what a fresh torrent gives it: wanted, normal priority.
TEST_F(ResumeTest, savedDndAndPrioritiesListsWrittenWithoutZeroLengthFiles)
{
    for (auto const& [name, file_sizes] : layouts())
    {
        SCOPED_TRACE(name);

        // "don't download the last file, and give it high priority", as 4.0.x
        // would have saved it: one entry per nonempty file. Those settings
        // belong to the last nonempty file, which is the one a positional
        // application of a short list would leave on an earlier file.
        auto dnd = std::vector<bool>{};
        auto priorities = std::vector<tr_priority_t>{};
        for (auto const file_size : file_sizes)
        {
            if (file_size != 0U)
            {
                dnd.push_back(false);
                priorities.push_back(TR_PRI_NORMAL);
            }
        }
        dnd.back() = true;
        priorities.back() = TR_PRI_HIGH;

        auto expected_wanted = std::vector<bool>(std::size(file_sizes), true);
        auto expected_priorities = std::vector<tr_priority_t>(std::size(file_sizes), TR_PRI_NORMAL);
        for (size_t i = std::size(file_sizes); i > 0U; --i)
        {
            if (file_sizes[i - 1U] != 0U)
            {
                expected_wanted[i - 1U] = false;
                expected_priorities[i - 1U] = TR_PRI_HIGH;
                break;
            }
        }

        auto map = savedFilenames(legacySubpaths(file_sizes));
        map.try_emplace(TR_KEY_dnd, dndList(dnd));
        map.try_emplace(TR_KEY_priority, priorityList(priorities));

        expectWantedAndPriorities(torrentInit(file_sizes, std::move(map)), expected_wanted, expected_priorities);
    }
}

// Without zero-length files to account for, short dnd and priority lists have
// no reading that pairs them up with the files, so none of them is applied.
TEST_F(ResumeTest, savedDndAndPrioritiesShortListsWithoutZeroLengthFiles)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 1U, 1U, 1U };

    auto map = savedFilenames(canonicalSubpaths(std::size(file_sizes)));
    map.try_emplace(TR_KEY_dnd, dndList({ false, true, false }));
    map.try_emplace(TR_KEY_priority, priorityList({ TR_PRI_HIGH, TR_PRI_HIGH, TR_PRI_HIGH }));

    expectWantedAndPriorities(
        torrentInit(file_sizes, std::move(map)),
        { true, true, true, true },
        { TR_PRI_NORMAL, TR_PRI_NORMAL, TR_PRI_NORMAL, TR_PRI_NORMAL });
}

// Nor are they applied when they're short by more than the zero-length files.
TEST_F(ResumeTest, savedDndAndPrioritiesListsOfUnusableLength)
{
    auto const file_sizes = std::vector<uint64_t>{ 1U, 0U, 1U, 1U };

    auto map = savedFilenames(canonicalSubpaths(std::size(file_sizes)));
    map.try_emplace(TR_KEY_dnd, dndList({ false, true }));
    map.try_emplace(TR_KEY_priority, priorityList({ TR_PRI_HIGH, TR_PRI_HIGH }));

    expectWantedAndPriorities(
        torrentInit(file_sizes, std::move(map)),
        { true, true, true, true },
        { TR_PRI_NORMAL, TR_PRI_NORMAL, TR_PRI_NORMAL, TR_PRI_NORMAL });
}

// The mtimes list in a resume file written without zero-length files realigns
// like the others. A file checked against another file's mtime looks changed,
// so applying such a list by position throws away the checked state of every
// piece after the torrent's first zero-length file and rehashes it.
TEST_F(ResumeTest, savedMtimesListWrittenWithoutZeroLengthFiles)
{
    // One piece per nonempty file, so that each file's mtime decides one
    // piece. The zero-length file has no piece of its own; it shares the one
    // that begins where it does, which is f2's.
    auto const file_sizes = std::vector<uint64_t>{ PieceSize, 0U, PieceSize, PieceSize, PieceSize };
    auto const mtimes = createFiles(file_sizes);

    // Save f4's real mtime and nobody else's: a file whose saved mtime is 0
    // counts as untested, so f4's piece is the only one that can stay checked,
    // and only if f4's entry lands back on f4.
    auto mtime_list = tr_variant::Vector{};
    mtime_list.emplace_back(int64_t{ 0 }); // f0
    mtime_list.emplace_back(int64_t{ 0 }); // f2
    mtime_list.emplace_back(int64_t{ 0 }); // f3
    mtime_list.emplace_back(int64_t{ mtimes.back() }); // f4

    auto progress = tr_variant::Map{ 3U };
    progress.try_emplace(TR_KEY_mtimes, std::move(mtime_list));
    progress.try_emplace(TR_KEY_pieces, tr_variant::unmanaged_string("all"sv));
    progress.try_emplace(TR_KEY_blocks, tr_variant::unmanaged_string("all"sv));

    auto map = savedFilenames(legacySubpaths(file_sizes));
    map.try_emplace(TR_KEY_progress, std::move(progress));

    auto const* const tor = torrentInit(file_sizes, std::move(map));

    EXPECT_FALSE(tor->is_piece_checked(0)); // f0
    EXPECT_FALSE(tor->is_piece_checked(1)); // f1 and f2
    EXPECT_FALSE(tor->is_piece_checked(2)); // f3
    EXPECT_TRUE(tor->is_piece_checked(3)); // f4
}

} // namespace libtransmission::test
