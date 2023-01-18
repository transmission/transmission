/*
 * icons.[ch] written by Paolo Bacchilega, who writes:
 * "There is no problem for me, you can license
 * my code under whatever licence you wish :)"
 *
 */

#pragma once

#include <giomm/icon.h>
#include <glibmm/refptr.h>

#include <string_view>

extern std::string_view const DirectoryMimeType;
extern std::string_view const UnknownMimeType;

Glib::RefPtr<Gio::Icon> gtr_get_mime_type_icon(std::string_view mime_type);
