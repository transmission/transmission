// This file Copyright © 2008-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@class PrefsController;

@interface BlocklistDownloaderViewController : NSObject

+ (void)downloadWithPrefsController:(PrefsController*)prefsController;

- (IBAction)cancelDownload:(id)sender;

- (void)setStatusStarting;
- (void)setStatusProgressForCurrentSize:(NSUInteger)currentSize expectedSize:(long long)expectedSize;
- (void)setStatusProcessing;

- (void)setFinished;
- (void)setFailed:(NSString*)error;

@end
