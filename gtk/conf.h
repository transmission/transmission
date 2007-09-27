/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

/**
***
**/

#ifndef TG_CONF_H
#define TG_CONF_H

int       pref_int_get            ( const char * key );
void      pref_int_set            ( const char * key, int value );
void      pref_int_set_default    ( const char * key, int default_value );

gboolean  pref_flag_get            ( const char * key );
void      pref_flag_set            ( const char * key, gboolean value );
void      pref_flag_set_default    ( const char * key, gboolean default_value );

char*     pref_string_get          ( const char * key );
void      pref_string_set          ( const char * key, const char * value );
void      pref_string_set_default  ( const char * key, const char * default_value );

void      pref_save                ( char **errstr );

/**
***
**/

struct benc_val_s;

gboolean
cf_init(const char *confdir, char **errstr);
gboolean
cf_lock(char **errstr);
char *
cf_sockname(void);
void
cf_loadprefs(char **errstr);
struct benc_val_s *
cf_loadstate(char **errstr);
void
cf_savestate(struct benc_val_s *state, char **errstr);
void
cf_freestate(struct benc_val_s *state);

#endif /* TG_CONF_H */
