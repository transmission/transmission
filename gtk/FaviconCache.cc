// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FaviconCache.h"

#include "Utils.h" /* gtr_get_host_from_url() */

#include <libtransmission/transmission.h>
#include <libtransmission/web-utils.h>
#include <libtransmission/web.h> // tr_sessionFetch()

#include <glibmm/error.h>
#include <glibmm/fileutils.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#include <fmt/core.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include <glib/gstdio.h> /* g_remove() */

using namespace std::literals;

namespace
{

constexpr auto TimeoutSecs = 15s;
constexpr auto ImageTypes = std::array<std::string_view, 4>{ "ico"sv, "png"sv, "gif"sv, "jpg"sv };

struct favicon_data
{
    tr_session* session = nullptr;
    std::function<void(Glib::RefPtr<Gdk::Pixbuf> const&)> func;
    std::string host;
    std::string contents;
    size_t type = 0;
    long code = 0;
};

Glib::ustring get_url(std::string const& host, size_t image_type)
{
    return fmt::format("http://{}/favicon.{}", host, ImageTypes.at(image_type));
}

std::string favicon_get_cache_dir()
{
    static std::string dir;

    if (dir.empty())
    {
        dir = Glib::build_filename(Glib::get_user_cache_dir(), "transmission", "favicons");
        (void)g_mkdir_with_parents(dir.c_str(), 0777);
    }

    return dir;
}

std::string favicon_get_cache_filename(std::string const& host)
{
    return Glib::build_filename(favicon_get_cache_dir(), host);
}

void favicon_save_to_cache(std::string const& host, std::string const& data)
{
    Glib::file_set_contents(favicon_get_cache_filename(host), data);
}

Glib::RefPtr<Gdk::Pixbuf> favicon_load_from_cache(std::string const& host)
{
    auto const filename = favicon_get_cache_filename(host);

    try
    {
        return Gdk::Pixbuf::create_from_file(filename, 16, 16, false);
    }
    catch (Glib::Error const&)
    {
        (void)g_remove(filename.c_str());
        return {};
    }
}

void favicon_web_done_cb(tr_web::FetchResponse const& response);

constexpr bool should_keep_trying(long code)
{
    return code != 0 && code <= 500;
}

bool favicon_web_done_idle_cb(std::unique_ptr<favicon_data> fav)
{
    Glib::RefPtr<Gdk::Pixbuf> pixbuf;

    if (!fav->contents.empty()) /* we got something... try to make a pixbuf from it */
    {
        favicon_save_to_cache(fav->host, fav->contents);
        pixbuf = favicon_load_from_cache(fav->host);
    }

    if (pixbuf == nullptr && should_keep_trying(fav->code) && ++fav->type < ImageTypes.size()) /* keep trying */
    {
        fav->contents.clear();
        auto* const session = fav->session;
        auto const next_url = get_url(fav->host, fav->type);
        tr_sessionFetch(session, { next_url.raw(), favicon_web_done_cb, fav.release(), TimeoutSecs });
        return false;
    }

    // Not released into the next web request, means we're done trying (even if `pixbuf` is still invalid)
    fav->func(pixbuf);
    return false;
}

void favicon_web_done_cb(tr_web::FetchResponse const& response)
{
    auto* const fav = static_cast<favicon_data*>(response.user_data);
    fav->contents = response.body;
    fav->code = response.status;
    Glib::signal_idle().connect([fav]() { return favicon_web_done_idle_cb(std::unique_ptr<favicon_data>(fav)); });
}

} // namespace

void gtr_get_favicon(
    tr_session* session,
    std::string const& host,
    std::function<void(Glib::RefPtr<Gdk::Pixbuf> const&)> const& pixbuf_ready_func)
{
    auto pixbuf = favicon_load_from_cache(host);

    if (pixbuf != nullptr)
    {
        pixbuf_ready_func(pixbuf);
    }
    else
    {
        auto data = std::make_unique<favicon_data>();
        data->session = session;
        data->func = pixbuf_ready_func;
        data->host = host;
        tr_sessionFetch(session, { get_url(host, 0).raw(), favicon_web_done_cb, data.release(), TimeoutSecs });
    }
}

void gtr_get_favicon_from_url(
    tr_session* session,
    Glib::ustring const& url,
    std::function<void(Glib::RefPtr<Gdk::Pixbuf> const&)> const& pixbuf_ready_func)
{
    if (auto const parsed_url = tr_urlParse(url.c_str()); parsed_url.has_value())
    {
        auto const host = std::string{ parsed_url->host };
        gtr_get_favicon(session, host, pixbuf_ready_func);
    }
    else
    {
        pixbuf_ready_func({});
    }
}
