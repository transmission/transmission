// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <string>

#include <curl/curl.h>

// Apply the same certificate and proxy TLS settings to HTTP and WebSocket transfers.
void tr_curl_configure_tls(CURL* curl, std::string const& ca_bundle, bool ssl_verify, bool proxy_ssl_verify);
