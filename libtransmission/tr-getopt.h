/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * $Id:$
 */

#ifndef TR_GETOPT_H
#define TR_GETOPT_H

extern int tr_optind;

typedef struct tr_option
{
    int     val;          /* the value to return from tr_getopt() */
    char *  longName;     /* --long-form */
    char *  description;  /* option's description for tr_getopt_usage() */
    char *  shortName;    /* short form */
    int     has_arg;      /* 0 for no argument, 1 for argument */
    char *  argName;      /* argument's description for tr_getopt_usage() */
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

#endif /* TR_GETOPT_H */
