/* getopts.c - Command line argument parser
 *
 * Whom: Steve Mertz <steve@dragon-ware.com>
 * Date: 20010111
 * Why:  Because I couldn't find one that I liked. So I wrote this one.
 *
*/
/*
 * Copyright (c) 2001-2004 Steve Mertz <steve@dragon-ware.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this 
 * list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of Dragon Ware nor the names of its contributors may be 
 * used to endorse or promote products derived from this software without 
 * specific prior written permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS 
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 *
*/
#include <stdio.h>
#include <stdlib.h> /* exit() */
#include <string.h>

#include "getopts.h"

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

int option_index = 1;

static const char*
getArgName( const struct options * opt )
{
    if( !opt->args )   return "";
    if( opt->argName ) return opt->argName;
    return "<args>";
}

static void
getopts_usage_line( const struct options * opt, int nameWidth, int shortWidth, int argWidth )
{
    const char * name       = opt->name ? opt->name : "";
    const char * shortName  = opt->shortName ? opt->shortName : "";
    const char * arg        = getArgName( opt );
    printf( "  -%*s, --%-*s %-*s  %s\n", shortWidth, shortName, nameWidth, name, argWidth, arg, opt->description );
}

void
getopts_usage( const char           * progName,
               const char           * description,
               const struct options   opts[] )
{
  int count;
  int nameWidth = 0;
  int shortWidth = 0;
  int argWidth = 0;
  struct options help;

  for( count=0; opts[count].description; ++count )
  {
    const char * arg;

    if( opts[count].name ) 
      nameWidth = MAX( nameWidth, (int)strlen( opts[count].name ) );

    if( opts[count].shortName )
      shortWidth = MAX( shortWidth, (int)strlen( opts[count].shortName ) );

    if(( arg = getArgName( &opts[count] )))
      argWidth = MAX( argWidth, (int)strlen( arg ) );
  }

  if( !description )
    description = "Usage: %s [options]";
  printf( description, progName );
  printf( "\n\n" );
  printf( "Usage:\n" );

  help.number = -1;
  help.name = "help";
  help.description = "Display this help page and exit";
  help.shortName = "h";
  help.args = 0;
  getopts_usage_line( &help, nameWidth, shortWidth, argWidth );
  
  for( count=0; opts[count].description; ++count )
      getopts_usage_line( &opts[count], nameWidth, shortWidth, argWidth );
}

static const struct options *
findOption( const struct options  * opts,
            const char            * str,
            const char           ** nested )
{
    size_t len;
    const struct options * o;

    for( o=opts; o->number; ++o )
    {
        if( o->name && (str[0]=='-') && (str[1]=='-') ) {
            if( !strcmp( o->name, str+2 ) ) {
                if( nested ) *nested = NULL;
                return o;
            }
            len = strlen( o->name );
            if( !memcmp( o->name, str+2, len ) && str[len+2]=='=' ) {
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
showUsageAndExit( const char           * appName,
                  const char           * description,
                  const struct options * opts )
{
    getopts_usage( appName, description, opts );
    exit( 0 );
}


/*
 * Returns: 0  - No arguments to parse
 *          #  - The number in the struct for the matched arg.
 *         -2  - Unrecognized option
 */
int
getopts( const char             * usage,
         int                      argc,
         const char            ** argv,
         const struct options   * opts,
         const char            ** setme_optarg )
{
    int i;
    const char * nest = NULL;
    const struct options * o = NULL;

    *setme_optarg = NULL;  

    if( argc==1 || argc==option_index )
        return 0;
  
    /* handle the builtin 'help' option */
    for( i=1; i<argc; ++i )
        if( !strcmp(argv[i], "-h") || !strcmp(argv[i], "--help" ) )
            showUsageAndExit( argv[0], usage, opts );

    /* out of options */
    if( option_index >= argc )
        return 0;

    o = findOption( opts, argv[option_index], &nest );
    if( !o ) {
        /* let the user know we got an unknown option... */
        *setme_optarg = argv[option_index++];
        return -2;
    }

    if( !o->args ) {
        /* no argument needed for this option, so we're done */
        if( nest )
            showUsageAndExit( argv[0], usage, opts );
        *setme_optarg = NULL;
        option_index++;
        return o->number;
    }

    /* option needed an argument, and it was nested in this string */
    if( nest ) {
        *setme_optarg = nest;
        option_index++;
        return o->number;
    }

    /* throw an error if the option needed an argument but didn't get one */
    if( ++option_index >= argc )
        showUsageAndExit( argv[0], usage, opts );
    if( findOption( opts, argv[option_index], NULL ))
        showUsageAndExit( argv[0], usage, opts );

    *setme_optarg = argv[option_index++];
    return o->number;
}
