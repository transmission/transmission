/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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

int option_index = 1;

static const char*
getArgName( const tr_option * opt )
{
    if( !opt->has_arg )   return "";
    if( opt->argName ) return opt->argName;
    return "<args>";
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

void
tr_getopt_usage( const char             * progName,
                 const char             * description,
                 const struct tr_option    opts[] )
{
  int count;
  int longWidth = 0;
  int shortWidth = 0;
  int argWidth = 0;
  struct tr_option help;

  for( count=0; opts[count].description; ++count )
  {
    const char * arg;

    if( opts[count].longName ) 
      longWidth = MAX( longWidth, (int)strlen( opts[count].longName ) );

    if( opts[count].shortName )
      shortWidth = MAX( shortWidth, (int)strlen( opts[count].shortName ) );

    if(( arg = getArgName( &opts[count] )))
      argWidth = MAX( argWidth, (int)strlen( arg ) );
  }

  if( !description )
    description = "Usage: %s [options]";
  printf( description, progName );
  printf( "\n\n" );
  printf( "Options:\n" );

  help.val = -1;
  help.longName = "help";
  help.description = "Display this help page and exit";
  help.shortName = "h";
  help.has_arg = 0;
  getopts_usage_line( &help, longWidth, shortWidth, argWidth );
  
  for( count=0; opts[count].description; ++count )
      getopts_usage_line( &opts[count], longWidth, shortWidth, argWidth );
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

static void
showUsageAndExit( const char        * appName,
                  const char        * description,
                  const tr_option   * opts )
{
    tr_getopt_usage( appName, description, opts );
    exit( 0 );
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

    if( argc==1 || argc==option_index )
        return TR_OPT_DONE;
  
    /* handle the builtin 'help' option */
    for( i=1; i<argc; ++i )
        if( !strcmp(argv[i], "-h") || !strcmp(argv[i], "--help" ) )
            showUsageAndExit( argv[0], usage, opts );

    /* out of options */
    if( option_index >= argc )
        return TR_OPT_DONE;

    o = findOption( opts, argv[option_index], &nest );
    if( !o ) {
        /* let the user know we got an unknown option... */
        *setme_optarg = argv[option_index++];
        return TR_OPT_UNK;
    }

    if( !o->has_arg ) {
        /* no argument needed for this option, so we're done */
        if( nest )
            return TR_OPT_ERR;
        *setme_optarg = NULL;
        option_index++;
        return o->val;
    }

    /* option needed an argument, and it was nested in this string */
    if( nest ) {
        *setme_optarg = nest;
        option_index++;
        return o->val;
    }

    /* throw an error if the option needed an argument but didn't get one */
    if( ++option_index >= argc )
        return TR_OPT_ERR;
    if( findOption( opts, argv[option_index], NULL ))
        return TR_OPT_ERR;

    *setme_optarg = argv[option_index++];
    return o->val;
}
