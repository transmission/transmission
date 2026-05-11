// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <gtest/gtest.h>

#import "../../macosx/Utils.h"

namespace
{
NSString* const MagnetWithEncodedTracker =
    @"magnet:?xt=urn:btih:792B3577FED6DD95DBB03F5F0972E821230B834F"
     "&dn=Project.Hail.Mary.2026"
     "&tr=udp%3A%2F%2Ftracker.bittor.pw%3A1337%2Fannounce";
}

TEST(PasteboardURLs, MagnetDoesNotOpenEncodedTrackerFragment)
{
    @autoreleasepool
    {
        BOOL found_magnet = NO;
        NSArray<NSString*>* urls = torrentURLStringsFromPasteboardStrings(@[ MagnetWithEncodedTracker ], &found_magnet);

        EXPECT_TRUE(found_magnet);
        ASSERT_EQ(1UL, urls.count);
        EXPECT_TRUE([urls[0] isEqualToString:MagnetWithEncodedTracker]);
    }
}

TEST(PasteboardURLs, ExtractsMagnetWithoutTrackers)
{
    @autoreleasepool
    {
        NSString* const magnet = @"magnet:?xt=urn:btih:792B3577FED6DD95DBB03F5F0972E821230B834F";

        BOOL found_magnet = NO;
        NSArray<NSString*>* urls = torrentURLStringsFromPasteboardStrings(@[ magnet ], &found_magnet);

        EXPECT_TRUE(found_magnet);
        ASSERT_EQ(1UL, urls.count);
        EXPECT_TRUE([urls[0] isEqualToString:magnet]);
    }
}

TEST(PasteboardURLs, PrefersMagnetURLObjectOverTextMagnet)
{
    @autoreleasepool
    {
        NSString* const text_magnet = @"magnet:?xt=urn:btih:792B3577FED6DD95DBB03F5F0972E821230B834F";
        NSURL* const url_magnet = [NSURL URLWithString:MagnetWithEncodedTracker];

        NSArray<NSString*>* urls = torrentURLStringsFromPasteboard(@[ text_magnet ], @[ url_magnet ]);

        ASSERT_EQ(1UL, urls.count);
        EXPECT_TRUE([urls[0] isEqualToString:MagnetWithEncodedTracker]);
    }
}

TEST(PasteboardURLs, ExtractsStandaloneTorrentURL)
{
    @autoreleasepool
    {
        BOOL found_magnet = NO;
        NSArray<NSString*>* urls = torrentURLStringsFromPasteboardStrings(@[ @"download https://example.com/file.torrent" ], &found_magnet);

        EXPECT_FALSE(found_magnet);
        ASSERT_EQ(1UL, urls.count);
        EXPECT_TRUE([urls[0] isEqualToString:@"https://example.com/file.torrent"]);
    }
}

TEST(PasteboardURLs, PreservesStandaloneURLAndMagnetOrder)
{
    @autoreleasepool
    {
        NSString* const text = [NSString stringWithFormat:@"https://example.com/file.torrent\n%@", MagnetWithEncodedTracker];

        BOOL found_magnet = NO;
        NSArray<NSString*>* urls = torrentURLStringsFromPasteboardStrings(@[ text ], &found_magnet);

        EXPECT_TRUE(found_magnet);
        ASSERT_EQ(2UL, urls.count);
        EXPECT_TRUE([urls[0] isEqualToString:@"https://example.com/file.torrent"]);
        EXPECT_TRUE([urls[1] isEqualToString:MagnetWithEncodedTracker]);
    }
}
