// This file Copyright © 2021-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint16_t
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "tr-macros.h" // tr_sha1_digest_t

/** @brief convenience function to determine if an address is an IP address (IPv4 or IPv6) */
bool tr_addressIsIP(char const* address);

/** @brief return true if the url is a http or https or UDP url that Transmission understands */
bool tr_urlIsValidTracker(std::string_view url);

/** @brief return true if the url is a [ http, https, ftp, sftp ] url that Transmission understands */
bool tr_urlIsValid(std::string_view url);

struct tr_url_parsed_t
{
    // http://example.com:80/over/there?name=ferret#nose

    std::string_view scheme; // "http"
    std::string_view authority; // "example.com:80"
    std::string_view host; // "example.com"
    std::string_view sitename; // "example"
    std::string_view path; // /"over/there"
    std::string_view query; // "name=ferret"
    std::string_view fragment; // "nose"
    std::string_view full; // "http://example.com:80/over/there?name=ferret#nose"
    uint16_t port = 0;
};

[[nodiscard]] std::optional<tr_url_parsed_t> tr_urlParse(std::string_view url);

// like tr_urlParse(), but with the added constraint that 'scheme'
// must be one we that Transmission supports for announce and scrape
[[nodiscard]] std::optional<tr_url_parsed_t> tr_urlParseTracker(std::string_view url);

// Convenience function to get a log-safe version of a tracker URL.
// This is to avoid logging sensitive info, e.g. a personal announcer id in the URL.
[[nodiscard]] std::string tr_urlTrackerLogName(std::string_view url);

// example use: `for (auto const [key, val] : tr_url_query_view{ querystr })`
struct tr_url_query_view
{
    std::string_view const query;

    explicit tr_url_query_view(std::string_view query_in)
        : query{ query_in }
    {
    }

    struct iterator
    {
        std::pair<std::string_view, std::string_view> keyval = std::make_pair(std::string_view{ "" }, std::string_view{ "" });
        std::string_view remain = "";

        iterator& operator++();

        [[nodiscard]] constexpr auto const& operator*() const
        {
            return keyval;
        }

        [[nodiscard]] constexpr auto const* operator->() const
        {
            return &keyval;
        }

        [[nodiscard]] constexpr bool operator==(iterator const& that) const
        {
            return this->remain == that.remain && this->keyval == that.keyval;
        }

        [[nodiscard]] constexpr bool operator!=(iterator const& that) const
        {
            return !(*this == that);
        }
    };

    [[nodiscard]] iterator begin() const;

    [[nodiscard]] constexpr iterator end() const
    {
        return iterator{};
    }
};

template<typename BackInsertIter>
constexpr void tr_urlPercentEncode(BackInsertIter out, std::string_view input, bool escape_reserved = true)
{
    auto constexpr IsUnreserved = [](unsigned char ch)
    {
        return ('0' <= ch && ch <= '9') || ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~';
    };

    auto constexpr IsReserved = [](unsigned char ch)
    {
        return ch == '!' || ch == '*' || ch == '(' || ch == ')' || ch == ';' || ch == ':' || ch == '@' || ch == '&' ||
            ch == '=' || ch == '+' || ch == '$' || ch == ',' || ch == '/' || ch == '?' || ch == '%' || ch == '#' || ch == '[' ||
            ch == ']' || ch == '\'';
    };

    for (unsigned char ch : input)
    {
        if (IsUnreserved(ch) || (!escape_reserved && IsReserved(ch)))
        {
            out = ch;
        }
        else
        {
            fmt::format_to(out, "%{:02X}", ch);
        }
    }
}

template<typename BackInsertIter>
constexpr void tr_urlPercentEncode(BackInsertIter out, tr_sha1_digest_t const& digest)
{
    tr_urlPercentEncode(out, std::string_view{ reinterpret_cast<char const*>(digest.data()), std::size(digest) });
}

[[nodiscard]] char const* tr_webGetResponseStr(long response_code);

[[nodiscard]] std::string tr_urlPercentDecode(std::string_view /*url*/);
