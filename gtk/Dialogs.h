// This file Copyright (C) 2005-2021 by its respective authors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <vector>

#include <gtkmm.h>

class Session;

/**
 * Prompt the user to confirm removing a torrent.
 */
void gtr_confirm_remove(
    Gtk::Window& parent,
    Glib::RefPtr<Session> const& core,
    std::vector<int> const& torrent_ids,
    bool delete_files);
