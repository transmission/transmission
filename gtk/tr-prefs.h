/*
 * This file Copyright (C) 2007-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <memory>

#include <gtkmm.h>

typedef struct _TrCore TrCore;

class PrefsDialog : public Gtk::Dialog
{
public:
    ~PrefsDialog() override;

    static std::unique_ptr<PrefsDialog> create(Gtk::Window& parent, TrCore* core);

protected:
    PrefsDialog(Gtk::Window& parent, TrCore* core);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};

enum
{
    MAIN_WINDOW_REFRESH_INTERVAL_SECONDS = 2,
    SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS = 2
};
