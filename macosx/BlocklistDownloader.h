// This file Copyright © 2008-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@class BlocklistDownloaderViewController;

typedef NS_ENUM(unsigned int, blocklistDownloadState) { //
    BLOCKLIST_DL_START,
    BLOCKLIST_DL_DOWNLOADING,
    BLOCKLIST_DL_PROCESSING
};

@interface BlocklistDownloader : NSObject<NSURLSessionDownloadDelegate>

@property(nonatomic) BlocklistDownloaderViewController* viewController;
@property(nonatomic, class, readonly) BOOL isRunning;

+ (BlocklistDownloader*)downloader; //starts download if not already occurring

- (void)cancelDownload;

@end
