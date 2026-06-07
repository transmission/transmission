// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdio> // stderr
#include <cstdlib> // EXIT_FAILURE
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/error.h>
#include <libtransmission/log.h>
#include <libtransmission/quark.h>
#include <libtransmission/string-utils.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>
#include <libtransmission/version.h>

namespace
{
char constexpr MyName[] = "transmission-edit";
char constexpr Usage[] = "Usage: transmission-edit [options] torrent-file(s)";

struct app_options
{
    std::vector<std::string_view> files;
    char const* add = nullptr;
    char const* deleteme = nullptr;
    std::array<char const*, 2> replace;
    char const* source = nullptr;
    bool show_version = false;
};

using Arg = tr_option::Arg;
auto constexpr Options = std::array<tr_option, 6>{ {
    { 'a', "add", "Add a tracker's announce URL", "a", Arg::Required, "<url>" },
    { 'd', "delete", "Delete a tracker's announce URL", "d", Arg::Required, "<url>" },
    { 'r', "replace", "Search and replace a substring in the announce URLs", "r", Arg::Required, "<old> <new>" },
    { 's', "source", "Set the source", "s", Arg::Required, "<source>" },
    { 'V', "version", "Show version number and exit", "V", Arg::None, nullptr },
    { 0, nullptr, nullptr, nullptr, Arg::None, nullptr },
} };
static_assert(Options[std::size(Options) - 2].val != 0);
} // namespace

namespace
{
int parseCommandLine(app_options& opts, int argc, char const* const* argv)
{
    int c;
    char const* optarg;

    while ((c = tr_getopt(Usage, argc, argv, std::data(Options), &optarg)) != TR_OPT_DONE)
    {
        switch (c)
        {
        case 'a':
            opts.add = optarg;
            break;

        case 'd':
            opts.deleteme = optarg;
            break;

        case 'r':
            opts.replace[0] = optarg;
            c = tr_getopt(Usage, argc, argv, std::data(Options), &optarg);

            if (c != TR_OPT_UNK)
            {
                return 1;
            }

            opts.replace[1] = optarg;
            break;

        case 's':
            opts.source = optarg;
            break;

        case 'V':
            opts.show_version = true;
            break;

        case TR_OPT_UNK:
            opts.files.push_back(optarg);
            break;

        default:
            return 1;
        }
    }

    return 0;
}

bool removeURL(tr_variant& metainfo, std::string_view url)
{
    auto* const map = metainfo.get_if<tr_variant::Map>();
    if (map == nullptr)
    {
        return false;
    }

    bool changed = false;

    if (auto sv_opt = map->value_if<std::string_view>(TR_KEY_announce); sv_opt && url == *sv_opt)
    {
        fmt::print("\tRemoved '{:s}' from 'announce'\n", *sv_opt);
        map->erase(TR_KEY_announce);
        changed = true;
    }

    if (auto* al_vec = map->find_if<tr_variant::Vector>(TR_KEY_announce_list); al_vec != nullptr)
    {
        int tierIndex = 0;

        for (auto tier_it = al_vec->begin(); tier_it != al_vec->end();)
        {
            auto* const tier_vec = tier_it->get_if<tr_variant::Vector>();
            if (tier_vec == nullptr)
            {
                ++tier_it;
                ++tierIndex;
                continue;
            }

            for (auto node_it = tier_vec->begin(); node_it != tier_vec->end();)
            {
                if (auto sv_opt = node_it->value_if<std::string_view>(); sv_opt && url == *sv_opt)
                {
                    fmt::print("\tRemoved '{:s}' from 'announce-list' tier #{:d}\n", *sv_opt, tierIndex + 1);
                    node_it = tier_vec->erase(node_it);
                    changed = true;
                }
                else
                {
                    ++node_it;
                }
            }

            if (tier_vec->empty())
            {
                fmt::print("\tNo URLs left in tier #{:d}... removing tier\n", tierIndex + 1);
                tier_it = al_vec->erase(tier_it);
            }
            else
            {
                ++tier_it;
                ++tierIndex;
            }
        }

        if (al_vec->empty())
        {
            fmt::print("\tNo tiers left... removing announce-list\n");
            map->erase(TR_KEY_announce_list);
        }
    }

    /* if we removed the "announce" field and there's still another track left,
     * use it as the "announce" field */
    if (changed && !map->value_if<std::string_view>(TR_KEY_announce))
    {
        if (auto* al_vec = map->find_if<tr_variant::Vector>(TR_KEY_announce_list); al_vec != nullptr && !al_vec->empty())
        {
            if (auto* const tier_vec = al_vec->front().get_if<tr_variant::Vector>(); tier_vec != nullptr && !tier_vec->empty())
            {
                if (auto sv_opt = tier_vec->front().value_if<std::string_view>())
                {
                    (*map)[TR_KEY_announce] = std::string_view{ *sv_opt };
                    fmt::print("\tAdded '{:s}' to announce\n", *sv_opt);
                }
            }
        }
    }

    return changed;
}

[[nodiscard]] auto replaceSubstr(std::string_view str, std::string_view oldval, std::string_view newval)
{
    auto ret = std::string{};

    while (!std::empty(str))
    {
        auto const pos = str.find(oldval);
        ret += str.substr(0, pos);
        if (pos == std::string_view::npos)
        {
            break;
        }
        ret += newval;
        str.remove_prefix(pos + std::size(oldval));
    }

    return ret;
}

bool replaceURL(tr_variant& metainfo, std::string_view oldval, std::string_view newval)
{
    auto* const map = metainfo.get_if<tr_variant::Map>();
    if (map == nullptr)
    {
        return false;
    }

    bool changed = false;

    if (auto sv_opt = map->value_if<std::string_view>(TR_KEY_announce); sv_opt && tr_strv_contains(*sv_opt, oldval))
    {
        auto const newstr = replaceSubstr(*sv_opt, oldval, newval);
        fmt::print("\tReplaced in 'announce': '{:s}' --> '{:s}'\n", *sv_opt, newstr);
        (*map)[TR_KEY_announce] = std::string_view{ newstr };
        changed = true;
    }

    if (auto* const al_vec = map->find_if<tr_variant::Vector>(TR_KEY_announce_list); al_vec != nullptr)
    {
        int tierCount = 0;

        for (auto& tier_variant : *al_vec)
        {
            if (auto* const tier_vec = tier_variant.get_if<tr_variant::Vector>())
            {
                for (auto& node : *tier_vec)
                {
                    if (auto sv_opt = node.value_if<std::string_view>(); sv_opt && tr_strv_contains(*sv_opt, oldval))
                    {
                        auto const newstr = replaceSubstr(*sv_opt, oldval, newval);
                        fmt::print(
                            "\tReplaced in 'announce-list' tier #{:d}: '{:s}' --> '{:s}'\n",
                            tierCount + 1,
                            *sv_opt,
                            newstr);
                        node = std::string_view{ newstr };
                        changed = true;
                    }
                }
            }

            ++tierCount;
        }
    }

    return changed;
}
[[nodiscard]] bool announce_list_has_url(tr_variant::Vector const& announce_list, std::string_view url)
{
    for (auto const& tier_variant : announce_list)
    {
        if (auto const* const tier_vec = tier_variant.get_if<tr_variant::Vector>())
        {
            for (auto const& node : *tier_vec)
            {
                if (auto sv_opt = node.value_if<std::string_view>(); sv_opt && *sv_opt == url)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool addURL(tr_variant& metainfo, std::string_view url)
{
    auto* const map = metainfo.get_if<tr_variant::Map>();
    if (map == nullptr)
    {
        return false;
    }

    bool changed = false;
    auto const announce_opt = map->value_if<std::string_view>(TR_KEY_announce);
    auto* al_vec = map->find_if<tr_variant::Vector>(TR_KEY_announce_list);
    bool const had_announce = announce_opt.has_value();
    bool const had_announce_list = al_vec != nullptr;

    if (!had_announce && !had_announce_list)
    {
        /* this new tracker is the only one, so add it to "announce"... */
        fmt::print("\tAdded '{:s}' in 'announce'\n", url);
        (*map)[TR_KEY_announce] = std::string_view{ url };
        changed = true;
    }
    else
    {
        if (!had_announce_list)
        {
            al_vec = map->insert_or_assign(TR_KEY_announce_list, tr_variant::make_vector(2)).first.get_if<tr_variant::Vector>();

            if (had_announce)
            {
                /* we're moving from an 'announce' to an 'announce-list',
                 * so copy the old announce URL to the list */
                al_vec->emplace_back(tr_variant::make_vector(1))
                    .get_if<tr_variant::Vector>()
                    ->emplace_back(std::string_view{ *announce_opt });
                changed = true;
            }
        }

        /* If the user-specified URL isn't in the announce list yet, add it */
        if (!announce_list_has_url(*al_vec, url))
        {
            al_vec->emplace_back(tr_variant::make_vector(1))
                .get_if<tr_variant::Vector>()
                ->emplace_back(std::string_view{ url });
            fmt::print("\tAdded '{:s}' to 'announce-list' tier #{:d}\n", url, al_vec->size());
            changed = true;
        }
    }

    return changed;
}

bool setSource(tr_variant& metainfo, std::string_view source_value)
{
    auto* const map = metainfo.get_if<tr_variant::Map>();
    if (map == nullptr)
    {
        return false;
    }

    auto const current_source_opt = map->value_if<std::string_view>(TR_KEY_source);

    if (!current_source_opt)
    {
        fmt::print("\tAdded '{:s}' as source\n", source_value);
        (*map)[TR_KEY_source] = std::string_view{ source_value };
        return true;
    }

    if (*current_source_opt != source_value)
    {
        fmt::print("\tUpdated source: '{:s}' -> '{:s}'\n", *current_source_opt, source_value);
        (*map)[TR_KEY_source] = std::string_view{ source_value };
        return true;
    }

    return false;
}
} // namespace

int tr_main(int argc, char* argv[])
{
    tr_locale_set_global("");

    int changedCount = 0;

    tr_logSetLevel(TR_LOG_ERROR);

    auto options = app_options{};
    if (parseCommandLine(options, argc, (char const* const*)argv) != 0)
    {
        return EXIT_FAILURE;
    }

    if (options.show_version)
    {
        fmt::print(stderr, "{:s} {:s}\n", MyName, LONG_VERSION_STRING);
        return EXIT_SUCCESS;
    }

    if (std::empty(options.files))
    {
        fmt::print(stderr, "ERROR: No torrent files specified.\n");
        tr_getopt_usage(MyName, Usage, std::data(Options));
        fmt::print(stderr, "\n");
        return EXIT_FAILURE;
    }

    if (options.add == nullptr && options.deleteme == nullptr && options.replace[0] == nullptr && options.source == nullptr)
    {
        fmt::print(stderr, "ERROR: Must specify -a, -d, -r or -s\n");
        tr_getopt_usage(MyName, Usage, std::data(Options));
        fmt::print(stderr, "\n");
        return EXIT_FAILURE;
    }

    auto serde = tr_variant_serde::benc();
    for (auto const& filename : options.files)
    {
        bool changed = false;

        fmt::print("{:s}\n", filename);

        auto otop = serde.parse_file(filename);
        if (!otop)
        {
            fmt::print("\tError reading file: {:s}\n", serde.error_.message());
            continue;
        }
        auto& top = *otop;

        if (options.deleteme != nullptr)
        {
            changed |= removeURL(top, options.deleteme);
        }

        if (options.add != nullptr)
        {
            changed = addURL(top, options.add);
        }

        if (options.replace[0] != nullptr && options.replace[1] != nullptr)
        {
            changed |= replaceURL(top, options.replace[0], options.replace[1]);
        }

        if (options.source != nullptr)
        {
            changed = setSource(top, options.source);
        }

        if (changed)
        {
            ++changedCount;
            serde.to_file(top, filename);
        }
    }

    fmt::print("Changed {:d} files\n", changedCount);

    return EXIT_SUCCESS;
}
