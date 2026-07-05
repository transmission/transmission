// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#import "VDKQueue.h"

@protocol AutoImportControllerDelegate;

@interface AutoImportController : NSObject<VDKQueueDelegate>

- (instancetype)initWithDefaults:(NSUserDefaults*)defaults delegate:(id<AutoImportControllerDelegate>)delegate;

@property(nonatomic, readonly) VDKQueue* fileWatcherQueue;

- (void)checkAutoImportDirectory;

@end

@protocol AutoImportControllerDelegate<NSObject>

- (void)autoImportController:(AutoImportController*)controller openTorrentFile:(NSString*)path;

@end
