/*
  Copyright (c) 2005-2006 Joshua Elsasser. All rights reserved.
   
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

#include "tr_backend.h"
#include "tr_torrent.h"
#include "util.h"

/* macros for names of prefs we use */
#define PREF_PORT               "listening-port"
#define PREF_USEDOWNLIMIT       "use-download-limit"
#define PREF_DOWNLIMIT          "download-limit"
#define PREF_USEUPLIMIT         "use-upload-limit"
#define PREF_UPLIMIT            "upload-limit"
#define PREF_DIR                "download-directory"

/* default values for a couple prefs */
#define DEF_DOWNLIMIT           100
#define DEF_USEDOWNLIMIT        FALSE
#define DEF_UPLIMIT             20
#define DEF_USEUPLIMIT          TRUE

void
makeprefwindow(GtkWindow *parent, TrBackend *back, gboolean *opened);

/* set the upload limit based on saved prefs */
void
setlimit(TrBackend *back);

/* show the "add a torrent" dialog */
void
makeaddwind(GtkWindow *parent, add_torrents_func_t addfunc, void *cbdata);

/* show the info window for a torrent */
void
makeinfowind(GtkWindow *parent, TrTorrent *tor);

#endif /* TG_PREFS_H */
