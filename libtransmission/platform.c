/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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
 *****************************************************************************/

#include "platform.h"

#ifdef SYS_BEOS
/***********************************************************************
 * tr_init_beos
 ***********************************************************************
 * Puts the prefsDirectory in the right place.
 **********************************************************************/
void tr_init_beos( tr_handle_t * h )
{
	int32 length = 0;
	char path[B_FILE_NAME_LENGTH];
	
	find_directory( B_USER_SETTINGS_DIRECTORY, dev_for_path("/boot"),
	                true, path, length );
	
	snprintf( h->prefsDirectory, B_FILE_NAME_LENGTH,
	          "%s/Transmission", path );
	mkdir( h->prefsDirectory, 0755 );
}
#endif

/***********************************************************************
 * tr_init_platform
 ***********************************************************************
 * Setup the prefsDirectory for the current platform.
 **********************************************************************/
void tr_init_platform( tr_handle_t *h )
{
#ifdef SYS_BEOS
	tr_init_beos( h );
#else
    snprintf( h->prefsDirectory, sizeof( h->prefsDirectory ),
              "%s/.transmission", getenv( "HOME" ) );
    mkdir( h->prefsDirectory, 0755 );
#endif
}
