// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>
#include <string>
#include <string_view>
#include <utf8.h>

// macOS implementation of tr_strv_replace_invalid() that autodetects the encoding.
// This replaces the generic implementation of the function in utils.cc

std::string tr_strv_replace_invalid(std::string_view sv, uint32_t replacement)
{
    // UTF-8 encoding
    NSString* validUTF8 = @(std::string(sv).data());
    if (validUTF8)
    {
        return std::string(validUTF8.UTF8String);
    }

    // autodetection of the encoding (#3434)
    NSString* convertedString;
    NSData* data = [NSData dataWithBytes:(void const*)sv.data() length:sizeof(unsigned char) * sv.length()];
    [NSString stringEncodingForData:data encodingOptions:nil convertedString:&convertedString usedLossyConversion:nil];
    if (convertedString)
    {
        return std::string(convertedString.UTF8String);
    }

    // invalid encoding
    auto out = std::string{};
    out.reserve(std::size(sv));
    utf8::unchecked::replace_invalid(std::data(sv), std::data(sv) + std::size(sv), std::back_inserter(out), replacement);
    return out;
}
