// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <glibmm/refptr.h>
#include <gtkmm/builder.h>
#include <gtkmm/dialog.h>
#include <gtkmm/window.h>

#include <memory>

class Session;

class PrefsDialog : public Gtk::Dialog
{
public:
    PrefsDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core);
    PrefsDialog(PrefsDialog&&) = delete;
    PrefsDialog(PrefsDialog const&) = delete;
    PrefsDialog& operator=(PrefsDialog&&) = delete;
    PrefsDialog& operator=(PrefsDialog const&) = delete;
    ~PrefsDialog() override;

    static std::unique_ptr<PrefsDialog> create(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};

auto inline constexpr MAIN_WINDOW_REFRESH_INTERVAL_SECONDS = 2;
auto inline constexpr SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS = 2;
