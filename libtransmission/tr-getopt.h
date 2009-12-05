/*
 * This file Copyright (C) 2008-2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_GETOPT_H
#define TR_GETOPT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup utils Utilities
 * @{
 */

extern int tr_optind;

typedef struct tr_option
{
    int           val;          /* the value to return from tr_getopt() */
    const char *  longName;     /* --long-form */
    const char *  description;  /* option's description for tr_getopt_usage() */
    const char *  shortName;    /* short form */
    int           has_arg;      /* 0 for no argument, 1 for argument */
    const char *  argName;      /* argument's description for tr_getopt_usage() */
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
 * @return TR_GETOPT_DONE, TR_GETOPT_ERR, TR_GETOPT_UNK,
 *         or the matching tr_option's `val' field
 */
int  tr_getopt( const char *      summary,
                int               argc,
                const char **     argv,
                const tr_option * opts,
                const char **     setme_optarg );

void tr_getopt_usage( const char *      appName,
                      const char *      description,
                      const tr_option * opts );

#ifdef __cplusplus
} /* extern "C" */
#endif

/** @} */

#endif /* TR_GETOPT_H */
