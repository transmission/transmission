// This file Copyright © 2005-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>

#include <glibmm/refptr.h>
#include <gtkmm/window.h>

#include <vector>

class Session;

/**
 * Prompt the user to confirm removing a torrent.
 */
void gtr_confirm_remove(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<tr_torrent_id_t> const& torrent_ids,
    bool delete_files);
