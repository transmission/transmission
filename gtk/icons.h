/*
 * icons.[ch] written by Paolo Bacchilega, who writes:
 * "There is no problem for me, you can license
 * my code under whatever licence you wish :)"
 *
 */

#pragma once

#define DIRECTORY_MIME_TYPE "folder"
#define UNKNOWN_MIME_TYPE "unknown"

char const* gtr_get_mime_type_from_filename(char const* file);

GdkPixbuf* gtr_get_mime_type_icon(char const* mime_type, GtkIconSize icon_size, GtkWidget* for_widget);
