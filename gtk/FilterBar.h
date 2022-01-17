/*
 * This file Copyright (C) 2012-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

#include <gtkmm.h>

#include <libtransmission/tr-macros.h>

typedef struct tr_session tr_session;

class FilterBar : public Gtk::Box
{
public:
    FilterBar(tr_session* session, Glib::RefPtr<Gtk::TreeModel> const& torrent_model);
    ~FilterBar() override;

    TR_DISABLE_COPY_MOVE(FilterBar)

    Glib::RefPtr<Gtk::TreeModel> get_filter_model() const;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
