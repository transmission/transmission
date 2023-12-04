// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <chrono>
#include <cstddef> // size_t
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include <libtransmission/file.h>
#include <libtransmission/web-utils.h>
#include <libtransmission/web.h>

template<typename Icon>
class FaviconCache
{
public:
    using IconFunc = std::function<void(Icon const*)>;

    FaviconCache()
        : cache_dir_{ app_cache_dir() }
        , icons_dir_{ fmt::format("{:s}/{:s}", cache_dir_, "favicons") }
        , scraped_sitenames_filename_{ fmt::format("{:s}/favicons-scraped.txt", cache_dir_) }
    {
    }

    [[nodiscard]] Icon const* find(std::string_view sitename) const noexcept
    {
        auto const iter = icons_.find(sitename);
        return iter != std::end(icons_) ? &iter->second : nullptr;
    }

    void load(
        std::string_view url_in,
        IconFunc callback = [](Icon const&) {})
    {
        std::call_once(scan_once_flag_, &FaviconCache::scan_file_cache, this);

        auto const url = tr_urlParse(url_in);
        if (!url) // invalid url?
        {
            callback({});
            return;
        }

        // If we already have it, use it.
        if (auto const* const icon = find(url->sitename); icon != nullptr)
        {
            callback(icon);
            return;
        }

        // We don't already have it, so fetch it.
        // Add a placeholder to icons_ prevent repeat downloads.
        icons_.try_emplace(std::string{ url->sitename });
        mark_site_as_scraped(url->sitename);

        // ports to try
        auto n_ports = 0;
        auto ports = std::array<int, 2>{};
        ports[n_ports++] = 80;
        if (url->port != 80)
        {
            ports[n_ports++] = url->port;
        }

        auto in_flight = std::make_shared<InFlightData>(callback, url->sitename);
        for (auto i = 0; i < n_ports; ++i)
        {
            for (auto const scheme : { "http", "https" })
            {
                for (auto const suffix : { "ico", "png", "gif", "jpg" })
                {
                    auto on_fetch_response = [this, in_flight](auto const& response)
                    {
                        in_flight->add_response(response.body, response.status);
                        add_to_ui_thread([this, in_flight]() { check_responses(in_flight); });
                    };

                    static constexpr auto TimeoutSecs = std::chrono::seconds{ 15 };
                    auto const favicon_url = fmt::format("{:s}://{:s}:{:d}/favicon.{:s}", scheme, url->host, ports[i], suffix);
                    in_flight->web().fetch({ favicon_url, std::move(on_fetch_response), nullptr, TimeoutSecs });
                }
            }
        }
    }

    static inline constexpr auto Width = 16;
    static inline constexpr auto Height = 16;

private:
    class InFlightData
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
            invoke_callback(nullptr); // ensure it's called once, even if no icon
        }

        void invoke_callback(Icon const* icon)
        {
            if (callback_)
            {
                callback_(icon);
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

        [[nodiscard]] auto& web()
        {
            return *web_;
        }

    private:
        IconFunc callback_;
        std::string const sitename_;

        std::mutex responses_mutex_;
        std::vector<std::pair<std::string, long>> responses_;

        tr_web::Mediator mediator_;
        std::unique_ptr<tr_web> web_ = tr_web::create(mediator_);
    };

    [[nodiscard]] Icon create_from_file(std::string_view filename) const;
    [[nodiscard]] Icon create_from_data(void const* data, size_t datalen) const;
    [[nodiscard]] std::string app_cache_dir() const;
    void add_to_ui_thread(std::function<void()> idlefunc);

    void scan_file_cache()
    {
        // ensure the folders exist
        tr_sys_dir_create(cache_dir_, TR_SYS_DIR_CREATE_PARENTS, 0700);
        tr_sys_dir_create(icons_dir_, TR_SYS_DIR_CREATE_PARENTS, 0700);

        // remember which hosts we've asked for a favicon so that we
        // don't re-ask them every time we start a new session
        if (auto ifs = std::ifstream{ scraped_sitenames_filename_ }; ifs.is_open())
        {
            auto sitename = std::string{};
            while (std::getline(ifs, sitename))
            {
                icons_.try_emplace(sitename);
            }
        }

        // load the cached favicons
        for (auto const& sitename : tr_sys_dir_get_files(icons_dir_))
        {
            auto const filename = fmt::format("{:s}/{:s}", icons_dir_, sitename);

            if (auto icon = create_from_file(filename); !icon)
            {
                tr_sys_path_remove(filename);
            }
            else
            {
                icons_[sitename] = std::move(icon);
            }
        }
    }

    void mark_site_as_scraped(std::string_view sitename)
    {
        if (auto ofs = std::ofstream{ scraped_sitenames_filename_, std::ios_base::out | std::ios_base::app }; ofs.is_open())
        {
            ofs << sitename << '\n';
        }
    }

    void check_responses(std::shared_ptr<FaviconCache::InFlightData> in_flight)
    {
        for (auto const& [contents, code] : in_flight->get_responses())
        {
            if (std::empty(contents) || code < 200 || code >= 300)
            {
                continue;
            }

            auto const icon = create_from_data(std::data(contents), std::size(contents));
            if (!icon)
            {
                continue;
            }

            // cache it in memory
            auto& perm = icons_[in_flight->sitename()];
            perm = std::move(icon);

            // cache it on disk
            tr_file_save(fmt::format("{:s}/{:s}", icons_dir_, in_flight->sitename()), contents);

            // notify the user that we got it
            in_flight->invoke_callback(&perm);
            return;
        }
    }

    std::once_flag scan_once_flag_;
    std::string const cache_dir_;
    std::string const icons_dir_;
    std::string const scraped_sitenames_filename_;

    std::map<std::string /*sitename*/, Icon, std::less<>> icons_;
};
