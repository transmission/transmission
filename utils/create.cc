// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cinttypes> // PRIu32
#include <cstdint> // uint32_t
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/log.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "units.h"

using namespace std::literals;

namespace
{

char constexpr MyName[] = "transmission-create";
char constexpr Usage[] = "Usage: transmission-create [options] <file|directory>";

uint32_t constexpr KiB = 1024;

auto constexpr Options = std::array<tr_option, 10>{
    { { 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", false, nullptr },
      { 'r', "source", "Set the source for private trackers", "r", true, "<source>" },
      { 'o', "outfile", "Save the generated .torrent to this filename", "o", true, "<file>" },
      { 's', "piecesize", "Set the piece size in KiB, overriding the preferred default", "s", true, "<KiB>" },
      { 'c', "comment", "Add a comment", "c", true, "<comment>" },
      { 't', "tracker", "Add a tracker's announce URL", "t", true, "<url>" },
      { 'w', "webseed", "Add a webseed URL", "w", true, "<url>" },
      { 'x', "anonymize", "Omit \"Creation date\" and \"Created by\" info", nullptr, false, nullptr },
      { 'V', "version", "Show version number and exit", "V", false, nullptr },
      { 0, nullptr, nullptr, nullptr, false, nullptr } }
};

struct app_options
{
    std::vector<tr_tracker_info> trackers;
    std::vector<char const*> webseeds;
    std::string outfile;
    char const* comment = nullptr;
    char const* infile = nullptr;
    char const* source = nullptr;
    uint32_t piecesize_kib = 0;
    bool is_private = false;
    bool show_version = false;
    bool anonymize = false;
};

int parseCommandLine(app_options& options, int argc, char const* const* argv)
{
    int c;
    char const* optarg;

    while ((c = tr_getopt(Usage, argc, argv, std::data(Options), &optarg)) != TR_OPT_DONE)
    {
        switch (c)
        {
        case 'V':
            options.show_version = true;
            break;

        case 'p':
            options.is_private = true;
            break;

        case 'o':
            options.outfile = optarg;
            break;

        case 'c':
            options.comment = optarg;
            break;

        case 't':
            options.trackers.push_back(tr_tracker_info{ 0, const_cast<char*>(optarg) });
            break;

        case 'w':
            options.webseeds.push_back(optarg);
            break;

        case 's':
            if (optarg != nullptr)
            {
                char* endptr = nullptr;
                options.piecesize_kib = strtoul(optarg, &endptr, 10);

                if (endptr != nullptr && *endptr == 'M')
                {
                    options.piecesize_kib *= KiB;
                }
            }

            break;

        case 'r':
            options.source = optarg;
            break;

        case 'x':
            options.anonymize = true;
            break;

        case TR_OPT_UNK:
            options.infile = optarg;
            break;

        default:
            return 1;
        }
    }

    return 0;
}

char* tr_getcwd(void)
{
    char* result;
    tr_error* error = nullptr;

    result = tr_sys_dir_get_current(&error);

    if (result == nullptr)
    {
        fprintf(stderr, "getcwd error: \"%s\"", error->message);
        tr_error_free(error);
        result = tr_strdup("");
    }

    return result;
}

} // namespace

int tr_main(int argc, char* argv[])
{
    tr_metainfo_builder* b = nullptr;

    tr_logSetLevel(TR_LOG_ERROR);
    tr_formatter_mem_init(MemK, MemKStr, MemMStr, MemGStr, MemTStr);
    tr_formatter_size_init(DiskK, DiskKStr, DiskMStr, DiskGStr, DiskTStr);
    tr_formatter_speed_init(SpeedK, SpeedKStr, SpeedMStr, SpeedGStr, SpeedTStr);

    auto options = app_options{};
    if (parseCommandLine(options, argc, (char const* const*)argv) != 0)
    {
        return EXIT_FAILURE;
    }

    if (options.show_version)
    {
        fprintf(stderr, "%s %s\n", MyName, LONG_VERSION_STRING);
        return EXIT_SUCCESS;
    }

    if (options.infile == nullptr)
    {
        fprintf(stderr, "ERROR: No input file or directory specified.\n");
        tr_getopt_usage(MyName, Usage, std::data(Options));
        fprintf(stderr, "\n");
        return EXIT_FAILURE;
    }

    if (std::empty(options.outfile))
    {
        tr_error* error = nullptr;
        auto const base = tr_sys_path_basename(options.infile, &error);

        if (std::empty(base))
        {
            fprintf(stderr, "ERROR: Cannot deduce output path from input path: %s\n", error->message);
            return EXIT_FAILURE;
        }

        char* const cwd = tr_getcwd();
        options.outfile = tr_strvDup(tr_pathbuf{ std::string_view{ cwd }, '/', base, ".torrent"sv });
        tr_free(cwd);
    }

    if (std::empty(options.trackers))
    {
        if (options.is_private)
        {
            fprintf(stderr, "ERROR: no trackers specified for a private torrent\n");
            return EXIT_FAILURE;
        }
        else
        {
            printf("WARNING: no trackers specified\n");
        }
    }

    printf("Creating torrent \"%s\"\n", options.outfile.c_str());

    b = tr_metaInfoBuilderCreate(options.infile);

    if (b == nullptr)
    {
        fprintf(stderr, "ERROR: Cannot find specified input file or directory.\n");
        return EXIT_FAILURE;
    }

    for (uint32_t i = 0; i < b->fileCount; ++i)
    {
        if (auto const& file = b->files[i]; !file.is_portable)
        {
            fprintf(stderr, "WARNING: consider renaming nonportable filename \"%s\".\n", file.filename);
        }
    }

    if (options.piecesize_kib != 0)
    {
        tr_metaInfoBuilderSetPieceSize(b, options.piecesize_kib * KiB);
    }

    printf(
        b->fileCount > 1 ? " %" PRIu32 " files, %s\n" : " %" PRIu32 " file, %s\n",
        b->fileCount,
        tr_formatter_size_B(b->totalSize).c_str());
    printf(
        b->pieceCount > 1 ? " %" PRIu32 " pieces, %s each\n" : " %" PRIu32 " piece, %s\n",
        b->pieceCount,
        tr_formatter_size_B(b->pieceSize).c_str());

    tr_makeMetaInfo(
        b,
        options.outfile.c_str(),
        std::data(options.trackers),
        static_cast<int>(std::size(options.trackers)),
        std::data(options.webseeds),
        static_cast<int>(std::size(options.webseeds)),
        options.comment,
        options.is_private,
        options.anonymize,
        options.source);

    uint32_t last = UINT32_MAX;
    while (!b->isDone)
    {
        tr_wait_msec(500);

        uint32_t current = b->pieceIndex;
        if (current != last)
        {
            printf("\rPiece %" PRIu32 "/%" PRIu32 " ...", current, b->pieceCount);
            fflush(stdout);

            last = current;
        }
    }

    putc(' ', stdout);

    switch (b->result)
    {
    case TrMakemetaResult::OK:
        printf("done!");
        break;

    case TrMakemetaResult::ERR_URL:
        printf("bad announce URL: \"%s\"", b->errfile);
        break;

    case TrMakemetaResult::ERR_IO_READ:
        printf("error reading \"%s\": %s", b->errfile, tr_strerror(b->my_errno));
        break;

    case TrMakemetaResult::ERR_IO_WRITE:
        printf("error writing \"%s\": %s", b->errfile, tr_strerror(b->my_errno));
        break;

    case TrMakemetaResult::CANCELLED:
        printf("cancelled");
        break;
    }

    putc('\n', stdout);

    tr_metaInfoBuilderFree(b);
    return EXIT_SUCCESS;
}
