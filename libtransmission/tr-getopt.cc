// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstdlib> /* exit() */
#include <cstring>
#include <string_view>

#include "transmission.h"
#include "tr-getopt.h"
#include "tr-macros.h"
#include "utils.h"

int tr_optind = 1;

static char const* getArgName(tr_option const* opt)
{
    if (!opt->has_arg)
    {
        return "";
    }

    if (opt->argName != nullptr)
    {
        return opt->argName;
    }

    return "<args>";
}

static size_t get_next_line_len(std::string_view description, size_t maxlen)
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

static void getopts_usage_line(tr_option const* opt, int longWidth, int shortWidth, int argWidth)
{
    char const* longName = opt->longName != nullptr ? opt->longName : "";
    char const* shortName = opt->shortName != nullptr ? opt->shortName : "";
    char const* arg = getArgName(opt);

    int const d_indent = shortWidth + longWidth + argWidth + 7;
    int const d_width = 80 - d_indent;
    auto d = std::string_view{ opt->description };

    printf(
        " %s%-*s %s%-*s %-*s ",
        tr_str_is_empty(shortName) ? " " : "-",
        TR_ARG_TUPLE(shortWidth, shortName),
        tr_str_is_empty(longName) ? "  " : "--",
        TR_ARG_TUPLE(longWidth, longName),
        TR_ARG_TUPLE(argWidth, arg));

    auto const strip_leading_whitespace = [](std::string_view text)
    {
        if (auto pos = text.find_first_not_of(' '); pos != std::string_view::npos)
        {
            text.remove_prefix(pos);
        }

        return text;
    };

    int len = get_next_line_len(d, d_width);
    printf("%*.*s\n", TR_ARG_TUPLE(len, len, std::data(d)));
    d.remove_prefix(len);
    d = strip_leading_whitespace(d);

    while ((len = get_next_line_len(d, d_width)) != 0)
    {
        printf("%*.*s%*.*s\n", TR_ARG_TUPLE(d_indent, d_indent, ""), TR_ARG_TUPLE(len, len, std::data(d)));
        d.remove_prefix(len);
        d = strip_leading_whitespace(d);
    }
}

static void maxWidth(struct tr_option const* o, size_t& longWidth, size_t& shortWidth, size_t& argWidth)
{
    if (o->longName != nullptr)
    {
        longWidth = std::max(longWidth, strlen(o->longName));
    }

    if (o->shortName != nullptr)
    {
        shortWidth = std::max(shortWidth, strlen(o->shortName));
    }

    char const* const arg = getArgName(o);
    if (arg != nullptr)
    {
        argWidth = std::max(argWidth, strlen(arg));
    }
}

void tr_getopt_usage(char const* progName, char const* description, struct tr_option const opts[])
{
    auto longWidth = size_t{ 0 };
    auto shortWidth = size_t{ 0 };
    auto argWidth = size_t{ 0 };

    for (tr_option const* o = opts; o->val != 0; ++o)
    {
        maxWidth(o, longWidth, shortWidth, argWidth);
    }

    auto const help = tr_option{ -1, "help", "Display this help page and exit", "h", false, nullptr };
    maxWidth(&help, longWidth, shortWidth, argWidth);

    if (description == nullptr)
    {
        description = "Usage: %s [options]";
    }

    printf(description, progName);
    printf("\n\nOptions:\n");
    getopts_usage_line(&help, longWidth, shortWidth, argWidth);

    for (tr_option const* o = opts; o->val != 0; ++o)
    {
        getopts_usage_line(o, longWidth, shortWidth, argWidth);
    }
}

static tr_option const* findOption(tr_option const* opts, char const* str, char const** setme_arg)
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
