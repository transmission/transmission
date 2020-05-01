/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup utils Utilities
 * @{
 */

/** @brief Similar to optind, this is the current index into argv */
extern int tr_optind;

typedef struct tr_option
{
    int val; /* the value to return from tr_getopt() */
    char const* longName; /* --long-form */
    char const* description; /* option's description for tr_getopt_usage() */
    char const* shortName; /* short form */
    bool has_arg; /* 0 for no argument, 1 for argument */
    char const* argName; /* argument's description for tr_getopt_usage() */
}
tr_option;

enum
{
    /* all options have been processed */
    TR_OPT_DONE = 0,
    /* a syntax error was detected, such as a missing
     * argument for an option that requires one */
    TR_OPT_ERR = -1,
    /* an unknown option was reached */
    TR_OPT_UNK = -2
};

/**
 * @brief similar to getopt()
 * @return TR_GETOPT_DONE, TR_GETOPT_ERR, TR_GETOPT_UNK, or the matching tr_option's `val' field
 */
int tr_getopt(char const* summary, int argc, char const* const* argv, tr_option const* opts, char const** setme_optarg);

/** @brief prints the `Usage' help section to stdout */
void tr_getopt_usage(char const* appName, char const* description, tr_option const* opts);

#ifdef __cplusplus
} /* extern "C" */
#endif

/** @} */
