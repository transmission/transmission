/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "crypto-utils.h"
#include "file.h"
#include "makemeta.h"
#include "utils.h" // tr_free()

#include "test-fixtures.h"

#include <stdlib.h> // mktemp()
#include <string.h> // strlen()

class MakemetaTest: public SandboxedTest
{
protected:
    void test_single_file_impl(tr_tracker_info const* trackers,
                               size_t const trackerCount,
                               void const* payload,
                               size_t const payloadSize,
                               char const* comment,
                               bool isPrivate)
    {
        // char* sandbox;
        tr_info inf;

        // create a single input file
        auto* input_file = tr_buildPath(sandbox_, "test.XXXXXX", nullptr);
        create_tmpfile_with_contents(input_file, payload, payloadSize);
        auto* builder = tr_metaInfoBuilderCreate(input_file);
        EXPECT_EQ(1, builder->fileCount);
        EXPECT_STREQ(input_file, builder->top);
        EXPECT_STREQ(input_file, builder->files[0].filename);
        EXPECT_EQ(payloadSize, builder->files[0].size);
        EXPECT_EQ(payloadSize, builder->totalSize);
        EXPECT_FALSE(builder->isFolder);
        EXPECT_FALSE(builder->abortFlag);

        // have tr_makeMetaInfo() build the .torrent file
        auto* torrent_file = tr_strdup_printf("%s.torrent", input_file);
        tr_makeMetaInfo(builder, torrent_file, trackers, trackerCount, comment, isPrivate);
        EXPECT_EQ(isPrivate, builder->isPrivate);
        EXPECT_STREQ(torrent_file, builder->outputFile);
        EXPECT_STREQ(comment, builder->comment);
        EXPECT_EQ(trackerCount, builder->trackerCount);

        while (!builder->isDone)
        {
            tr_wait_msec(100);
        }

        // now let's check our work: parse the  .torrent file
        auto* ctor = tr_ctorNew(nullptr);
        sync();
        tr_ctorSetMetainfoFromFile(ctor, torrent_file);
        auto const parse_result = tr_torrentParse(ctor, &inf);
        EXPECT_EQ(TR_PARSE_OK, parse_result);

        // quick check of some of the parsed metainfo
        EXPECT_EQ(payloadSize, inf.totalSize);
        auto* tmpstr = tr_sys_path_basename(input_file, nullptr);
        EXPECT_STREQ(tmpstr, inf.name);
        tr_free(tmpstr);
        EXPECT_STREQ(comment, inf.comment);
        EXPECT_EQ(1, inf.fileCount);
        EXPECT_EQ(isPrivate, inf.isPrivate);
        EXPECT_FALSE(inf.isFolder);
        EXPECT_EQ(trackerCount, inf.trackerCount);

        // cleanup
        tr_free(torrent_file);
        tr_free(input_file);
        tr_ctorFree(ctor);
        tr_metainfoFree(&inf);
        tr_metaInfoBuilderFree(builder);
    }

    void test_single_directory_impl(tr_tracker_info const* trackers,
                                    size_t const trackerCount,
                                    void const** payloads,
                                    size_t const* payloadSizes,
                                    size_t const payloadCount,
                                    char const* comment,
                                    bool const isPrivate)
    {
        // create the top temp directory
        auto* top = tr_buildPath(sandbox_, "folder.XXXXXX", nullptr);
        tr_sys_dir_create_temp(top, nullptr);

        // build the payload files that go into the top temp directory
        std::vector<char*> files;
        files.reserve(payloadCount);
        size_t totalSize = 0;

        for (size_t i = 0; i < payloadCount; i++)
        {
            char tmpl[16];
            tr_snprintf(tmpl, sizeof(tmpl), "file.%04zu%s", i, "XXXXXX");
            auto* path = tr_buildPath(top, tmpl, nullptr);
            files.push_back(path);
            create_tmpfile_with_contents(path, payloads[i], payloadSizes[i]);
            totalSize += payloadSizes[i];
        }

        sync();

        // init the builder
        auto* builder = tr_metaInfoBuilderCreate(top);
        EXPECT_FALSE(builder->abortFlag);
        EXPECT_STREQ(top, builder->top);
        EXPECT_EQ(payloadCount, builder->fileCount);
        EXPECT_EQ(totalSize, builder->totalSize);
        EXPECT_TRUE(builder->isFolder);

        for (size_t i = 0; i < builder->fileCount; ++i)
        {
            EXPECT_STREQ(files[i], builder->files[i].filename);
            EXPECT_EQ(payloadSizes[i], builder->files[i].size);
        }

        // call tr_makeMetaInfo() to build the .torrent file
        auto* torrent_file = tr_strdup_printf("%s.torrent", top);
        tr_makeMetaInfo(builder, torrent_file, trackers, trackerCount, comment, isPrivate);
        EXPECT_EQ(isPrivate, builder->isPrivate);
        EXPECT_STREQ(torrent_file, builder->outputFile);
        EXPECT_STREQ(comment, builder->comment);
        EXPECT_EQ(trackerCount, builder->trackerCount);

        while (!builder->isDone)
        {
            tr_wait_msec(100);
        }

        // now let's check our work: parse the  .torrent file
        sync();
        auto* ctor = tr_ctorNew(nullptr);
        tr_ctorSetMetainfoFromFile(ctor, torrent_file);
        tr_info inf;
        auto parse_result = tr_torrentParse(ctor, &inf);
        EXPECT_EQ(TR_PARSE_OK, parse_result);

        // quick check of some of the parsed metainfo
        EXPECT_EQ(totalSize, inf.totalSize);
        auto* tmpstr = tr_sys_path_basename(top, nullptr);
        EXPECT_STREQ(tmpstr, inf.name);
        tr_free(tmpstr);
        EXPECT_STREQ(comment, inf.comment);
        EXPECT_EQ(payloadCount, inf.fileCount);
        EXPECT_EQ(isPrivate, inf.isPrivate);
        EXPECT_EQ(builder->isFolder, inf.isFolder);
        EXPECT_EQ(trackerCount, inf.trackerCount);

        // cleanup
        tr_free(torrent_file);
        tr_ctorFree(ctor);
        tr_metainfoFree(&inf);
        tr_metaInfoBuilderFree(builder);

        for (size_t i = 0; i < payloadCount; i++)
        {
            tr_free(files[i]);
        }

        tr_free(top);
    }

    void test_single_directory_random_payload_impl(tr_tracker_info const* trackers,
                                                   size_t const trackerCount,
                                                   size_t const maxFileCount,
                                                   size_t const maxFileSize,
                                                   char const* comment,
                                                   bool const isPrivate)
    {

        // build random payloads
        size_t payloadCount = 1 + tr_rand_int_weak(maxFileCount);
        void** payloads = tr_new0(void*, payloadCount);
        size_t* payloadSizes = tr_new0(size_t, payloadCount);

        for (size_t i = 0; i < payloadCount; i++)
        {
            size_t const n = 1 + tr_rand_int_weak(maxFileSize);
            payloads[i] = tr_new(char, n);
            tr_rand_buffer(payloads[i], n);
            payloadSizes[i] = n;
        }

        // run the test
        test_single_directory_impl(trackers, trackerCount,
                                   (void const**)payloads, payloadSizes, payloadCount,
                                   comment, isPrivate);

        // cleanup
        for (size_t i = 0; i < payloadCount; i++)
        {
            tr_free(payloads[i]);
        }

        tr_free(payloads);
        tr_free(payloadSizes);
    }
};

TEST_F(MakemetaTest, single_file)
{
    tr_tracker_info trackers[16];
    size_t trackerCount = 0;
    trackers[trackerCount].tier = trackerCount;
    trackers[trackerCount].announce = (char*)"udp://tracker.openbittorrent.com:80";
    ++trackerCount;
    trackers[trackerCount].tier = trackerCount;
    trackers[trackerCount].announce = (char*)"udp://tracker.publicbt.com:80";
    ++trackerCount;
    auto constexpr payload = std::string_view { "Hello, World!\n" };
    char const* const comment = "This is the comment";
    bool const isPrivate = false;
    test_single_file_impl(trackers, trackerCount,
                          std::data(payload), std::size(payload),
                          comment, isPrivate);
}

TEST_F(MakemetaTest, single_directory_random_payload)
{
    auto constexpr DEFAULT_MAX_FILE_COUNT = size_t { 16 };
    auto constexpr DEFAULT_MAX_FILE_SIZE = size_t { 1024 };

    tr_tracker_info trackers[16];
    size_t trackerCount;
    trackerCount = 0;
    trackers[trackerCount].tier = trackerCount;
    trackers[trackerCount].announce = (char*)"udp://tracker.openbittorrent.com:80";
    ++trackerCount;
    trackers[trackerCount].tier = trackerCount;
    trackers[trackerCount].announce = (char*)"udp://tracker.publicbt.com:80";
    ++trackerCount;
    char const* const comment = "This is the comment";
    bool const isPrivate = false;

    for (size_t i = 0; i < 10; i++)
    {
        test_single_directory_random_payload_impl(trackers, trackerCount,
                                                  DEFAULT_MAX_FILE_COUNT,
                                                  DEFAULT_MAX_FILE_SIZE,
                                                  comment, isPrivate);
    }
}
