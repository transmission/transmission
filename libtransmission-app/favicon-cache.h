// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <atomic>
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
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <libtransmission/file-utils.h>
#include <libtransmission/file.h>
#include <libtransmission/web-utils.h>
#include <libtransmission/web.h>

namespace tr::app
{

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

    FaviconCache(FaviconCache const&) = delete;
    FaviconCache(FaviconCache&&) = delete;
    FaviconCache& operator=(FaviconCache const&) = delete;
    FaviconCache& operator=(FaviconCache&&) = delete;

    ~FaviconCache()
    {
        alive_->store(false);
        web_.reset();
        for (auto const& in_flight : in_flight_)
        {
            in_flight->clear_callback();
        }
        in_flight_.clear();
    }

    [[nodiscard]] Icon const* find(std::string_view sitename) const noexcept
    {
        auto const iter = icons_.find(sitename);
        return iter != std::end(icons_) ? &iter->second : nullptr;
    }

    void load(std::string_view url_in, IconFunc callback = [](Icon const*) { /*default callback is a no-op */ })
    {
        std::call_once(scan_once_flag_, &FaviconCache::scan_file_cache, this);

        auto const url = tr_urlParse(url_in);
        if (!url) // invalid url?
        {
            if (callback)
            {
                callback({});
            }
            return;
        }

        // If we already have it, use it.
        if (auto const* const icon = find(url->sitename); icon != nullptr)
        {
            if (callback)
            {
                callback(icon);
            }
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

        static constexpr auto Schemes = std::array{ "http", "https" };
        static constexpr auto Suffixes = std::array{ "ico", "png", "gif", "jpg" };
        auto const n_requests = static_cast<size_t>(n_ports) * std::size(Schemes) * std::size(Suffixes);

        if (!web_)
        {
            web_ = tr_web::create(mediator_);
        }

        auto const in_flight = std::make_shared<InFlightData>(std::move(callback), url->sitename, n_requests);
        in_flight_.emplace(in_flight);

        for (auto i = 0; i < n_ports; ++i)
        {
            for (auto const* const scheme : Schemes)
            {
                for (auto const* const suffix : Suffixes)
                {
                    auto on_fetch_response = [this, alive = alive_, in_flight](auto const& response)
                    {
                        if (!alive->load())
                        {
                            return;
                        }

                        in_flight->add_response(response.body, response.status);
                        add_to_ui_thread(
                            [this, alive, in_flight]()
                            {
                                if (!alive->load())
                                {
                                    return;
                                }

                                check_responses(in_flight);
                            });
                    };

                    static constexpr auto TimeoutSecs = std::chrono::seconds{ 15 };
                    auto const favicon_url = fmt::format("{:s}://{:s}:{:d}/favicon.{:s}", scheme, url->host, ports[i], suffix);
                    web_->fetch({ favicon_url, std::move(on_fetch_response), nullptr, TimeoutSecs });
                }
            }
        }
    }

    static constexpr auto Width = 16;
    static constexpr auto Height = 16;

private:
    class InFlightData
    {
    public:
        InFlightData(IconFunc callback, std::string_view sitename, size_t n_pending)
            : callback_{ std::move(callback) }
            , sitename_{ sitename }
            , n_pending_{ n_pending }
        {
        }

        InFlightData(InFlightData const&) = delete;
        InFlightData(InFlightData&&) = delete;
        InFlightData& operator=(InFlightData const&) = delete;
        InFlightData& operator=(InFlightData&&) = delete;
        ~InFlightData() = default;

        [[nodiscard]] constexpr auto const& sitename() const noexcept
        {
            return sitename_;
        }

        [[nodiscard]] constexpr bool is_done() const noexcept
        {
            return done_;
        }

        void clear_callback()
        {
            callback_ = nullptr;
            done_ = true;
        }

        void invoke_callback(Icon const* icon)
        {
            done_ = true;
            if (auto const callback = std::exchange(callback_, {}); callback)
            {
                callback(icon);
            }
        }

        [[nodiscard]] size_t consume_pending(size_t n_responses) noexcept
        {
            n_pending_ = n_responses < n_pending_ ? n_pending_ - n_responses : 0U;
            return n_pending_;
        }

        [[nodiscard]] auto get_responses()
        {
            auto lock = std::scoped_lock{ responses_mutex_ };

            auto tmp = decltype(responses_){};
            std::swap(tmp, responses_);
            return tmp;
        }

        void add_response(std::string contents, long code)
        {
            auto lock = std::scoped_lock{ responses_mutex_ };

            responses_.emplace_back(std::move(contents), code);
        }

    private:
        IconFunc callback_;
        std::string const sitename_;
        size_t n_pending_ = 0;
        bool done_ = false;

        std::mutex responses_mutex_;
        std::vector<std::pair<std::string, long>> responses_;
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

    void mark_site_as_scraped(std::string_view sitename) const
    {
        if (auto ofs = std::ofstream{ scraped_sitenames_filename_, std::ios_base::out | std::ios_base::app }; ofs.is_open())
        {
            ofs << sitename << '\n';
        }
    }

    void check_responses(std::shared_ptr<InFlightData> const& in_flight)
    {
        auto const responses = in_flight->get_responses();
        if (in_flight->is_done())
        {
            return;
        }

        for (auto const& [contents, code] : responses)
        {
            if (std::empty(contents) || code < 200 || code >= 300)
            {
                continue;
            }

            auto icon = create_from_data(std::data(contents), std::size(contents));
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
            in_flight_.erase(in_flight);
            in_flight->invoke_callback(&perm);
            return;
        }

        if (in_flight->consume_pending(std::size(responses)) == 0U)
        {
            in_flight_.erase(in_flight);
            in_flight->invoke_callback(nullptr);
        }
    }

    std::once_flag scan_once_flag_;
    std::string const cache_dir_;
    std::string const icons_dir_;
    std::string const scraped_sitenames_filename_;

    std::map<std::string /*sitename*/, Icon, std::less<>> icons_;
    std::unordered_set<std::shared_ptr<InFlightData>> in_flight_;

    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    tr_web::Mediator mediator_;
    std::unique_ptr<tr_web> web_;
};

} // namespace tr::app
