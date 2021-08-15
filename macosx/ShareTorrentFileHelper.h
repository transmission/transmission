//
//  ShareTorrentFileHelper.h
//  Transmission
//
//  Created by Mitchell Livingston on 1/10/14.
//  Copyright (c) 2014 The Transmission Project. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface ShareTorrentFileHelper : NSObject

@property(nonatomic, class, readonly) ShareTorrentFileHelper* sharedHelper;

@property(nonatomic, readonly) NSArray* shareTorrentURLs;
@property(nonatomic, readonly) NSArray* menuItems;

@end
