/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

struct evbuffer;

#include "transmission.h" // tr_sha1_digest_t

/** @brief convenience function to determine if an address is an IP address (IPv4 or IPv6) */
bool tr_addressIsIP(char const* address);

/** @brief return true if the url is a http or https or UDP url that Transmission understands */
bool tr_urlIsValidTracker(std::string_view url);

/** @brief return true if the url is a [ http, https, ftp, sftp ] url that Transmission understands */
bool tr_urlIsValid(std::string_view url);

struct tr_url_parsed_t
{
    std::string_view scheme;
    std::string_view authority;
    std::string_view host;
    std::string_view path;
    std::string_view portstr;
    std::string_view query;
    std::string_view fragment;
    std::string_view full;
    int port = -1;
};

std::optional<tr_url_parsed_t> tr_urlParse(std::string_view url);

// like tr_urlParse(), but with the added constraint that 'scheme'
// must be one we that Transmission supports for announce and scrape
std::optional<tr_url_parsed_t> tr_urlParseTracker(std::string_view url);

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
        struct keyval_t
        {
            std::string_view key;
            std::string_view value;

            bool operator==(keyval_t const& that) const
            {
                return this->key == that.key && this->value == that.value;
            }

            bool operator!=(keyval_t const& that) const
            {
                return !(*this == that);
            }
        };

        keyval_t keyval;
        std::string_view remain;

        iterator& operator++();
        keyval_t const& operator*() const
        {
            return keyval;
        }
        keyval_t const* operator->() const
        {
            return &keyval;
        }

        bool operator==(iterator const& that) const
        {
            return this->keyval == that.keyval && this->remain == that.remain;
        }

        bool operator!=(iterator const& that) const
        {
            return !(*this == that);
        }
    };

    iterator begin() const;
    iterator end() const
    {
        return iterator{};
    }
};

// TODO: replace evbuffer* with std::string&
void tr_http_escape(struct evbuffer* out, std::string_view str, bool escape_reserved);

void tr_http_escape_sha1(char* out, uint8_t const* sha1_digest);

void tr_http_escape_sha1(char* out, tr_sha1_digest_t const& digest);

char* tr_http_unescape(char const* str, size_t len);

char const* tr_webGetResponseStr(long response_code);
