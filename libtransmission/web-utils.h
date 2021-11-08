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

#include "transmission.h"

/** @brief convenience function to determine if an address is an IP address (IPv4 or IPv6) */
bool tr_addressIsIP(char const* address);

/** @brief return true if the url is a http or https or UDP url that Transmission understands */
bool tr_urlIsValidTracker(std::string_view url);

/** @brief return true if the url is a [ http, https, ftp, sftp ] url that Transmission understands */
bool tr_urlIsValid(std::string_view url);

struct tr_url_parsed_t
{
    std::string_view scheme;
    std::string_view host;
    std::string_view path;
    std::string_view portstr;
    int port = -1;
};

std::optional<tr_url_parsed_t> tr_urlParse(std::string_view url);

// like tr_urlParse(), but with the added constraint that 'scheme'
// must be one we that Transmission supports for announce and scrape
std::optional<tr_url_parsed_t> tr_urlParseTracker(std::string_view url);

struct tr_url_query_walk_t
{
    std::string_view key;
    std::string_view value;
    std::string_view query_remain;
};

tr_url_query_walk_t tr_urlNextQueryPair(std::string_view query_remain);

/** @brief parse a URL into its component parts
    @return True on success or false if an error occurred */
bool tr_urlParse(char const* url, size_t url_len, char** setme_scheme, char** setme_host, int* setme_port, char** setme_path)
    TR_GNUC_NONNULL(1);

void tr_http_escape(struct evbuffer* out, std::string_view str, bool escape_reserved);

void tr_http_escape_sha1(char* out, uint8_t const* sha1_digest);

void tr_http_escape_sha1(char* out, tr_sha1_digest_t const& digest);

char* tr_http_unescape(char const* str, size_t len);

char const* tr_webGetResponseStr(long response_code);
