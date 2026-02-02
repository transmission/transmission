// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#if __has_feature(modules)
@import AppKit;
#else
#import <AppKit/AppKit.h>
#endif

#include <libtransmission/transmission.h>

#include <libtransmission/utils.h>

int main(int argc, char** argv)
{
    tr_lib_init();

    tr_locale_set_global("");

    return NSApplicationMain(argc, (char const**)argv);
}
