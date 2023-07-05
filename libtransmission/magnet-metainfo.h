// This file Copyright 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // size_t
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "announce-list.h"
#include "crypto-utils.h"
#include "tr-strbuf.h" // tr_urlbuf
#include "utils.h" // tr_strv_convert_utf8()

struct tr_error;
struct tr_variant;

class tr_magnet_metainfo
{
    friend struct MetainfoHandler;

public:
    bool parseMagnet(std::string_view magnet_link, tr_error** error = nullptr);

    [[nodiscard]] tr_urlbuf magnet() const;

    [[nodiscard]] constexpr auto const& info_hash() const noexcept
    {
        return info_hash_;
    }

    [[nodiscard]] constexpr auto const& name() const noexcept
    {
        return name_;
    }

    [[nodiscard]] TR_CONSTEXPR20 auto webseed_count() const noexcept
    {
        return std::size(webseed_urls_);
    }

    [[nodiscard]] TR_CONSTEXPR20 auto const& webseed(size_t i) const
    {
        return webseed_urls_.at(i);
    }

    [[nodiscard]] constexpr auto& announce_list() noexcept
    {
        return announce_list_;
    }

    [[nodiscard]] constexpr auto const& announce_list() const noexcept
    {
        return announce_list_;
    }

    [[nodiscard]] constexpr auto const& info_hash_string() const noexcept
    {
        return info_hash_str_;
    }

    [[nodiscard]] constexpr auto const& info_hash2_string() const noexcept
    {
        return info_hash2_str_;
    }

    void set_name(std::string_view name)
    {
        name_ = tr_strv_convert_utf8(name);
    }

    void add_webseed(std::string_view webseed);

protected:
    tr_announce_list announce_list_;
    std::vector<std::string> webseed_urls_;
    tr_sha1_digest_t info_hash_ = {};
    tr_sha256_digest_t info_hash2_ = {};
    tr_sha1_string info_hash_str_;
    tr_sha256_string info_hash2_str_;
    std::string name_;
};
