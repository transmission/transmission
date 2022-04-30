/*
 * icons.[ch] written by Paolo Bacchilega, who writes:
 * "There is no problem for me, you can license
 * my code under whatever licence you wish :)"
 *
 */

#pragma once

#include <string_view>

#include <gtkmm.h>

extern std::string_view const DirectoryMimeType;
extern std::string_view const UnknownMimeType;

Glib::RefPtr<Gdk::Pixbuf> gtr_get_mime_type_icon(std::string_view mime_type, Gtk::IconSize icon_size, Gtk::Widget& for_widget);
