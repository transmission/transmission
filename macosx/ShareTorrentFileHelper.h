// This file Copyright © 2014-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.
// Created by Mitchell Livingston on 1/10/14.

#import <AppKit/AppKit.h>

@interface ShareTorrentFileHelper : NSObject

@property(nonatomic, class, readonly) ShareTorrentFileHelper* sharedHelper;

@property(nonatomic, readonly) NSArray<NSURL*>* shareTorrentURLs;
@property(nonatomic, readonly) NSArray<NSMenuItem*>* menuItems;

@end
