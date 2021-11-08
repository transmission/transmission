/*
 * This file Copyright (C) 2012-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <functional>
#include <string>

#include <gdkmm.h>
#include <glibmm.h>

#include <libtransmission/transmission.h>

void gtr_get_favicon(
    tr_session* session,
    std::string const& host,
    std::function<void(Glib::RefPtr<Gdk::Pixbuf> const&)> const& pixbuf_ready_func);

void gtr_get_favicon_from_url(
    tr_session* session,
    Glib::ustring const& url,
    std::function<void(Glib::RefPtr<Gdk::Pixbuf> const&)> const& pixbuf_ready_func);
