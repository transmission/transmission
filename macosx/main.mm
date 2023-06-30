// This file Copyright © 2005-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

@import AppKit;

#include <libtransmission/transmission.h>

#include <libtransmission/utils.h>

int main(int argc, char** argv)
{
    auto const curl_mgr = tr_get_curl_mgr();

    tr_locale_set_global("");

    return NSApplicationMain(argc, (char const**)argv);
}
