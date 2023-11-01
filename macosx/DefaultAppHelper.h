// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@interface DefaultAppHelper : NSObject

- (BOOL)isDefaultForTorrentFiles;
- (void)setDefaultForTorrentFiles:(void (^_Nullable)())completionHandler;

- (BOOL)isDefaultForMagnetURLs;
- (void)setDefaultForMagnetURLs:(void (^_Nullable)())completionHandler;

@end
