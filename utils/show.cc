// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2(SPDX : GPL - 2.0), GPLv3(SPDX : GPL - 3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cinttypes> // PRIu64
#include <cstdio>
#include <ctime>
#include <iterator>
#include <string>
#include <string_view>

#include <curl/curl.h>

#include <event2/buffer.h>

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/log.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/tr-macros.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>
#include <libtransmission/web-utils.h>

#include "units.h"

using namespace std::literals;

namespace
{

auto constexpr TimeoutSecs = long{ 30 };

char constexpr MyName[] = "transmission-show";
char constexpr Usage[] = "Usage: transmission-show [options] <torrent-file>";
char constexpr UserAgent[] = "transmission-show/" LONG_VERSION_STRING;

auto options = std::array<tr_option, 14>{
    { { 'd', "header", "Show only header section", "d", false, nullptr },
      { 'i', "info", "Show only info section", "i", false, nullptr },
      { 't', "trackers", "Show only trackers section", "t", false, nullptr },
      { 'f', "files", "Show only file list", "f", false, nullptr },
      { 'D', "no-header", "Do not show header section", "D", false, nullptr },
      { 'I', "no-info", "Do not show info section", "I", false, nullptr },
      { 'T', "no-trackers", "Do not show trackers section", "T", false, nullptr },
      { 'F', "no-files", "Do not show files section", "F", false, nullptr },
      { 'b', "bytes", "Show file sizes in bytes", "b", false, nullptr },
      { 'm', "magnet", "Give a magnet link for the specified torrent", "m", false, nullptr },
      { 's', "scrape", "Ask the torrent's trackers how many peers are in the torrent's swarm", "s", false, nullptr },
      { 'u', "unsorted", "Do not sort files by name", "u", false, nullptr },
      { 'V', "version", "Show version number and exit", "V", false, nullptr },
      { 0, nullptr, nullptr, nullptr, false, nullptr } }
};

struct app_opts
{
    std::string_view filename;
    bool scrape = false;
    bool show_magnet = false;
    bool show_version = false;
    bool unsorted = false;
    bool print_header = true;
    bool print_info = true;
    bool print_trackers = true;
    bool print_files = true;
    bool show_bytesize = false;
};

int parseCommandLine(app_opts& opts, int argc, char const* const* argv)
{
    int c;
    char const* optarg;

    while ((c = tr_getopt(Usage, argc, argv, std::data(options), &optarg)) != TR_OPT_DONE)
    {
        switch (c)
        {
        case 'b':
            opts.show_bytesize = true;
            break;

        case 'f':
            opts.print_header = false;
            opts.print_info = false;
            opts.print_trackers = false;
            opts.print_files = true;
            break;

        case 'd':
            opts.print_header = true;
            opts.print_info = false;
            opts.print_trackers = false;
            opts.print_files = false;
            break;

        case 'i':
            opts.print_header = false;
            opts.print_info = true;
            opts.print_trackers = false;
            opts.print_files = false;
            break;

        case 't':
            opts.print_header = false;
            opts.print_info = false;
            opts.print_trackers = true;
            opts.print_files = false;
            break;

        case 'D':
            opts.print_header = false;
            break;

        case 'I':
            opts.print_info = false;
            break;

        case 'T':
            opts.print_trackers = false;
            break;

        case 'F':
            opts.print_files = false;
            break;

        case 'm':
            opts.show_magnet = true;
            break;

        case 's':
            opts.scrape = true;
            break;

        case 'u':
            opts.unsorted = true;
            break;

        case 'V':
            opts.show_version = true;
            break;

        case TR_OPT_UNK:
            opts.filename = optarg;
            break;

        default:
            return 1;
        }
    }

    return 0;
}

[[nodiscard]] auto toString(time_t now)
{
    return now == 0 ? "Unknown" : fmt::format("{:%a %b %d %T %Y}", fmt::localtime(now));
}

bool compare_2nd_field(std::string_view const& l, std::string_view const& r)
{
    auto l_ = l.find(" ");
    auto r_ = r.find(" ");

    if (l_ == std::string_view::npos)
    {
        return false;
    }
    if (r_ == std::string_view::npos)
    {
        return true;
    }
    return l.substr(l_) <= r.substr(r_);
}

void showInfo(app_opts const& opts, tr_torrent_metainfo const& metainfo)
{
    /**
    ***  General Info
    **/
    if (opts.print_info)
    {
        printf("GENERAL\n\n");
        printf("  Name: %s\n", metainfo.name().c_str());
        printf("  Hash: %" TR_PRIsv "\n", TR_PRIsv_ARG(metainfo.infoHashString()));
        printf("  Created by: %s\n", std::empty(metainfo.creator()) ? "Unknown" : metainfo.creator().c_str());
        printf("  Created on: %s\n\n", toString(metainfo.dateCreated()).c_str());

        if (!std::empty(metainfo.comment()))
        {
            printf("  Comment: %s\n", metainfo.comment().c_str());
        }

        if (!std::empty(metainfo.source()))
        {
            printf("  Source: %s\n", metainfo.source().c_str());
        }

        printf("  Piece Count: %" PRIu32 "\n", metainfo.pieceCount());
        printf("  Piece Size: %s\n", tr_formatter_mem_B(metainfo.pieceSize()).c_str());
        printf("  Total Size: %s\n", tr_formatter_size_B(metainfo.totalSize()).c_str());
        printf("  Privacy: %s\n", metainfo.isPrivate() ? "Private torrent" : "Public torrent");
    }

    /**
    ***  Trackers
    **/

    if (opts.print_trackers)
    {
        printf("\nTRACKERS\n");
        auto current_tier = std::optional<tr_tracker_tier_t>{};
        auto print_tier = size_t{ 1 };
        for (auto const& tracker : metainfo.announceList())
        {
            if (!current_tier || current_tier != tracker.tier)
            {
                current_tier = tracker.tier;
                printf("\n  Tier #%zu\n", print_tier);
                ++print_tier;
            }

            printf("  %" TR_PRIsv "\n", TR_PRIsv_ARG(tracker.announce.sv()));
        }

        /**
    ***
    **/

        if (auto const n_webseeds = metainfo.webseedCount(); n_webseeds > 0)
        {
            printf("\nWEBSEEDS\n\n");

            for (size_t i = 0; i < n_webseeds; ++i)
            {
                printf("  %s\n", metainfo.webseed(i).c_str());
            }
        }
    }

    /**
    ***  Files
    **/

    if (opts.print_files)
    {
        if (!opts.show_bytesize)
        {
            printf("\nFILES\n\n");
        }

        auto filenames = std::vector<std::string>{};
        for (tr_file_index_t i = 0, n = metainfo.fileCount(); i < n; ++i)
        {
            std::string filename;
            if (opts.show_bytesize)
            {
                filename = std::to_string(metainfo.fileSize(i));
                filename += " ";
                filename += metainfo.fileSubpath(i);
            }
            else
            {
                filename = "  ";
                filename += metainfo.fileSubpath(i);
                filename += " (";
                filename += tr_formatter_size_B(metainfo.fileSize(i));
                filename += ')';
            }
            filenames.emplace_back(filename);
        }

        if (!opts.unsorted)
        {
            if (opts.show_bytesize)
            {
                std::sort(std::begin(filenames), std::end(filenames), compare_2nd_field);
            }
            else
            {
                std::sort(std::begin(filenames), std::end(filenames));
            }
        }

        for (auto const& filename : filenames)
        {
            printf("%s\n", filename.c_str());
        }
    }
}

size_t writeFunc(void* ptr, size_t size, size_t nmemb, void* vbuf)
{
    auto* buf = static_cast<evbuffer*>(vbuf);
    size_t const byteCount = size * nmemb;
    evbuffer_add(buf, ptr, byteCount);
    return byteCount;
}

CURL* tr_curl_easy_init(struct evbuffer* writebuf)
{
    CURL* curl = curl_easy_init();
    (void)curl_easy_setopt(curl, CURLOPT_USERAGENT, UserAgent);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunc);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, writebuf);
    (void)curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    (void)curl_easy_setopt(curl, CURLOPT_VERBOSE, tr_env_key_exists("TR_CURL_VERBOSE"));
    (void)curl_easy_setopt(curl, CURLOPT_ENCODING, "");
    return curl;
}

void doScrape(tr_torrent_metainfo const& metainfo)
{
    auto* const buf = evbuffer_new();
    auto* const curl = tr_curl_easy_init(buf);

    for (auto const& tracker : metainfo.announceList())
    {
        if (std::empty(tracker.scrape))
        {
            continue;
        }

        // build the full scrape URL
        auto escaped = std::array<char, TR_SHA1_DIGEST_LEN * 3 + 1>{};
        tr_http_escape_sha1(std::data(escaped), metainfo.infoHash());
        auto const scrape = tracker.scrape.sv();
        auto const url = tr_urlbuf{ scrape,
                                    tr_strvContains(scrape, '?') ? '&' : '?',
                                    "info_hash="sv,
                                    std::string_view{ std::data(escaped) } };

        printf("%" TR_PRIsv " ... ", TR_PRIsv_ARG(url));
        fflush(stdout);

        // execute the http scrape
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, TimeoutSecs);
        if (auto const res = curl_easy_perform(curl); res != CURLE_OK)
        {
            printf("error: %s\n", curl_easy_strerror(res));
            continue;
        }

        // check the response code
        long response;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
        if (response != 200 /*HTTP OK*/)
        {
            printf("error: unexpected response %ld \"%s\"\n", response, tr_webGetResponseStr(response));
            continue;
        }

        // print it out
        tr_variant top;
        auto* const begin = (char const*)evbuffer_pullup(buf, -1);
        auto sv = std::string_view{ begin, evbuffer_get_length(buf) };
        if (!tr_variantFromBuf(&top, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, sv))
        {
            printf("error parsing scrape response\n");
            continue;
        }

        bool matched = false;
        if (tr_variant* files = nullptr; tr_variantDictFindDict(&top, TR_KEY_files, &files))
        {
            size_t child_pos = 0;
            tr_quark key;
            tr_variant* val;

            auto hashsv = std::string_view{ reinterpret_cast<char const*>(std::data(metainfo.infoHash())),
                                            std::size(metainfo.infoHash()) };

            while (tr_variantDictChild(files, child_pos, &key, &val))
            {
                if (hashsv == tr_quark_get_string_view(key))
                {
                    auto i = int64_t{};
                    auto const seeders = tr_variantDictFindInt(val, TR_KEY_complete, &i) ? int(i) : -1;
                    auto const leechers = tr_variantDictFindInt(val, TR_KEY_incomplete, &i) ? int(i) : -1;
                    printf("%d seeders, %d leechers\n", seeders, leechers);
                    matched = true;
                }

                ++child_pos;
            }
        }

        tr_variantFree(&top);

        if (!matched)
        {
            printf("no match\n");
        }
    }

    curl_easy_cleanup(curl);
    evbuffer_free(buf);
}

} // namespace

int tr_main(int argc, char* argv[])
{
    tr_logSetLevel(TR_LOG_ERROR);
    tr_formatter_mem_init(MemK, MemKStr, MemMStr, MemGStr, MemTStr);
    tr_formatter_size_init(DiskK, DiskKStr, DiskMStr, DiskGStr, DiskTStr);
    tr_formatter_speed_init(SpeedK, SpeedKStr, SpeedMStr, SpeedGStr, SpeedTStr);

    auto opts = app_opts{};
    if (parseCommandLine(opts, argc, (char const* const*)argv) != 0)
    {
        return EXIT_FAILURE;
    }

    if (opts.show_version)
    {
        fprintf(stderr, "%s %s\n", MyName, LONG_VERSION_STRING);
        return EXIT_SUCCESS;
    }

    /* make sure the user specified a filename */
    if (std::empty(opts.filename))
    {
        fprintf(stderr, "ERROR: No torrent file specified.\n");
        tr_getopt_usage(MyName, Usage, std::data(options));
        fprintf(stderr, "\n");
        return EXIT_FAILURE;
    }

    /* try to parse the torrent file */
    auto metainfo = tr_torrent_metainfo{};
    tr_error* error = nullptr;
    auto const parsed = metainfo.parseTorrentFile(opts.filename, nullptr, &error);
    if (error != nullptr)
    {
        fprintf(
            stderr,
            "Error parsing torrent file \"%" TR_PRIsv "\": %s (%d)\n",
            TR_PRIsv_ARG(opts.filename),
            error->message,
            error->code);
        tr_error_clear(&error);
    }
    if (!parsed)
    {
        return EXIT_FAILURE;
    }

    if (opts.show_magnet)
    {
        printf("%s", metainfo.magnet().c_str());
    }
    else
    {
        if (opts.print_header)
        {
            printf("Name: %s\n", metainfo.name().c_str());
            printf("File: %" TR_PRIsv "\n", TR_PRIsv_ARG(opts.filename));
            printf("\n");
            fflush(stdout);
        }

        if (opts.scrape)
        {
            doScrape(metainfo);
        }
        else
        {
            showInfo(opts, metainfo);
        }
    }

    /* cleanup */
    putc('\n', stdout);
    return EXIT_SUCCESS;
}
