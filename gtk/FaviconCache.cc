// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "FaviconCache.h"

#include "Utils.h" /* gtr_get_host_from_url() */

#include <libtransmission/transmission.h>
#include <libtransmission/file.h>
#include <libtransmission/utils.h>
#include <libtransmission/web-utils.h>
#include <libtransmission/web.h> // tr_sessionFetch()

#include <giomm/memoryinputstream.h>
#include <gdkmm/pixbuf.h>
#include <glibmm/error.h>
#include <glibmm/fileutils.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#include <fmt/core.h>

#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <glib/gstdio.h> /* g_remove() */

using namespace std::literals;

namespace
{

constexpr auto TimeoutSecs = 15s;
constexpr auto Width = 16;
constexpr auto Height = 16;

std::map<std::string /*sitename*/, Glib::RefPtr<Gdk::Pixbuf>, std::less<>> pixmaps_;

struct InFlightData
{
    using Callback = std::function<void(Glib::RefPtr<Gdk::Pixbuf> const&)>;

    InFlightData(Callback pixbuf_ready_func, std::string_view sitename)
        : pixbuf_ready_func_{ std::move(pixbuf_ready_func) }
        , sitename_{ sitename }
    {
    }

    [[nodiscard]] constexpr auto const& sitename() const noexcept
    {
        return sitename_;
    }

    ~InFlightData()
    {
        fmt::print("destructor for {:s}\n", sitename());
        invoke_callback({}); // ensure it's called once, even if no pixbuf
    }

    void invoke_callback(Glib::RefPtr<Gdk::Pixbuf> const& pixbuf)
    {
        if (pixbuf_ready_func_)
        {
            fmt::print("calling callback for {:s}\n", sitename());
            pixbuf_ready_func_(pixbuf);
            pixbuf_ready_func_ = {};
        }
    }

    [[nodiscard]] auto get_responses()
    {
        auto lock = std::lock_guard{ responses_mutex_ };
        auto tmp = decltype(responses_){};
        std::swap(tmp, responses_);
        return tmp;
    }

    void add_response(std::string contents, long code)
    {
        auto lock = std::lock_guard{ responses_mutex_ };
        responses_.emplace_back(std::move(contents), code);
    }

private:
    std::function<void(Glib::RefPtr<Gdk::Pixbuf> const&)> pixbuf_ready_func_;
    std::string const sitename_;

    std::mutex responses_mutex_;
    std::vector<std::pair<std::string, long>> responses_;
};

[[nodiscard]] auto const& get_app_cache_dir()
{
    static auto dir = std::string{};

    if (std::empty(dir))
    {
        dir = Glib::build_filename(Glib::get_user_cache_dir(), "transmission");
        (void)g_mkdir_with_parents(dir.c_str(), 0777);
    }

    return dir;
}

[[nodiscard]] auto get_app_favicons_dir()
{
    static auto dir = std::string{};

    if (std::empty(dir))
    {
        dir = Glib::build_filename(get_app_cache_dir(), "favicons");
        (void)g_mkdir_with_parents(dir.c_str(), 0777);
    }

    return dir;
}

[[nodiscard]] auto get_scraped_file()
{
    return fmt::format("{:s}/favicons-scraped.txt", get_app_cache_dir());
}

void mark_site_as_scraped(std::string_view sitename)
{
    if (auto ofs = std::ofstream{ get_scraped_file(), std::ios_base::out | std::ios_base::app }; ofs.is_open())
    {
        ofs << sitename << '\n';
    }
}

std::once_flag cache_dir_once_flag;

void ensure_cache_dir_has_been_scanned()
{
    // remember which hosts we've asked for a favicon so that we
    // don't re-ask them every time we start a new session
    if (auto ifs = std::ifstream{ get_scraped_file() }; ifs.is_open())
    {
        auto line = std::string{};
        while (std::getline(ifs, line))
        {
            if (auto const sitename = tr_strvStrip(line); !std::empty(sitename))
            {
                pixmaps_.try_emplace(std::string{ sitename });
            }
        }
    }

    // load the cached favicons
    auto const icons_dir = get_app_favicons_dir();
    for (auto const& sitename : tr_sys_dir_get_files(icons_dir))
    {
        auto filename = Glib::build_filename(icons_dir, sitename);

        try
        {
            pixmaps_[sitename] = Gdk::Pixbuf::create_from_file(filename, Width, Height, false);
        }
        catch (Glib::Error const&)
        {
            (void)g_remove(filename.c_str());
        }
    }
}


void on_fetch_done_idle(std::shared_ptr<InFlightData> fav)
{
    for (auto const& [contents, code] : fav->get_responses())
    {
        if (std::empty(contents) || code < 200 || code >= 300)
        {
            continue;
        }

        auto memory_stream = Gio::MemoryInputStream::create();
        memory_stream->add_data(std::data(contents), std::size(contents), nullptr);
        auto pixbuf = Gdk::Pixbuf::create_from_stream_at_scale(memory_stream, Width, Height, false);
        if (!pixbuf)
        {
            continue;
        }

        fmt::print("got pixbuf for {:s}\n", fav->sitename());
        Glib::file_set_contents(Glib::build_filename(get_app_favicons_dir(), fav->sitename()), contents);
        fav->invoke_callback(pixbuf);
        return;
    }
}

void on_fetch_done(std::shared_ptr<InFlightData> fav, tr_web::FetchResponse const& response)
{
    fav->add_response(response.body, response.status);
    Glib::signal_idle().connect_once([fav]() { return on_fetch_done_idle(fav); });
}

} // namespace

void gtr_get_favicon_from_url(
    tr_session* session,
    Glib::ustring const& url_in,
    std::function<void(Glib::RefPtr<Gdk::Pixbuf> const&)> const& pixbuf_ready_func)
{
    std::call_once(cache_dir_once_flag, ensure_cache_dir_has_been_scanned);

    auto const url = tr_urlParse(url_in.c_str());
    if (!url) // invalid url?
    {
        pixbuf_ready_func({});
        return;
    }

    // Try to download a favicon if we don't have one.
    // Add a placeholder to prevent repeat downloads.
    if (auto const [iter, inserted] = pixmaps_.try_emplace(std::string{ url->sitename }); !inserted)
    {
        pixbuf_ready_func(iter->second);
        return;
    }

    mark_site_as_scraped(url->sitename);

    // ports to try
    auto n_ports = 0;
    auto ports = std::array<int, 2>{};
    ports[n_ports++] = 80;
    if (url->port != 80)
    {
        ports[n_ports++] = url->port;
    }

    auto data = std::make_shared<InFlightData>(pixbuf_ready_func, url->sitename);
    for (auto i = 0; i < n_ports; ++i)
    {
        for (auto const scheme : { "http"sv, "https"sv })
        {
            for (auto const suffix : { "ico"sv, "png"sv, "gif"sv, "jpg"sv })
            {
                auto const favicon_url = fmt::format("{:s}://{:s}:{:d}/favicon.{:s}", scheme, url->host, ports[i], suffix);
                tr_sessionFetch(session, { favicon_url, [data](auto const& response){ on_fetch_done(data, response); }, nullptr, TimeoutSecs });
            }
        }
    }
}
