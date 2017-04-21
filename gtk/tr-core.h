/******************************************************************************
 * Copyright (c) Transmission authors and contributors
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

#pragma once

#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

G_BEGIN_DECLS

#define TR_CORE_TYPE (tr_core_get_type())
#define TR_CORE(o) (G_TYPE_CHECK_INSTANCE_CAST((o), TR_CORE_TYPE, TrCore))

#define TR_RESOURCE_PATH "/com/transmissionbt/transmission/"

typedef struct _TrCore
{
    GObject parent;

    /*< private >*/
    struct TrCorePrivate* priv;
}
TrCore;

enum tr_core_err
{
    TR_CORE_ERR_ADD_TORRENT_ERR = TR_PARSE_ERR,
    TR_CORE_ERR_ADD_TORRENT_DUP = TR_PARSE_DUPLICATE,
    TR_CORE_ERR_NO_MORE_TORRENTS = 1000 /* finished adding a batch */
};

typedef struct _TrCoreClass
{
    GObjectClass parent_class;

    void (* add_error)(TrCore*, enum tr_core_err, char const* name);
    void (* add_prompt)(TrCore*, gpointer ctor);
    void (* blocklist_updated)(TrCore*, int ruleCount);
    void (* busy)(TrCore*, gboolean is_busy);
    void (* prefs_changed)(TrCore*, tr_quark const key);
    void (* port_tested)(TrCore*, gboolean is_open);
    void (* quit)(TrCore*);
}
TrCoreClass;

GType tr_core_get_type(void) G_GNUC_CONST;

TrCore* gtr_core_new(tr_session*);

tr_session* gtr_core_close(TrCore*);

/* Return the model used without incrementing the reference count */
GtkTreeModel* gtr_core_model(TrCore* self);

void gtr_core_clear(TrCore* self);

tr_session* gtr_core_session(TrCore* self);

size_t gtr_core_get_active_torrent_count(TrCore* self);

size_t gtr_core_get_torrent_count(TrCore* self);

tr_torrent* gtr_core_find_torrent(TrCore* core, int id);

void gtr_core_pref_changed(TrCore* core, tr_quark const key);

/******
*******
******/

/**
 * Load saved state and return number of torrents added.
 * May trigger one or more "error" signals with TR_CORE_ERR_ADD_TORRENT
 */
void gtr_core_load(TrCore* self, gboolean forcepaused);

/**
 * Add a list of torrents.
 * This function assumes ownership of torrentFiles
 *
 * May pop up dialogs for each torrent if that preference is enabled.
 * May trigger one or more "error" signals with TR_CORE_ERR_ADD_TORRENT
 */
void gtr_core_add_files(TrCore* core, GSList* files, gboolean do_start, gboolean do_prompt, gboolean do_notify);

/** @brief Add a torrent from a URL */
bool gtr_core_add_from_url(TrCore* core, char const* url);

/** @brief Add a torrent.
    @param ctor this function assumes ownership of the ctor */
void gtr_core_add_ctor(TrCore* core, tr_ctor* ctor);

/** Add a torrent. */
void gtr_core_add_torrent(TrCore*, tr_torrent*, gboolean do_notify);

/**
 * Notifies listeners that torrents have been added.
 * This should be called after one or more tr_core_add* () calls.
 */
void gtr_core_torrents_added(TrCore* self);

void gtr_core_torrent_changed(TrCore* self, int id);

/******
*******
******/

/* remove a torrent */
void gtr_core_remove_torrent(TrCore* self, int id, gboolean delete_files);

/* update the model with current torrent status */
void gtr_core_update(TrCore* self);

/**
***  Set a preference value, save the prefs file, and emit the "prefs-changed" signal
**/

void gtr_core_set_pref(TrCore* self, tr_quark const key, char const* val);
void gtr_core_set_pref_bool(TrCore* self, tr_quark const key, gboolean val);
void gtr_core_set_pref_int(TrCore* self, tr_quark const key, int val);
void gtr_core_set_pref_double(TrCore* self, tr_quark const key, double val);

/**
***
**/

void gtr_core_port_test(TrCore* core);

void gtr_core_blocklist_update(TrCore* core);

void gtr_core_exec(TrCore* core, tr_variant const* benc);

void gtr_core_open_folder(TrCore* core, int torrent_id);

/**
***
**/

/* column names for the model used to store torrent information */
/* keep this in sync with the type array in tr_core_init() in tr_core.c */
enum
{
    MC_NAME_COLLATED,
    MC_TORRENT,
    MC_TORRENT_ID,
    MC_SPEED_UP,
    MC_SPEED_DOWN,
    MC_ACTIVE_PEERS_UP,
    MC_ACTIVE_PEERS_DOWN,
    MC_RECHECK_PROGRESS,
    MC_ACTIVE,
    MC_ACTIVITY,
    MC_FINISHED,
    MC_PRIORITY,
    MC_QUEUE_POSITION,
    MC_TRACKERS,
    /* tr_stat.error
     * Tracked because ACTIVITY_FILTER_ERROR needs the row-changed events */
    MC_ERROR,
    /* tr_stat.{ peersSendingToUs + peersGettingFromUs + webseedsSendingToUs }
     * Tracked because ACTIVITY_FILTER_ACTIVE needs the row-changed events */
    MC_ACTIVE_PEER_COUNT,
    /* */
    MC_ROW_COUNT
};

G_END_DECLS
