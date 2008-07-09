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

#include <stdio.h>
#include <stdlib.h> /* exit() */
#include <string.h>

#include "tr-getopt.h"

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
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
                    int longWidth, int shortWidth, int argWidth )
{
    const char * longName   = opt->longName ? opt->longName : "";
    const char * shortName  = opt->shortName ? opt->shortName : "";
    const char * arg        = getArgName( opt );
    printf( "  -%*s, --%-*s %-*s  %s\n", shortWidth, shortName,
                                         longWidth, longName,
                                         argWidth, arg,
                                         opt->description );
}

static void
maxWidth( const struct tr_option * o,
          int * longWidth, int * shortWidth, int * argWidth )
{
    const char * arg;

    if( o->longName ) 
        *longWidth = MAX( *longWidth, (int)strlen( o->longName ) );

    if( o->shortName )
        *shortWidth = MAX( *shortWidth, (int)strlen( o->shortName ) );

    if(( arg = getArgName( o )))
        *argWidth = MAX( *argWidth, (int)strlen( arg ) );
}

void
tr_getopt_usage( const char              * progName,
                 const char              * description,
                 const struct tr_option    opts[] )
{
    int longWidth = 0;
    int shortWidth = 0;
    int argWidth = 0;
    struct tr_option help;
    const struct tr_option * o;

    for( o=opts; o->val; ++o )
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
    for( o=opts; o->val; ++o )
        getopts_usage_line( o, longWidth, shortWidth, argWidth );
}

static const tr_option *
findOption( const tr_option   * opts,
            const char        * str,
            const char       ** nested )
{
    size_t len;
    const tr_option * o;

    for( o=opts; o->val; ++o )
    {
        if( o->longName && (str[0]=='-') && (str[1]=='-') ) {
            if( !strcmp( o->longName, str+2 ) ) {
                if( nested ) *nested = NULL;
                return o;
            }
            len = strlen( o->longName );
            if( !memcmp( o->longName, str+2, len ) && str[len+2]=='=' ) {
                if( nested ) *nested = str+len+3;
                return o;
            }
        }
        
        if( o->shortName && (str[0]=='-') ) {
            if( !strcmp( o->shortName, str+1 ) ) {
                if( nested ) *nested = NULL;
                return o;
            }
            len = strlen( o->shortName );
            if( !memcmp( o->shortName, str+1, len ) && str[len+1]=='=' ) {
                if( nested ) *nested = str+len+2;
                return o;
            }
        }
    }

    return NULL;
}

int
tr_getopt( const char        * usage,
           int                 argc,
           const char       ** argv,
           const tr_option   * opts,
           const char       ** setme_optarg )
{
    int i;
    const char * nest = NULL;
    const tr_option * o = NULL;

    *setme_optarg = NULL;  
  
    /* handle the builtin 'help' option */
    for( i=1; i<argc; ++i ) {
        if( !strcmp(argv[i], "-h") || !strcmp(argv[i], "--help" ) ) {
            tr_getopt_usage( argv[0], usage, opts );
            exit( 0 );
        }
    }

    /* out of options? */
    if( argc==1 || tr_optind>=argc )
        return TR_OPT_DONE;

    o = findOption( opts, argv[tr_optind], &nest );
    if( !o ) {
        /* let the user know we got an unknown option... */
        *setme_optarg = argv[tr_optind++];
        return TR_OPT_UNK;
    }

    if( !o->has_arg ) {
        /* no argument needed for this option, so we're done */
        if( nest )
            return TR_OPT_ERR;
        *setme_optarg = NULL;
        tr_optind++;
        return o->val;
    }

    /* option needed an argument, and it was nested in this string */
    if( nest ) {
        *setme_optarg = nest;
        tr_optind++;
        return o->val;
    }

    /* throw an error if the option needed an argument but didn't get one */
    if( ++tr_optind >= argc )
        return TR_OPT_ERR;
    if( findOption( opts, argv[tr_optind], NULL ))
        return TR_OPT_ERR;

    *setme_optarg = argv[tr_optind++];
    return o->val;
}
