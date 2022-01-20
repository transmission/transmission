// This file Copyright (c) 2008-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#include <libtransmission/transmission.h>

@class PrefsController;

@interface BlocklistDownloaderViewController : NSObject
{
    PrefsController* fPrefsController;

    IBOutlet NSWindow* fStatusWindow;
    IBOutlet NSProgressIndicator* fProgressBar;
    IBOutlet NSTextField* fTextField;
    IBOutlet NSButton* fButton;
}

+ (void)downloadWithPrefsController:(PrefsController*)prefsController;

- (void)cancelDownload:(id)sender;

- (void)setStatusStarting;
- (void)setStatusProgressForCurrentSize:(NSUInteger)currentSize expectedSize:(long long)expectedSize;
- (void)setStatusProcessing;

- (void)setFinished;
- (void)setFailed:(NSString*)error;

@end
