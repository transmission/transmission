// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@class BlocklistDownloaderViewController;

typedef NS_ENUM(NSUInteger, BlocklistDownloadState) { //
    BlocklistDownloadStateStart,
    BlocklistDownloadStateDownloading,
    BlocklistDownloadStateProcessing
};

@interface BlocklistDownloader : NSObject<NSURLSessionDownloadDelegate>

@property(nonatomic) BlocklistDownloaderViewController* viewController;
@property(nonatomic, class, readonly) BOOL isRunning;

+ (BlocklistDownloader*)downloader; //starts download if not already occurring

- (void)cancelDownload;

@end
