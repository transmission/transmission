/*
 * icons.[ch] written by Paolo Bacchilega, who writes:
 * "There is no problem for me, you can license my code
 * under whatever licence you wish :)"
 *
 */

#include "IconCache.h"

#include "GtkCompat.h"

#include <giomm/contenttype.h>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>

using namespace std::literals;

using IconCache = std::map<std::string, Glib::RefPtr<Gio::Icon>, std::less<>>;

std::string_view const DirectoryMimeType = "folder"sv;
std::string_view const UnknownMimeType = "unknown"sv;

Glib::RefPtr<Gio::Icon> gtr_get_mime_type_icon(std::string_view mime_type)
{
    static IconCache cache;

    if (auto mime_it = cache.find(mime_type); mime_it != std::end(cache))
    {
        return mime_it->second;
    }

    auto mime_type_str = std::string{ mime_type };
    auto icon = Gio::content_type_get_icon(mime_type_str);
    if (icon != nullptr)
    {
        cache.try_emplace(std::move(mime_type_str), icon);
    }

    return icon;
}
