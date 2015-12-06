/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
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

/** @brief Similar to optind, this is the current index into argv */
extern int tr_optind;

typedef struct tr_option
{
    int           val;          /* the value to return from tr_getopt () */
    const char *  longName;     /* --long-form */
    const char *  description;  /* option's description for tr_getopt_usage () */
    const char *  shortName;    /* short form */
    int           has_arg;      /* 0 for no argument, 1 for argument */
    const char *  argName;      /* argument's description for tr_getopt_usage () */
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
 * @brief similar to getopt ()
 * @return TR_GETOPT_DONE, TR_GETOPT_ERR, TR_GETOPT_UNK, or the matching tr_option's `val' field
 */
int  tr_getopt (const char          * summary,
                int                   argc,
                const char * const  * argv,
                const tr_option     * opts,
                const char         ** setme_optarg);

/** @brief prints the `Usage' help section to stdout */
void tr_getopt_usage (const char       * appName,
                      const char       * description,
                      const tr_option  * opts);

#ifdef __cplusplus
} /* extern "C" */
#endif

/** @} */

#endif /* TR_GETOPT_H */
