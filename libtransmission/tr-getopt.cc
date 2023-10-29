// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstdlib> /* exit() */
#include <cstring>
#include <string_view>

#include <fmt/format.h>

#include "transmission.h"

#include "tr-getopt.h"
#include "tr-macros.h"
#include "utils.h"

using namespace std::literals;

int tr_optind = 1;

namespace
{
[[nodiscard]] constexpr std::string_view getArgName(tr_option const* opt)
{
    if (!opt->has_arg)
    {
        return ""sv;
    }

    if (opt->argName != nullptr)
    {
        return opt->argName;
    }

    return "<args>"sv;
}

[[nodiscard]] constexpr size_t get_next_line_len(std::string_view description, size_t maxlen)
{
    auto len = std::size(description);
    if (len > maxlen)
    {
        description.remove_suffix(len - maxlen);
        auto const pos = description.rfind(' ');
        len = pos != std::string_view::npos ? pos : maxlen;
    }
    return len;
}

void getopts_usage_line(tr_option const* const opt, size_t long_width, size_t short_width, size_t arg_width)
{
    auto const long_name = std::string_view{ opt->longName != nullptr ? opt->longName : "" };
    auto const short_name = std::string_view{ opt->shortName != nullptr ? opt->shortName : "" };
    auto const arg = getArgName(opt);

    fmt::print(
        FMT_STRING(" {:s}{:<{}s} {:s}{:<{}s} {:<{}s} "),
        std::empty(short_name) ? " "sv : "-"sv,
        short_name,
        short_width,
        std::empty(long_name) ? "  "sv : "--"sv,
        long_name,
        long_width,
        arg,
        arg_width);

    auto const d_indent = short_width + long_width + arg_width + 7U;
    auto const d_width = 80U - d_indent;

    auto description = std::string_view{ opt->description };
    auto len = get_next_line_len(description, d_width);
    fmt::print(FMT_STRING("{:s}\n"), description.substr(0, len));
    description.remove_prefix(len);
    description = tr_strvStrip(description);

    auto const indent = std::string(d_indent, ' ');
    while ((len = get_next_line_len(description, d_width)) != 0)
    {
        fmt::print(FMT_STRING("{:s}{:s}\n"), indent, description.substr(0, len));
        description.remove_prefix(len);
        description = tr_strvStrip(description);
    }
}

void maxWidth(struct tr_option const* o, size_t& long_width, size_t& short_width, size_t& arg_width)
{
    if (o->longName != nullptr)
    {
        long_width = std::max(long_width, strlen(o->longName));
    }

    if (o->shortName != nullptr)
    {
        short_width = std::max(short_width, strlen(o->shortName));
    }

    if (auto const arg = getArgName(o); !std::empty(arg))
    {
        arg_width = std::max(arg_width, std::size(arg));
    }
}

tr_option const* findOption(tr_option const* opts, char const* str, char const** setme_arg)
{
    size_t matchlen = 0;
    char const* arg = nullptr;
    tr_option const* match = nullptr;

    /* find the longest matching option */
    for (tr_option const* o = opts; o->val != 0; ++o)
    {
        size_t len = o->longName != nullptr ? strlen(o->longName) : 0;

        if (matchlen < len && str[0] == '-' && str[1] == '-' && strncmp(str + 2, o->longName, len) == 0 &&
            (str[len + 2] == '\0' || (o->has_arg && str[len + 2] == '=')))
        {
            matchlen = len;
            match = o;
            arg = str[len + 2] == '=' ? str + len + 3 : nullptr;
        }

        len = o->shortName != nullptr ? strlen(o->shortName) : 0;

        if (matchlen < len && str[0] == '-' && strncmp(str + 1, o->shortName, len) == 0 && (str[len + 1] == '\0' || o->has_arg))
        {
            matchlen = len;
            match = o;

            switch (str[len + 1])
            {
            case '\0':
                arg = nullptr;
                break;

            case '=':
                arg = str + len + 2;
                break;

            default:
                arg = str + len + 1;
                break;
            }
        }
    }

    if (setme_arg != nullptr)
    {
        *setme_arg = arg;
    }

    return match;
}

} // namespace

void tr_getopt_usage(char const* app_name, char const* description, struct tr_option const* opts)
{
    auto long_width = size_t{ 0 };
    auto short_width = size_t{ 0 };
    auto arg_width = size_t{ 0 };

    for (tr_option const* o = opts; o->val != 0; ++o)
    {
        maxWidth(o, long_width, short_width, arg_width);
    }

    auto const help = tr_option{ -1, "help", "Display this help page and exit", "h", false, nullptr };
    maxWidth(&help, long_width, short_width, arg_width);

    if (description == nullptr)
    {
        description = "Usage: %s [options]";
    }

    printf(description, app_name);
    printf("\n\nOptions:\n");
    getopts_usage_line(&help, long_width, short_width, arg_width);

    for (tr_option const* o = opts; o->val != 0; ++o)
    {
        getopts_usage_line(o, long_width, short_width, arg_width);
    }
}

int tr_getopt(char const* usage, int argc, char const* const* argv, tr_option const* opts, char const** setme_optarg)
{
    char const* arg = nullptr;
    tr_option const* o = nullptr;

    *setme_optarg = nullptr;

    /* handle the builtin 'help' option */
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            tr_getopt_usage(argv[0], usage, opts);
            exit(0);
        }
    }

    /* out of options? */
    if (argc == 1 || tr_optind >= argc)
    {
        return TR_OPT_DONE;
    }

    o = findOption(opts, argv[tr_optind], &arg);

    if (o == nullptr)
    {
        /* let the user know we got an unknown option... */
        *setme_optarg = argv[tr_optind++];
        return TR_OPT_UNK;
    }

    if (!o->has_arg)
    {
        /* no argument needed for this option, so we're done */
        if (arg != nullptr)
        {
            return TR_OPT_ERR;
        }

        *setme_optarg = nullptr;
        ++tr_optind;
        return o->val;
    }

    /* option needed an argument, and it was embedded in this string */
    if (arg != nullptr)
    {
        *setme_optarg = arg;
        ++tr_optind;
        return o->val;
    }

    /* throw an error if the option needed an argument but didn't get one */
    if (++tr_optind >= argc)
    {
        return TR_OPT_ERR;
    }

    if (findOption(opts, argv[tr_optind], nullptr) != nullptr)
    {
        return TR_OPT_ERR;
    }

    *setme_optarg = argv[tr_optind++];
    return o->val;
}
