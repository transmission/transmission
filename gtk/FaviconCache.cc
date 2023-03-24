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

class FaviconCache::InFlightData
{
public:
    InFlightData(IconFunc callback, std::string_view sitename)
      : callback_{ std::move(callback) }
      , sitename_{ sitename }
    {
    }

    [[nodiscard]] constexpr auto const& sitename() const noexcept
    {
      return sitename_;
    }

    ~InFlightData()
    {
      invoke_callback({}); // ensure it's called once, even if no pixbuf
    }

    void invoke_callback(Glib::RefPtr<Gdk::Pixbuf> const& pixbuf)
    {
        if (callback_)
        {
            callback_(pixbuf);
            callback_ = {};
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
    IconFunc callback_;
    std::string const sitename_;

    std::mutex responses_mutex_;
    std::vector<std::pair<std::string, long>> responses_;
};


FaviconCache::FaviconCache(tr_session* const session)
    : session_{ session }
    , cache_dir_{ Glib::build_filename(Glib::get_user_cache_dir(), "transmission") }
    , icons_dir_{ Glib::build_filename(cache_dir_, "favicons") }
    , scraped_sitenames_filename_{ Glib::build_filename(cache_dir_, "favicons-scraped.txt") }
{
}

void FaviconCache::mark_site_as_scraped(std::string_view sitename)
{
    if (auto ofs = std::ofstream{ scraped_sitenames_filename_, std::ios_base::out | std::ios_base::app }; ofs.is_open())
    {
        ofs << sitename << '\n';
    }
}

void FaviconCache::on_fetch_idle(std::shared_ptr<FaviconCache::InFlightData> fav)
{
    for (auto const& [contents, code] : fav->get_responses())
    {
        if (std::empty(contents) || code < 200 || code >= 300)
        {
            continue;
        }

        try
        {
            auto memory_stream = Gio::MemoryInputStream::create();
            memory_stream->add_data(std::data(contents), std::size(contents), nullptr);
            if (auto icon = Gdk::Pixbuf::create_from_stream_at_scale(memory_stream, Width, Height, false); icon)
            {
                icons_[fav->sitename()] = icon;
                Glib::file_set_contents(Glib::build_filename(icons_dir_, fav->sitename()), contents);
                fav->invoke_callback(icon);
                return;
            }
        }
        catch (Glib::Error const&)
        {
        }
    }
}

void FaviconCache::on_fetch_done(std::shared_ptr<FaviconCache::InFlightData> fav, tr_web::FetchResponse const& response)
{
    // delegate the work to the glib thread
    fav->add_response(response.body, response.status);
    Glib::signal_idle().connect_once([this, fav]() { on_fetch_idle(fav); });
}

void FaviconCache::scan_file_cache()
{
    // ensure the folders exist
    (void)g_mkdir_with_parents(cache_dir_.c_str(), 0777);
    (void)g_mkdir_with_parents(icons_dir_.c_str(), 0777);

    // remember which hosts we've asked for a favicon so that we
    // don't re-ask them every time we start a new session
    if (auto ifs = std::ifstream{ scraped_sitenames_filename_ }; ifs.is_open())
    {
        auto line = std::string{};
        while (std::getline(ifs, line))
        {
            if (auto const sitename = tr_strvStrip(line); !std::empty(sitename))
            {
                icons_.try_emplace(std::string{ sitename });
            }
        }
    }

    // load the cached favicons
    for (auto const& sitename : tr_sys_dir_get_files(icons_dir_))
    {
        auto const filename = Glib::build_filename(icons_dir_, sitename);

        try
        {
            icons_[sitename] = Gdk::Pixbuf::create_from_file(filename, Width, Height, false);
        }
        catch (Glib::Error const&)
        {
            (void)g_remove(filename.c_str());
        }
    }
}

void FaviconCache::lookup(std::string_view url_in, IconFunc callback)
{
    std::call_once(scan_once_flag_, &FaviconCache::scan_file_cache, this);

    auto const url = tr_urlParse(url_in);
    if (!url) // invalid url?
    {
        callback({});
        return;
    }

    // Try to download a favicon if we don't have one.
    // Add a placeholder to prevent repeat downloads.
    if (auto const [iter, inserted] = icons_.try_emplace(std::string{ url->sitename }); !inserted)
    {
        callback(iter->second);
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

    static constexpr auto TimeoutSecs = 15s;
    auto data = std::make_shared<InFlightData>(callback, url->sitename);
    for (auto i = 0; i < n_ports; ++i)
    {
        for (auto const scheme : { "http"sv, "https"sv })
        {
            for (auto const suffix : { "ico"sv, "png"sv, "gif"sv, "jpg"sv })
            {
                auto const favicon_url = fmt::format("{:s}://{:s}:{:d}/favicon.{:s}", scheme, url->host, ports[i], suffix);
                tr_sessionFetch(session_, { favicon_url, [this, data](auto const& response){ on_fetch_done(data, response); }, nullptr, TimeoutSecs });
            }
        }
    }
}
