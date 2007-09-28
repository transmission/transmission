/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#ifndef TR_PREFS_H
#define TR_PREFS_H

#include <gtk/gtk.h>

GtkWidget * tr_prefs_dialog_new( GObject * core, GtkWindow * parent );

/* if you add a key here,  you /must/ add its
 * default in tr_prefs_init_global( void ) */

#define PREF_KEY_DL_LIMIT_ENABLED  "download-limit-enabled"
#define PREF_KEY_DL_LIMIT          "download-limit"
#define PREF_KEY_UL_LIMIT_ENABLED  "upload-limit-enabled"
#define PREF_KEY_UL_LIMIT          "upload-limit"
#define PREF_KEY_DIR_ASK           "prompt-for-download-directory"
#define PREF_KEY_DIR_DEFAULT       "default-download-directory"
#define PREF_KEY_ADDSTD            "add-behavior-standard"
#define PREF_KEY_ADDIPC            "add-behavior-ipc"
#define PREF_KEY_PORT              "listening-port"
#define PREF_KEY_NAT               "nat-traversal-enabled"
#define PREF_KEY_PEX               "pex-enabled"
#define PREF_KEY_SYSTRAY           "system-tray-icon-enabled"
#define PREF_KEY_ASKQUIT           "prompt-before-exit"
#define PREF_KEY_ENCRYPTED_ONLY    "encrypted-connections-only"
#define PREF_KEY_MSGLEVEL          "debug-message-level"

void tr_prefs_init_global( void );

int  tr_prefs_get_action( const char * key );
void tr_prefs_set_action( const char * key, int action );

#endif
