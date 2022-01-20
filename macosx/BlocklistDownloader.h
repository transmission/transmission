// This file Copyright © 2008-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@class BlocklistDownloaderViewController;

typedef NS_ENUM(unsigned int, blocklistDownloadState) { //
    BLOCKLIST_DL_START,
    BLOCKLIST_DL_DOWNLOADING,
    BLOCKLIST_DL_PROCESSING
};

@interface BlocklistDownloader : NSObject<NSURLSessionDownloadDelegate>
{
    NSURLSession* fSession;

    BlocklistDownloaderViewController* fViewController;

    NSUInteger fCurrentSize;
    long long fExpectedSize;

    blocklistDownloadState fState;
}

+ (BlocklistDownloader*)downloader; //starts download if not already occuring
@property(nonatomic, class, readonly) BOOL isRunning;

- (void)setViewController:(BlocklistDownloaderViewController*)viewController;

- (void)cancelDownload;

@end
