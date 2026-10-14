// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <string_view>
#include <utility>

#include <libtransmission/transmission.h>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/quark.h>
#include <libtransmission/resume.h>
#include <libtransmission/torrent-ctor.h>
#include <libtransmission/torrent.h>
#include <libtransmission/variant.h>

#include "gtest/gtest.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{

class ResumeTest : public SessionTest
{
protected:
    static constexpr auto OriginalFilenames = std::array<std::string_view, 6>{
        "root/zero-first"sv, "root/a"sv, "root/zero"sv, "root/b"sv, "root/c"sv, "root/zero-last"sv,
    };

    void SetUp() override
    {
        SessionTest::SetUp();

        ctor_ = tr_ctorNew(session_);
        auto const metainfo = tr_base64_decode(
            "ZDQ6aW5mb2Q1OmZpbGVzbGQ2Omxlbmd0aGkwZTQ6cGF0aGwxMDp6ZXJvLWZpcnN0ZWVkNjpsZW5n"
            "dGhpMWU0OnBhdGhsMTphZWVkNjpsZW5ndGhpMGU0OnBhdGhsNDp6ZXJvZWVkNjpsZW5ndGhpMWU0"
            "OnBhdGhsMTpiZWVkNjpsZW5ndGhpMWU0OnBhdGhsMTpjZWVkNjpsZW5ndGhpMGU0OnBhdGhsOTp6"
            "ZXJvLWxhc3RlZWU0Om5hbWU0OnJvb3QxMjpwaWVjZSBsZW5ndGhpMTYzODRlNjpwaWVjZXMyMDqp"
            "mT42RwaBaro+JXF4UMJsnNDYnWVl");
        auto error = tr_error{};
        ASSERT_TRUE(tr_ctorSetMetainfo(ctor_, std::data(metainfo), std::size(metainfo), &error));
        ASSERT_FALSE(error) << error;
        tr_ctorSetPaused(ctor_, TR_FORCE, true);

        tor_ = createTorrentAndWaitForVerifyDone(ctor_);
        ASSERT_NE(nullptr, tor_);
        ASSERT_EQ(OriginalFilenames.size(), tor_->file_count());
        expectOriginalFilenames();
    }

    void TearDown() override
    {
        if (ctor_ != nullptr)
        {
            tr_ctorFree(ctor_);
        }
        SessionTest::TearDown();
    }

    void writeResume(tr_variant::Map map)
    {
        ASSERT_TRUE(tr_variant_serde::benc().to_file(tr_variant{ std::move(map) }, tor_->resume_file()));
    }

    void writeFilenames(std::initializer_list<std::string_view> filenames, bool const needs_verification = false)
    {
        auto list = tr_variant::Vector{};
        list.reserve(filenames.size());
        for (auto const filename : filenames)
        {
            list.emplace_back(tr_variant::unmanaged_string(filename));
        }

        auto map = tr_variant::Map{};
        map.try_emplace(TR_KEY_files, std::move(list));
        if (needs_verification)
        {
            map.try_emplace(TR_KEY_resume_filenames_need_verification, true);
        }
        writeResume(std::move(map));
    }

    tr_resume::fields_t loadFilenames()
    {
        auto helper = tr_torrent::ResumeHelper{ *tor_ };
        return tr_resume::load(tor_, helper, tr_resume::Filenames, *ctor_);
    }

    tr_resume::fields_t loadFields(tr_resume::fields_t const fields)
    {
        auto helper = tr_torrent::ResumeHelper{ *tor_ };
        return tr_resume::load(tor_, helper, fields, *ctor_);
    }

    void writeLegacyZeroFileLayout(
        std::array<std::string_view, 3> const& filenames,
        std::array<bool, 3> const& dnd,
        std::array<tr_priority_t, 3> const& priorities,
        std::array<time_t, 3> const& mtimes)
    {
        auto filename_list = tr_variant::Vector{};
        auto dnd_list = tr_variant::Vector{};
        auto priority_list = tr_variant::Vector{};
        auto mtime_list = tr_variant::Vector{};
        for (size_t i = 0; i < filenames.size(); ++i)
        {
            filename_list.emplace_back(tr_variant::unmanaged_string(filenames[i]));
            dnd_list.emplace_back(dnd[i]);
            priority_list.emplace_back(priorities[i]);
            mtime_list.emplace_back(mtimes[i]);
        }

        auto progress = tr_variant::Map{};
        progress.try_emplace(TR_KEY_mtimes, std::move(mtime_list));

        auto map = tr_variant::Map{};
        map.try_emplace(TR_KEY_files, std::move(filename_list));
        map.try_emplace(TR_KEY_dnd, std::move(dnd_list));
        map.try_emplace(TR_KEY_priority, std::move(priority_list));
        map.try_emplace(TR_KEY_progress, std::move(progress));
        writeResume(std::move(map));
    }

    void saveResume()
    {
        auto const helper = tr_torrent::ResumeHelper{ *tor_ };
        tr_resume::save(tor_, helper);
    }

    [[nodiscard]] bool resumeFilenamesNeedVerification() const
    {
        auto const helper = tr_torrent::ResumeHelper{ *tor_ };
        return helper.resume_filenames_need_verification();
    }

    [[nodiscard]] bool startWhenStable() const
    {
        auto const helper = tr_torrent::ResumeHelper{ *tor_ };
        return helper.start_when_stable();
    }

    void setResumeFilenamesNeedVerification(bool const val)
    {
        auto helper = tr_torrent::ResumeHelper{ *tor_ };
        helper.load_resume_filenames_need_verification(val);
    }

    void expectOriginalFilenames() const
    {
        for (tr_file_index_t i = 0; i < OriginalFilenames.size(); ++i)
        {
            EXPECT_EQ(OriginalFilenames[i], tor_->file_subpath(i));
        }
    }

    void createCompleteFiles() const
    {
        createFileWithContents(tr_pathbuf{ tor_->download_dir(), "/root/zero-first" }, ""sv);
        createFileWithContents(tr_pathbuf{ tor_->download_dir(), "/root/a" }, "a"sv);
        createFileWithContents(tr_pathbuf{ tor_->download_dir(), "/root/zero" }, ""sv);
        createFileWithContents(tr_pathbuf{ tor_->download_dir(), "/root/b" }, "b"sv);
        createFileWithContents(tr_pathbuf{ tor_->download_dir(), "/root/c" }, "c"sv);
        createFileWithContents(tr_pathbuf{ tor_->download_dir(), "/root/zero-last" }, ""sv);
    }

    tr_ctor* ctor_ = nullptr;
    tr_torrent* tor_ = nullptr;
};

TEST_F(ResumeTest, rejectsWrongFilenameCount)
{
    // This has the legacy nonzero-file count, but not the other three
    // per-file arrays required to identify a Transmission 4.0.x layout.
    writeFilenames({ "root/renamed-a"sv, "root/renamed-b"sv, "root/renamed-c"sv });

    auto const loaded = loadFilenames();

    EXPECT_EQ(decltype(loaded){ 0 }, (loaded & tr_resume::Filenames));
    EXPECT_TRUE(resumeFilenamesNeedVerification());
    expectOriginalFilenames();
}

TEST_F(ResumeTest, migratesLegacyLayoutWithMultipleZeroLengthFilesAndQuarantines)
{
    writeLegacyZeroFileLayout(
        { "root/renamed-a"sv, "root/renamed-b"sv, "root/renamed-c"sv },
        { true, false, true },
        { TR_PRI_HIGH, TR_PRI_LOW, TR_PRI_HIGH },
        { 101, 102, 103 });

    auto const requested = tr_resume::Filenames | tr_resume::Progress | tr_resume::FilePriorities | tr_resume::Dnd;
    auto const loaded = loadFields(requested);

    EXPECT_EQ(requested, (loaded & requested));
    EXPECT_TRUE(resumeFilenamesNeedVerification());
    EXPECT_EQ(OriginalFilenames[0], tor_->file_subpath(0));
    EXPECT_EQ("root/renamed-a"sv, tor_->file_subpath(1));
    EXPECT_EQ(OriginalFilenames[2], tor_->file_subpath(2));
    EXPECT_EQ("root/renamed-b"sv, tor_->file_subpath(3));
    EXPECT_EQ("root/renamed-c"sv, tor_->file_subpath(4));
    EXPECT_EQ(OriginalFilenames[5], tor_->file_subpath(5));

    EXPECT_TRUE(tr_torrentFile(tor_, 0).wanted);
    EXPECT_FALSE(tr_torrentFile(tor_, 1).wanted);
    EXPECT_TRUE(tr_torrentFile(tor_, 2).wanted);
    EXPECT_TRUE(tr_torrentFile(tor_, 3).wanted);
    EXPECT_FALSE(tr_torrentFile(tor_, 4).wanted);
    EXPECT_TRUE(tr_torrentFile(tor_, 5).wanted);

    EXPECT_EQ(TR_PRI_NORMAL, tr_torrentFile(tor_, 0).priority);
    EXPECT_EQ(TR_PRI_HIGH, tr_torrentFile(tor_, 1).priority);
    EXPECT_EQ(TR_PRI_NORMAL, tr_torrentFile(tor_, 2).priority);
    EXPECT_EQ(TR_PRI_LOW, tr_torrentFile(tor_, 3).priority);
    EXPECT_EQ(TR_PRI_HIGH, tr_torrentFile(tor_, 4).priority);
    EXPECT_EQ(TR_PRI_NORMAL, tr_torrentFile(tor_, 5).priority);

    saveResume();
    auto const saved = tr_variant_serde::benc().parse_file(tor_->resume_file());
    ASSERT_TRUE(saved);
    auto const* const map = saved->get_if<tr_variant::Map>();
    ASSERT_NE(nullptr, map);
    EXPECT_TRUE(map->value_if<bool>(TR_KEY_resume_filenames_need_verification).value_or(false));
    auto const* const filenames = map->find_if<tr_variant::Vector>(TR_KEY_files);
    auto const* const dnd = map->find_if<tr_variant::Vector>(TR_KEY_dnd);
    auto const* const priorities = map->find_if<tr_variant::Vector>(TR_KEY_priority);
    auto const* const progress = map->find_if<tr_variant::Map>(TR_KEY_progress);
    auto const* const mtimes = progress != nullptr ? progress->find_if<tr_variant::Vector>(TR_KEY_mtimes) : nullptr;
    ASSERT_NE(nullptr, filenames);
    ASSERT_NE(nullptr, dnd);
    ASSERT_NE(nullptr, priorities);
    ASSERT_NE(nullptr, mtimes);
    EXPECT_EQ(OriginalFilenames.size(), filenames->size());
    EXPECT_EQ(OriginalFilenames.size(), dnd->size());
    EXPECT_EQ(OriginalFilenames.size(), priorities->size());
    EXPECT_EQ(OriginalFilenames.size(), mtimes->size());
}

TEST_F(ResumeTest, rejectsLegacySizedLayoutWhenReconstructedPathsCollide)
{
    writeLegacyZeroFileLayout(
        { "root/a"sv, "root/a"sv, "root/c"sv },
        { false, false, false },
        { TR_PRI_NORMAL, TR_PRI_NORMAL, TR_PRI_NORMAL },
        { 101, 102, 103 });

    auto const loaded = loadFields(tr_resume::Filenames | tr_resume::Progress | tr_resume::FilePriorities | tr_resume::Dnd);

    EXPECT_EQ(decltype(loaded){ 0 }, (loaded & tr_resume::Filenames));
    EXPECT_EQ(decltype(loaded){ 0 }, (loaded & tr_resume::FilePriorities));
    EXPECT_EQ(decltype(loaded){ 0 }, (loaded & tr_resume::Dnd));
    EXPECT_TRUE(resumeFilenamesNeedVerification());
    expectOriginalFilenames();
}

TEST_F(ResumeTest, rejectsStoredPathCollidingWithMetainfoPathTransactionally)
{
    writeFilenames({ ""sv, ""sv, "root/renamed-zero"sv, ""sv, "root/renamed-c"sv, "root/a"sv });

    auto const loaded = loadFilenames();

    EXPECT_EQ(decltype(loaded){ 0 }, (loaded & tr_resume::Filenames));
    EXPECT_TRUE(resumeFilenamesNeedVerification());
    expectOriginalFilenames();
}

TEST_F(ResumeTest, acceptsLegacyEmptyPaths)
{
    writeFilenames({ ""sv, ""sv, "root/renamed-zero"sv, ""sv, ""sv, ""sv });

    auto const loaded = loadFilenames();

    EXPECT_NE(decltype(loaded){ 0 }, (loaded & tr_resume::Filenames));
    EXPECT_FALSE(resumeFilenamesNeedVerification());
    EXPECT_EQ(OriginalFilenames[0], tor_->file_subpath(0));
    EXPECT_EQ(OriginalFilenames[1], tor_->file_subpath(1));
    EXPECT_EQ("root/renamed-zero"sv, tor_->file_subpath(2));
    EXPECT_EQ(OriginalFilenames[3], tor_->file_subpath(3));
    EXPECT_EQ(OriginalFilenames[4], tor_->file_subpath(4));
    EXPECT_EQ(OriginalFilenames[5], tor_->file_subpath(5));
}

TEST_F(ResumeTest, rejectsEntireListWhenAnElementIsNotAString)
{
    auto list = tr_variant::Vector{};
    list.emplace_back(tr_variant::unmanaged_string("root/renamed-zero-first"sv));
    list.emplace_back(tr_variant::unmanaged_string("root/renamed-a"sv));
    list.emplace_back(tr_variant::unmanaged_string("root/renamed-zero"sv));
    list.emplace_back(tr_variant::unmanaged_string("root/renamed-b"sv));
    list.emplace_back(tr_variant::unmanaged_string("root/renamed-c"sv));
    list.emplace_back(int64_t{ 1 });
    auto map = tr_variant::Map{};
    map.try_emplace(TR_KEY_files, std::move(list));
    writeResume(std::move(map));

    auto const loaded = loadFilenames();

    EXPECT_EQ(decltype(loaded){ 0 }, (loaded & tr_resume::Filenames));
    EXPECT_TRUE(resumeFilenamesNeedVerification());
    expectOriginalFilenames();
}

TEST_F(ResumeTest, quarantineSurvivesResumeRewriteAndReload)
{
    writeFilenames({ "root/zero-first"sv, "root/a"sv, "root/zero"sv, "root/b"sv, "root/c"sv });
    auto const initially_loaded = loadFilenames();
    ASSERT_EQ(decltype(initially_loaded){ 0 }, (initially_loaded & tr_resume::Filenames));

    saveResume();
    setResumeFilenamesNeedVerification(false);
    ASSERT_FALSE(resumeFilenamesNeedVerification());

    auto const loaded = loadFilenames();

    EXPECT_NE(decltype(loaded){ 0 }, (loaded & tr_resume::Filenames));
    EXPECT_TRUE(resumeFilenamesNeedVerification());
    expectOriginalFilenames();

    tr_torrentStart(tor_);
    auto const stats = tr_torrentStat(tor_);
    EXPECT_EQ(TR_STATUS_STOPPED, stats.activity);
    EXPECT_EQ(tr_stat::Error::LocalError, stats.error);
    EXPECT_FALSE(stats.error_string.empty());
    EXPECT_FALSE(startWhenStable());
}

TEST_F(ResumeTest, successfulVerifyClearsQuarantineRewritesResumeAndKeepsTorrentStopped)
{
    writeFilenames({ "root/zero-first"sv, "root/a"sv, "root/zero"sv, "root/b"sv, "root/c"sv });
    auto const loaded = loadFilenames();
    ASSERT_EQ(decltype(loaded){ 0 }, (loaded & tr_resume::Filenames));
    tr_torrentStart(tor_);
    ASSERT_FALSE(startWhenStable());
    createCompleteFiles();

    blockingTorrentVerify(tor_);

    EXPECT_FALSE(resumeFilenamesNeedVerification());
    EXPECT_FALSE(startWhenStable());
    auto const stats = tr_torrentStat(tor_);
    EXPECT_EQ(TR_STATUS_STOPPED, stats.activity);
    EXPECT_EQ(tr_stat::Error::Ok, stats.error);

    auto const saved = tr_variant_serde::benc().parse_file(tor_->resume_file());
    ASSERT_TRUE(saved);
    auto const* const map = saved->get_if<tr_variant::Map>();
    ASSERT_NE(nullptr, map);
    EXPECT_FALSE(map->value_if<bool>(TR_KEY_resume_filenames_need_verification).value_or(false));
    auto const* const filenames = map->find_if<tr_variant::Vector>(TR_KEY_files);
    ASSERT_NE(nullptr, filenames);
    ASSERT_EQ(OriginalFilenames.size(), filenames->size());
    for (tr_file_index_t i = 0; i < OriginalFilenames.size(); ++i)
    {
        auto const filename = (*filenames)[i].value_if<std::string_view>();
        ASSERT_TRUE(filename);
        EXPECT_EQ(OriginalFilenames[i], *filename);
    }
}

TEST_F(ResumeTest, abortedVerifyKeepsQuarantine)
{
    writeFilenames({ "root/zero-first"sv, "root/a"sv, "root/zero"sv, "root/b"sv, "root/c"sv });
    auto const loaded = loadFilenames();
    ASSERT_EQ(decltype(loaded){ 0 }, (loaded & tr_resume::Filenames));
    tr_torrentStart(tor_);
    ASSERT_FALSE(startWhenStable());

    auto mediator = tr_torrent::VerifyMediator{ tor_ };
    mediator.on_verify_started();
    mediator.on_verify_done(true);

    EXPECT_TRUE(resumeFilenamesNeedVerification());
    EXPECT_FALSE(startWhenStable());
    auto const stats = tr_torrentStat(tor_);
    EXPECT_EQ(TR_STATUS_STOPPED, stats.activity);
    EXPECT_EQ(tr_stat::Error::LocalError, stats.error);
}

} // namespace tr::test
