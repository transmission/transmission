/*
  Copyright (c) 2005 Joshua Elsasser. All rights reserved.
   
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   
  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef TG_PREFS_H
#define TG_PREFS_H

#include "transmission.h"

/* macros for names of prefs we use */
#define PREF_PORT               "listening-port"
#define PREF_USELIMIT           "use-upload-limit"
#define PREF_LIMIT              "upload-limit"
#define PREF_DIR                "download-directory"

#define DEFAULT_UPLIMIT         20

typedef gboolean (*add_torrent_func_t)(tr_handle_t*, GtkWindow*, const char*, const char *, gboolean);
typedef void (*torrents_added_func_t)(void *);

void
makeprefwindow(GtkWindow *parent, tr_handle_t *tr);

/* set the upload limit based on saved prefs */
void
setlimit(tr_handle_t *tr);

/* show the "add a torrent" dialog */
void
makeaddwind(add_torrent_func_t addfunc, GtkWindow *parent, tr_handle_t *tr,
            torrents_added_func_t donefunc, void *donedata);

#endif /* TG_PREFS_H */
