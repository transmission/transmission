/*
 * icons.[ch] written by Paolo Bacchilega, who writes:
 * "There is no problem for me, you can license
 * my code under whatever licence you wish :)"
 *
 */

#pragma once

#include <string>

#include <gtkmm.h>

extern Glib::ustring const DirectoryMimeType;
extern Glib::ustring const UnknownMimeType;

Glib::ustring gtr_get_mime_type_from_filename(std::string const& file);

Glib::RefPtr<Gdk::Pixbuf> gtr_get_mime_type_icon(
    Glib::ustring const& mime_type,
    Gtk::IconSize icon_size,
    Gtk::Widget& for_widget);
