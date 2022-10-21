// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_ANNOUNCER_MODULE
#error only the libtransmission announcer module should #include this header.
#endif

#include <array>
#include <cstdint> // uint64_t
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "announcer.h"
#include "interned-string.h"
#include "net.h"
#include "peer-mgr.h" // tr_pex

struct tr_url_parsed_t;

void tr_tracker_http_scrape(
    tr_session const* session,
    tr_scrape_request const* req,
    tr_scrape_response_func response_func,
    void* user_data);

void tr_tracker_http_announce(
    tr_session const* session,
    tr_announce_request const* req,
    tr_announce_response_func response_func,
    void* user_data);

void tr_announcerParseHttpAnnounceResponse(tr_announce_response& response, std::string_view benc, std::string_view log_name);

void tr_announcerParseHttpScrapeResponse(tr_scrape_response& response, std::string_view benc, std::string_view log_name);

tr_interned_string tr_announcerGetKey(tr_url_parsed_t const& parsed);
