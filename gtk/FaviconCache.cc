// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/favicon-cache.h>

#include <gdkmm/pixbuf.h>
#include <giomm/memoryinputstream.h>
#include <glibmm/error.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

using Icon = Glib::RefPtr<Gdk::Pixbuf>;

template<>
Icon FaviconCache<Icon>::create_from_file(std::string_view filename) const
{
    try
    {
        return Gdk::Pixbuf::create_from_file(std::string{ filename }, Width, Height, false);
    }
    catch (Glib::Error const&)
    {
        return {};
    }
}

template<>
Icon FaviconCache<Icon>::create_from_data(void const* data, size_t datalen) const
{
    try
    {
        auto memory_stream = Gio::MemoryInputStream::create();
        memory_stream->add_data(data, datalen, nullptr);
        return Gdk::Pixbuf::create_from_stream_at_scale(memory_stream, Width, Height, false);
    }
    catch (Glib::Error const&)
    {
        return {};
    }
}

template<>
std::string FaviconCache<Icon>::app_cache_dir() const
{
    return fmt::format("{:s}/{:s}", Glib::get_user_cache_dir(), "transmission");
}

template<>
void FaviconCache<Icon>::add_to_ui_thread(std::function<void()> idlefunc)
{
    Glib::signal_idle().connect_once([idlefunc = std::move(idlefunc)]() { idlefunc(); });
}
