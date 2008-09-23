/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h> /* exit() */
#include <string.h>

#include "tr-getopt.h"

#ifndef MAX
 #define MAX( a, b ) ( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )
#endif

int tr_optind = 1;

static const char*
getArgName( const tr_option * opt )
{
    char * arg;

    if( !opt->has_arg )
        arg = "";
    else if( opt->argName )
        arg = opt->argName;
    else
        arg = "<args>";

    return arg;
}

static void
getopts_usage_line( const tr_option * opt,
                    int               longWidth,
                    int               shortWidth,
                    int               argWidth )
{
    const char * longName   = opt->longName ? opt->longName : "";
    const char * shortName  = opt->shortName ? opt->shortName : "";
    const char * arg        = getArgName( opt );

    printf( "  -%-*s --%-*s %-*s  %s\n", shortWidth, shortName,
            longWidth, longName,
            argWidth, arg,
            opt->description );
}

static void
maxWidth( const struct tr_option * o,
          int *                    longWidth,
          int *                    shortWidth,
          int *                    argWidth )
{
    const char * arg;

    if( o->longName )
        *longWidth = MAX( *longWidth, (int)strlen( o->longName ) );

    if( o->shortName )
        *shortWidth = MAX( *shortWidth, (int)strlen( o->shortName ) );

    if( ( arg = getArgName( o ) ) )
        *argWidth = MAX( *argWidth, (int)strlen( arg ) );
}

void
tr_getopt_usage( const char *           progName,
                 const char *           description,
                 const struct tr_option opts[] )
{
    int                      longWidth = 0;
    int                      shortWidth = 0;
    int                      argWidth = 0;
    struct tr_option         help;
    const struct tr_option * o;

    for( o = opts; o->val; ++o )
        maxWidth( o, &longWidth, &shortWidth, &argWidth );

    help.val = -1;
    help.longName = "help";
    help.description = "Display this help page and exit";
    help.shortName = "h";
    help.has_arg = 0;
    maxWidth( &help, &longWidth, &shortWidth, &argWidth );

    if( description == NULL )
        description = "Usage: %s [options]";
    printf( description, progName );
    printf( "\n\nOptions:\n" );
    getopts_usage_line( &help, longWidth, shortWidth, argWidth );
    for( o = opts; o->val; ++o )
        getopts_usage_line( o, longWidth, shortWidth, argWidth );
}

static const tr_option *
findOption( const tr_option * opts,
            const char *      str,
            const char **     setme_arg )
{
    size_t            matchlen = 0;
    const char *      arg = NULL;
    const tr_option * o;
    const tr_option * match = NULL;

    /* find the longest matching option */
    for( o = opts; o->val; ++o )
    {
        size_t len = o->longName ? strlen( o->longName ) : 0;

        if( ( matchlen < len ) && !memcmp( str, "--", 2 )
          && !memcmp( str + 2, o->longName, len )
          && ( str[len + 2] == '\0' || ( o->has_arg && str[len + 2] == '=' ) ) )
        {
            matchlen = len;
            match = o;
            arg = str[len + 2] == '=' ? str + len + 3 : NULL;
        }

        len = o->shortName ? strlen( o->shortName ) : 0;

        if( ( matchlen < len ) && !memcmp( str, "-", 1 )
          && !memcmp( str + 1, o->shortName, len )
          && ( str[len + 1] == '\0' || o->has_arg ) )
        {
            matchlen = len;
            match = o;
            switch( str[len + 1] )
            {
                case '\0':
                    arg = NULL;          break;

                case '=':
                    arg = str + len + 2; break;

                default:
                    arg = str + len + 1; break;
            }
        }
    }

    if( setme_arg )
        *setme_arg = arg;

    return match;
}

int
tr_getopt( const char *      usage,
           int               argc,
           const char **     argv,
           const tr_option * opts,
           const char **     setme_optarg )
{
    int               i;
    const char *      arg = NULL;
    const tr_option * o = NULL;

    *setme_optarg = NULL;

    /* handle the builtin 'help' option */
    for( i = 1; i < argc; ++i )
    {
        if( !strcmp( argv[i], "-h" ) || !strcmp( argv[i], "--help" ) )
        {
            tr_getopt_usage( argv[0], usage, opts );
            exit( 0 );
        }
    }

    /* out of options? */
    if( argc == 1 || tr_optind >= argc )
        return TR_OPT_DONE;

    o = findOption( opts, argv[tr_optind], &arg );
    if( !o )
    {
        /* let the user know we got an unknown option... */
        *setme_optarg = argv[tr_optind++];
        return TR_OPT_UNK;
    }

    if( !o->has_arg )
    {
        /* no argument needed for this option, so we're done */
        if( arg )
            return TR_OPT_ERR;
        *setme_optarg = NULL;
        ++tr_optind;
        return o->val;
    }

    /* option needed an argument, and it was embedded in this string */
    if( arg )
    {
        *setme_optarg = arg;
        ++tr_optind;
        return o->val;
    }

    /* throw an error if the option needed an argument but didn't get one */
    if( ++tr_optind >= argc )
        return TR_OPT_ERR;
    if( findOption( opts, argv[tr_optind], NULL ) )
        return TR_OPT_ERR;

    *setme_optarg = argv[tr_optind++];
    return o->val;
}

