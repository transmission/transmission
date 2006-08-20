/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#ifndef TG_CONF_H
#define TG_CONF_H

#include "transmission.h"
#include "bencode.h"

gboolean
cf_init(const char *confdir, char **errstr);
gboolean
cf_lock(char **errstr);
char *
cf_sockname(void);
void
cf_loadprefs(char **errstr);
const char *
cf_getpref(const char *name);
void
cf_setpref(const char *name, const char *value);
void
cf_saveprefs(char **errstr);
benc_val_t *
cf_loadstate(char **errstr);
void
cf_savestate(benc_val_t *state, char **errstr);
void
cf_freestate(benc_val_t *state);

/* macros for names of prefs we use */
#define PREF_PORT               "listening-port"
#define PREF_USEDOWNLIMIT       "use-download-limit"
#define PREF_DOWNLIMIT          "download-limit"
#define PREF_USEUPLIMIT         "use-upload-limit"
#define PREF_UPLIMIT            "upload-limit"
#define PREF_DIR                "download-directory"
#define PREF_ADDSTD             "add-behavior-standard"
#define PREF_ADDIPC             "add-behavior-ipc"
#define PREF_MSGLEVEL           "message-level"

#endif /* TG_CONF_H */
