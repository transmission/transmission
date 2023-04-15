// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <string>
#include <string_view>

#include "utils.h"

// macOS implementation of tr_strv_convert_utf8() that autodetects the encoding.
// This replaces the generic implementation of the function in utils.cc.

std::string tr_strv_convert_utf8(std::string_view sv)
{
    // local pool for non-app tools like transmission-daemon, transmission-remote, transmission-create, ...
    @autoreleasepool
    {
        // UTF-8 encoding
        char const* validUTF8 = [[NSString alloc] initWithBytes:std::data(sv) length:std::size(sv) encoding:NSUTF8StringEncoding]
                                    .UTF8String;
        if (validUTF8)
        {
            return std::string(validUTF8);
        }

        // autodetection of the encoding (#3434)
        NSString* convertedString;
        NSStringEncoding stringEncoding = [NSString
            stringEncodingForData:[NSData dataWithBytes:std::data(sv) length:std::size(sv)]
                  encodingOptions:@{
                      // We disallow lossy conversion, and will leave it to `utf8::unchecked::replace_invalid`.
                      NSStringEncodingDetectionAllowLossyKey : @NO,
                      // We only set the likely language.
                      // If we were to set suggested encodings, then whatever is listed first would take precedence on all others, making for instance kCFStringEncodingDOSJapanese (cp932) and kCFStringEncodingDOSRussian (cp866) taking priority on each other.
                      NSStringEncodingDetectionLikelyLanguageKey : NSLocale.currentLocale.languageCode
                  }
                  convertedString:&convertedString
              usedLossyConversion:nil];
        if (stringEncoding)
        {
            validUTF8 = convertedString.UTF8String;
            if (validUTF8)
            {
                return std::string(validUTF8);
            }
        }

        // invalid encoding
        return tr_strv_replace_invalid(sv);
    }
}
