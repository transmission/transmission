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
    using Icon = Glib::RefPtr<Gdk::Pixbuf>;
    using IconFunc = std::function<void(Icon const&)>;

    FaviconCache(tr_session* session);

    void lookup(std::string_view url, IconFunc callback);

private:
    class InFlightData;

    void scan_file_cache();
    void mark_site_as_scraped(std::string_view sitename);

    void check_responses(std::shared_ptr<FaviconCache::InFlightData> in_flight);
    void add_to_ui_thread(std::function<void()> idlefunc);

    [[nodiscard]] Icon create_from_file(std::string_view filename) const;
    [[nodiscard]] Icon create_from_data(void const* data, size_t datalen) const;
    [[nodiscard]] std::string app_cache_dir() const;

    static inline constexpr auto Width = 16;
    static inline constexpr auto Height = 16;

    tr_session* const session_;
    std::once_flag scan_once_flag_;
    std::string const cache_dir_;
    std::string const icons_dir_;
    std::string const scraped_sitenames_filename_;

    std::map<std::string /*sitename*/, Icon, std::less<>> icons_;
};
