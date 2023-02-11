// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "GtkCompat.h"

#include <libtransmission/tr-macros.h>

#include <glibmm/propertyproxy.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/builder.h>
#include <gtkmm/filechooser.h>
#include <gtkmm/filefilter.h>

#if GTKMM_CHECK_VERSION(4, 0, 0)
#include <gtkmm/button.h>
#else
#include <gtkmm/filechooserbutton.h>
#endif

#include <list>
#include <memory>
#include <string>

class PathButton : public IF_GTKMM4(Gtk::Button, Gtk::FileChooserButton)
{
    using BaseWidgetType = IF_GTKMM4(Gtk::Button, Gtk::FileChooserButton);

public:
    PathButton();
    PathButton(BaseObjectType* cast_item, Glib::RefPtr<Gtk::Builder> const& builder);
    ~PathButton() override;

    TR_DISABLE_COPY_MOVE(PathButton)

    void set_shortcut_folders(std::list<std::string> const& value);

#if GTKMM_CHECK_VERSION(4, 0, 0)
    std::string get_filename() const;
    void set_filename(std::string const& value);

    void add_filter(Glib::RefPtr<Gtk::FileFilter> const& value);

    Glib::PropertyProxy<Gtk::FileChooser::Action> property_action();
    Glib::PropertyProxy<Glib::ustring> property_title();

    sigc::signal<void()>& signal_selection_changed();
#endif

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
