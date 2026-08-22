// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cmath>
#include <vector>

#import "Utils.h"

bool isSpeedEqual(CGFloat old_speed, CGFloat new_speed)
{
    static CGFloat constexpr kSpeedCompareEps = 0.1 / 2;
    return std::abs(new_speed - old_speed) < kSpeedCompareEps;
}

bool isRatioEqual(CGFloat old_ratio, CGFloat new_ratio)
{
    static CGFloat constexpr kRatioCompareEps = 0.01 / 2;
    return std::abs(new_ratio - old_ratio) < kRatioCompareEps;
}

namespace
{

BOOL isMagnetURLString(NSString* url_string)
{
    return [url_string rangeOfString:@"magnet:" options:(NSAnchoredSearch | NSCaseInsensitiveSearch)].location != NSNotFound;
}

} // namespace

NSArray<NSString*>* torrentURLStringsFromPasteboard(NSArray<NSString*>* strings, NSArray<NSURL*>* urls)
{
    BOOL found_magnet = NO;
    NSArray<NSString*>* string_urls = torrentURLStringsFromPasteboardStrings(strings, &found_magnet);

    if (found_magnet)
    {
        NSMutableArray<NSString*>* url_strings = [NSMutableArray array];
        for (NSURL* url in urls)
        {
            NSString* url_string = url.absoluteString;
            if (isMagnetURLString(url_string))
            {
                [url_strings addObject:url_string];
            }
        }

        if (url_strings.count == 0)
        {
            [url_strings addObjectsFromArray:string_urls];
        }
        else
        {
            for (NSString* url_string in string_urls)
            {
                if (!isMagnetURLString(url_string))
                {
                    [url_strings addObject:url_string];
                }
            }
        }

        return url_strings;
    }

    if (urls.count > 0)
    {
        NSMutableArray<NSString*>* url_strings = [NSMutableArray array];
        for (NSURL* url in urls)
        {
            [url_strings addObject:url.absoluteString];
        }

        return url_strings;
    }

    return string_urls;
}

NSArray<NSString*>* torrentURLStringsFromPasteboardStrings(NSArray<NSString*>* strings, BOOL* found_magnet)
{
    if (found_magnet != nullptr)
    {
        *found_magnet = NO;
    }

    if (strings.count == 0)
    {
        return @[];
    }

    NSDataDetector* link_detector = [NSDataDetector dataDetectorWithTypes:NSTextCheckingTypeLink error:nil];
    // https://www.bittorrent.org/beps/bep_0009.html defines the magnet URI format as `magnet:?query` where query is non-empty.
    // https://datatracker.ietf.org/doc/html/rfc3986 defines the query format rigorously as `([!$\&-;=?-Z_a-z~]|%[0-9A-F]{2})*`.
    // But `tr_urlParse` acknowledges that magnet links can be malformed by "not escaping text in the display name".
    // Those malformed magnets aren't URI anymore, and since a display name can potentially contain any Unicode except '/' (see `isUnixReservedChar`), we may want to be liberal on what we accept.
    // In practice, copy-pasted magnets might most often be separated by Horizontal tab, Line feed, Carriage Return, Space, XML delimiters '<' '>', JSON delimiter '"' and Markdown delimiter '`'.
    // But for now, we'll keep the historical separator choice from 8392476b30491ffe7d8d64210f5cf3c3dd1d69ca, whitespaceAndNewlineCharacterSet, which is `[\p{Z}\v]`.
    NSRegularExpression* magnet_detector = [NSRegularExpression regularExpressionWithPattern:@"magnet:?([^\\p{Z}\\v])+"
                                                                                     options:kNilOptions
                                                                                       error:nil];

    NSMutableArray<NSString*>* url_strings = [NSMutableArray array];
    for (NSString* item_string in strings)
    {
        NSMutableIndexSet* magnet_indexes = [NSMutableIndexSet indexSet];
        std::vector<std::pair<NSUInteger, NSString*>> item_url_strings;

        for (NSTextCheckingResult* result in [magnet_detector matchesInString:item_string options:0
                                                                        range:NSMakeRange(0, item_string.length)])
        {
            if (found_magnet != nullptr)
            {
                *found_magnet = YES;
            }

            [magnet_indexes addIndexesInRange:result.range];
            item_url_strings.emplace_back(result.range.location, [item_string substringWithRange:result.range]);
        }

        // Open links that are not part of a magnet URI. NSDataDetector can otherwise
        // report tracker parameters inside a magnet as standalone URLs.
        for (NSTextCheckingResult* result in [link_detector matchesInString:item_string options:0
                                                                      range:NSMakeRange(0, item_string.length)])
        {
            if (![magnet_indexes intersectsIndexesInRange:result.range])
            {
                item_url_strings.emplace_back(result.range.location, result.URL.absoluteString);
            }
        }

        std::sort(
            std::begin(item_url_strings),
            std::end(item_url_strings),
            [](auto const& lhs, auto const& rhs) { return lhs.first < rhs.first; });

        for (auto const& url_pair : item_url_strings)
        {
            [url_strings addObject:url_pair.second];
        }
    }

    return url_strings;
}
