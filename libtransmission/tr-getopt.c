/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <ctype.h> /* isspace() */
#include <stdio.h>
#include <stdlib.h> /* exit() */
#include <string.h>

#include "transmission.h"
#include "tr-getopt.h"
#include "tr-macros.h"
#include "utils.h"

int tr_optind = 1;

static char const* getArgName(tr_option const* opt)
{
    char const* arg;

    if (!opt->has_arg)
    {
        arg = "";
    }
    else if (opt->argName != NULL)
    {
        arg = opt->argName;
    }
    else
    {
        arg = "<args>";
    }

    return arg;
}

static int get_next_line_len(char const* description, int maxlen)
{
    int end;
    int len = strlen(description);

    if (len < maxlen)
    {
        return len;
    }

    end = maxlen < len ? maxlen : len;

    while (end > 0 && !isspace(description[end]))
    {
        --end;
    }

    return end != 0 ? end : len;
}

static void getopts_usage_line(tr_option const* opt, int longWidth, int shortWidth, int argWidth)
{
    int len;
    char const* longName = opt->longName != NULL ? opt->longName : "";
    char const* shortName = opt->shortName != NULL ? opt->shortName : "";
    char const* arg = getArgName(opt);

    int const d_indent = shortWidth + longWidth + argWidth + 7;
    int const d_width = 80 - d_indent;
    char const* d = opt->description;

    printf(" %s%-*s %s%-*s %-*s ", tr_str_is_empty(shortName) ? " " : "-", shortWidth, shortName,
        tr_str_is_empty(longName) ? "  " : "--", longWidth, longName, argWidth, arg);
    len = get_next_line_len(d, d_width);
    printf("%*.*s\n", len, len, d);

    d += len;

    while (isspace(*d))
    {
        ++d;
    }

    while ((len = get_next_line_len(d, d_width)) != 0)
    {
        printf("%*.*s%*.*s\n", d_indent, d_indent, "", len, len, d);
        d += len;

        while (isspace(*d))
        {
            ++d;
        }
    }
}

static void maxWidth(struct tr_option const* o, int* longWidth, int* shortWidth, int* argWidth)
{
    char const* arg;

    if (o->longName != NULL)
    {
        *longWidth = MAX(*longWidth, (int)strlen(o->longName));
    }

    if (o->shortName != NULL)
    {
        *shortWidth = MAX(*shortWidth, (int)strlen(o->shortName));
    }

    if ((arg = getArgName(o)) != NULL)
    {
        *argWidth = MAX(*argWidth, (int)strlen(arg));
    }
}

void tr_getopt_usage(char const* progName, char const* description, struct tr_option const opts[])
{
    int longWidth = 0;
    int shortWidth = 0;
    int argWidth = 0;
    struct tr_option help;

    for (tr_option const* o = opts; o->val != 0; ++o)
    {
        maxWidth(o, &longWidth, &shortWidth, &argWidth);
    }

    help.val = -1;
    help.longName = "help";
    help.description = "Display this help page and exit";
    help.shortName = "h";
    help.has_arg = false;
    maxWidth(&help, &longWidth, &shortWidth, &argWidth);

    if (description == NULL)
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
    char const* arg = NULL;
    tr_option const* match = NULL;

    /* find the longest matching option */
    for (tr_option const* o = opts; o->val != 0; ++o)
    {
        size_t len = o->longName != NULL ? strlen(o->longName) : 0;

        if (matchlen < len && str[0] == '-' && str[1] == '-' && strncmp(str + 2, o->longName, len) == 0 &&
            (str[len + 2] == '\0' || (o->has_arg && str[len + 2] == '=')))
        {
            matchlen = len;
            match = o;
            arg = str[len + 2] == '=' ? str + len + 3 : NULL;
        }

        len = o->shortName != NULL ? strlen(o->shortName) : 0;

        if (matchlen < len && str[0] == '-' && strncmp(str + 1, o->shortName, len) == 0 && (str[len + 1] == '\0' || o->has_arg))
        {
            matchlen = len;
            match = o;

            switch (str[len + 1])
            {
            case '\0':
                arg = NULL;
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

    if (setme_arg != NULL)
    {
        *setme_arg = arg;
    }

    return match;
}

int tr_getopt(char const* usage, int argc, char const* const* argv, tr_option const* opts, char const** setme_optarg)
{
    char const* arg = NULL;
    tr_option const* o = NULL;

    *setme_optarg = NULL;

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

    if (o == NULL)
    {
        /* let the user know we got an unknown option... */
        *setme_optarg = argv[tr_optind++];
        return TR_OPT_UNK;
    }

    if (!o->has_arg)
    {
        /* no argument needed for this option, so we're done */
        if (arg != NULL)
        {
            return TR_OPT_ERR;
        }

        *setme_optarg = NULL;
        ++tr_optind;
        return o->val;
    }

    /* option needed an argument, and it was embedded in this string */
    if (arg != NULL)
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

    if (findOption(opts, argv[tr_optind], NULL) != NULL)
    {
        return TR_OPT_ERR;
    }

    *setme_optarg = argv[tr_optind++];
    return o->val;
}
