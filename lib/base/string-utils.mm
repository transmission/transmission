// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <string>
#include <string_view>

#include "lib/base/string-utils.h"

// macOS implementation of tr_strv_to_utf8_string() that autodetects the encoding.
// This replaces the generic implementation of the function in utils.cc.

std::string tr_strv_to_utf8_string(std::string_view sv)
{
    // local pool for non-app tools like transmission-daemon, transmission-remote, transmission-create, ...
    @autoreleasepool
    {
        // UTF-8 encoding
        NSString* const utf8 = [[NSString alloc] initWithBytes:std::data(sv) length:std::size(sv) encoding:NSUTF8StringEncoding];
        if (utf8 != nil && utf8.UTF8String != nullptr)
        {
            return tr_strv_to_utf8_string(utf8);
        }

        // Try to make a UTF8 string from the detected encoding.
        // 1. Disallow lossy conversion in this step. Lossy conversion
        // is done as last resort later in `tr_strv_replace_invalid()`.
        // 2. We only provide the likely language. If we also supplied
        // suggested encodings, the first one listed could override the
        // others (e.g. cp932 vs cp866).
        NSString* convertedString;
        NSStringEncoding stringEncoding = [NSString
            stringEncodingForData:[NSData dataWithBytes:std::data(sv) length:std::size(sv)]
                  encodingOptions:@{
                      NSStringEncodingDetectionAllowLossyKey : @NO,
                      NSStringEncodingDetectionLikelyLanguageKey : NSLocale.currentLocale.languageCode
                  }
                  convertedString:&convertedString
              usedLossyConversion:nil];

        if (stringEncoding && convertedString != nil && convertedString.UTF8String != nullptr)
        {
            return tr_strv_to_utf8_string(convertedString);
        }

        // invalid encoding
        return tr_strv_replace_invalid(sv);
    }
}

std::string tr_strv_to_utf8_string(NSString* str)
{
    return std::string{ str.UTF8String };
}

NSString* tr_strv_to_utf8_nsstring(std::string_view const sv)
{
    NSString* str = [[NSString alloc] initWithBytes:std::data(sv) length:std::size(sv) encoding:NSUTF8StringEncoding];
    return str ?: @"";
}

NSString* tr_strv_to_utf8_nsstring(std::string_view const sv, NSString* key, NSString* comment)
{
    NSString* str = [[NSString alloc] initWithBytes:std::data(sv) length:std::size(sv) encoding:NSUTF8StringEncoding];
    return str ?: NSLocalizedString(key, comment);
}
