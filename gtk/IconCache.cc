/*
 * icons.[ch] written by Paolo Bacchilega, who writes:
 * "There is no problem for me, you can license my code
 * under whatever licence you wish :)"
 *
 */

#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <glibmm.h>
#include <giomm.h>

#include "IconCache.h"
#include "Utils.h"

using namespace std::literals;

std::string_view const DirectoryMimeType = "folder"sv;
std::string_view const UnknownMimeType = "unknown"sv;

namespace
{

auto const VoidPixbufKey = "void-pixbuf"s;

struct IconCache
{
    Glib::RefPtr<Gtk::IconTheme> icon_theme;
    int icon_size;
    std::map<std::string, Glib::RefPtr<Gdk::Pixbuf>, std::less<>> cache;
};

std::array<std::unique_ptr<IconCache>, 7> icon_cache;

Glib::RefPtr<Gdk::Pixbuf> create_void_pixbuf(int width, int height)
{
    auto const p = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, width, height);
    p->fill(0xFFFFFF00);
    return p;
}

int get_size_in_pixels(Gtk::IconSize icon_size)
{
    int width = 0;
    int height = 0;
    Gtk::IconSize::lookup(icon_size, width, height);
    return std::max(width, height);
}

std::unique_ptr<IconCache> icon_cache_new(Gtk::Widget& for_widget, Gtk::IconSize icon_size)
{
    auto icons = std::make_unique<IconCache>();
    icons->icon_theme = Gtk::IconTheme::get_for_screen(for_widget.get_screen());
    icons->icon_size = get_size_in_pixels(icon_size);
    icons->cache.try_emplace(VoidPixbufKey, create_void_pixbuf(icons->icon_size, icons->icon_size));
    return icons;
}

Glib::RefPtr<Gdk::Pixbuf> get_themed_icon_pixbuf(Gio::ThemedIcon& icon, int size, Gtk::IconTheme& icon_theme)
{
    auto const icon_names = icon.get_names();

    auto icon_info = icon_theme.choose_icon(icon_names, size);

    if (!bool{ icon_info })
    {
        icon_info = icon_theme.lookup_icon("text-x-generic", size, Gtk::ICON_LOOKUP_USE_BUILTIN);
    }

    try
    {
        return icon_info.load_icon();
    }
    catch (Glib::Error const& e)
    {
        g_warning("could not load icon pixbuf: %s\n", e.what().c_str());
        return {};
    }
}

Glib::RefPtr<Gdk::Pixbuf> get_file_icon_pixbuf(Gio::FileIcon& icon, int size)
{
    try
    {
        return Gdk::Pixbuf::create_from_file(icon.get_file()->get_path(), size, -1, false);
    }
    catch (Glib::Error const&)
    {
        return {};
    }
}

Glib::RefPtr<Gdk::Pixbuf> _get_icon_pixbuf(Glib::RefPtr<Gio::Icon> const& icon, int size, Gtk::IconTheme& theme)
{
    if (icon == nullptr)
    {
        return {};
    }

    if (auto* const ticon = dynamic_cast<Gio::ThemedIcon*>(gtr_get_ptr(icon)); ticon != nullptr)
    {
        return get_themed_icon_pixbuf(*ticon, size, theme);
    }

    if (auto* const ficon = dynamic_cast<Gio::FileIcon*>(gtr_get_ptr(icon)); ficon != nullptr)
    {
        return get_file_icon_pixbuf(*ficon, size);
    }

    return {};
}

Glib::RefPtr<Gdk::Pixbuf> icon_cache_get_mime_type_icon(IconCache& icons, std::string_view mime_type)
{
    auto& cache = icons.cache;

    if (auto mime_it = cache.find(mime_type); mime_it != std::end(cache))
    {
        return mime_it->second;
    }

    auto mime_type_str = std::string{ mime_type };
    auto icon = Gio::content_type_get_icon(mime_type_str);
    auto pixbuf = _get_icon_pixbuf(icon, icons.icon_size, *gtr_get_ptr(icons.icon_theme));
    if (pixbuf != nullptr)
    {
        cache.try_emplace(std::move(mime_type_str), pixbuf);
    }

    return pixbuf;
}

} // namespace

Glib::RefPtr<Gdk::Pixbuf> gtr_get_mime_type_icon(std::string_view mime_type, Gtk::IconSize icon_size, Gtk::Widget& for_widget)
{
    int n;

    switch (icon_size)
    {
    case Gtk::ICON_SIZE_MENU:
        n = 1;
        break;

    case Gtk::ICON_SIZE_SMALL_TOOLBAR:
        n = 2;
        break;

    case Gtk::ICON_SIZE_LARGE_TOOLBAR:
        n = 3;
        break;

    case Gtk::ICON_SIZE_BUTTON:
        n = 4;
        break;

    case Gtk::ICON_SIZE_DND:
        n = 5;
        break;

    case Gtk::ICON_SIZE_DIALOG:
        n = 6;
        break;

    default: /*GTK_ICON_SIZE_INVALID*/
        n = 0;
        break;
    }

    if (icon_cache[n] == nullptr)
    {
        icon_cache[n] = icon_cache_new(for_widget, icon_size);
    }

    return icon_cache_get_mime_type_icon(*icon_cache[n], mime_type);
}
