/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2008 Transmission authors and contributors
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

#ifndef TR_CORE_H
#define TR_CORE_H

#include <string.h>

#include <glib-object.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include "conf.h" /* pref_flag_t */
#include "tr-torrent.h"

#define TR_CORE_TYPE ( tr_core_get_type( ) )
#define TR_CORE( o ) G_TYPE_CHECK_INSTANCE_CAST( ( o ), TR_CORE_TYPE,\
                                                TrCore )
#define TR_IS_CORE( o ) G_TYPE_CHECK_INSTANCE_TYPE( ( o ), TR_CORE_TYPE )
#define TR_CORE_CLASS( k ) G_TYPE_CHECK_CLASS_CAST( ( k ), TR_CORE_TYPE,\
                                                   TrCoreClass )
#define TR_IS_CORE_CLASS( k ) G_TYPE_CHECK_CLASS_TYPE( ( k ), TR_CORE_TYPE )
#define TR_CORE_GET_CLASS( o ) G_TYPE_INSTANCE_GET_CLASS( ( o ),\
                                                         TR_CORE_TYPE, \
                                                         TrCoreClass )

typedef struct TrCore
{
    GObject    parent;
    struct TrCorePrivate  * priv;
}
TrCore;

typedef struct TrCoreClass
{
    GObjectClass    parent;

    /* "blocklist-updated" signal with a callback type of
        void (*callback )( TrCore*, int ruleCount, gpointer userData ). */
    int blocklistSignal;

    /* "port-tested" signal with a callback type of
       void( *callback )( TrCore*, gboolean isOpen, gpointer userData ). */
    int portSignal;

    /* "error" signal with a callback type of
       void( *callback )( TrCore*, enum tr_core_err, const char * humanReadable, gpointer userData ). */
    int errsig;

    /* "add-torrent-prompt" signal with a callback type of
       void ( *callback)( TrCore *, gpointer ctor, gpointer userData )
       The handler assumes ownership of ctor and must free when done */
    int promptsig;

    /* "quit" signal:
       void handler( TrCore *, gpointer ) */
    int quitsig;

    /* "prefs-changed" signal:
       void handler( TrCore *, int, gpointer ) */
    int prefsig;
}
TrCoreClass;

enum tr_core_err
{
    TR_CORE_ERR_ADD_TORRENT_ERR  = TR_EINVALID,
    TR_CORE_ERR_ADD_TORRENT_DUP  = TR_EDUPLICATE,
    TR_CORE_ERR_NO_MORE_TORRENTS,  /* finished adding a batch */
    TR_CORE_ERR_SAVE_STATE         /* error saving state */
};

GType          tr_core_get_type( void );

TrCore *       tr_core_new( tr_session * );

void           tr_core_close( TrCore* );

/* Return the model used without incrementing the reference count */
GtkTreeModel * tr_core_model( TrCore * self );

tr_session *   tr_core_session( TrCore * self );

/******
*******
******/

/**
 * Load saved state and return number of torrents added.
 * May trigger one or more "error" signals with TR_CORE_ERR_ADD_TORRENT
 */
int      tr_core_load( TrCore * self,
                       gboolean forcepaused );

/**
 * Add a list of torrents.
 * This function assumes ownership of torrentFiles
 *
 * May pop up dialogs for each torrent if that preference is enabled.
 * May trigger one or more "error" signals with TR_CORE_ERR_ADD_TORRENT
 */
void     tr_core_add_list( TrCore *    self,
                           GSList *    torrentFiles,
                           pref_flag_t start,
                           pref_flag_t prompt,
                           gboolean    doNotify );

#define tr_core_add_list_defaults( c, l, doNotify ) \
    tr_core_add_list( c, l, PREF_FLAG_DEFAULT, PREF_FLAG_DEFAULT, doNotify )


/** @brief Add a torrent. */
gboolean tr_core_add_metainfo( TrCore      * core,
                               const char  * base64_metainfo,
                               gboolean    * setme_success,
                               GError     ** err );

/** Add a torrent. */
void tr_core_add_torrent( TrCore*, TrTorrent*, gboolean doNotify );

/** Present the main window */
gboolean tr_core_present_window( TrCore*, gboolean * setme_success, GError ** err ); 


/**
 * Notifies listeners that torrents have been added.
 * This should be called after one or more tr_core_add*() calls.
 */
void     tr_core_torrents_added( TrCore * self );

/******
*******
******/

/* we've gotten notice from RPC that a torrent has been destroyed;
   update our gui accordingly */
void  tr_core_torrent_destroyed( TrCore * self, int torrentId );

/* remove a torrent */
void  tr_core_remove_torrent( TrCore * self, TrTorrent * gtor, int deleteFiles );

/* update the model with current torrent status */
void  tr_core_update( TrCore * self );

/* emit the "quit" signal */
void  tr_core_quit( TrCore * self );

/**
***  Set a preference value, save the prefs file, and emit the "prefs-changed" signal
**/

void tr_core_set_pref     ( TrCore * self, const char * key, const char * val );
void tr_core_set_pref_bool( TrCore * self, const char * key, gboolean val );
void tr_core_set_pref_int ( TrCore * self, const char * key, int val );
void tr_core_set_pref_double( TrCore * self, const char * key, double val );

/**
***
**/

void tr_core_port_test( TrCore * core );

void tr_core_blocklist_update( TrCore * core );

void tr_core_exec( TrCore * core, const tr_benc * benc );

void tr_core_exec_json( TrCore * core, const char * json );


/**
***
**/

/* column names for the model used to store torrent information */
/* keep this in sync with the type array in tr_core_init() in tr_core.c */
enum
{
    MC_NAME,
    MC_NAME_COLLATED,
    MC_TORRENT,
    MC_TORRENT_RAW,
    MC_ACTIVITY,
    MC_ROW_COUNT
};

#endif
