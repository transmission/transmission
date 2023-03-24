// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h>
#include <libtransmission/web.h>

#include <gdkmm/pixbuf.h>
#include <glibmm/refptr.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

class FaviconCache
{
public:
    FaviconCache(tr_session* session);

    using IconFunc = std::function<void(Glib::RefPtr<Gdk::Pixbuf> const&)>;

    void lookup(std::string_view url, IconFunc callback);

private:
    class InFlightData;

    void scan_file_cache();
    void mark_site_as_scraped(std::string_view sitename);

    void on_fetch_idle(std::shared_ptr<FaviconCache::InFlightData> fav);
    void on_fetch_done(std::shared_ptr<FaviconCache::InFlightData> fav, tr_web::FetchResponse const& response);

    static inline constexpr auto Width = 16;
    static inline constexpr auto Height = 16;

    tr_session* const session_;
    std::once_flag scan_once_flag_;
    std::string const cache_dir_;
    std::string const icons_dir_;
    std::string const scraped_sitenames_filename_;

    std::map<std::string /*sitename*/, Glib::RefPtr<Gdk::Pixbuf>, std::less<>> icons_;
};
