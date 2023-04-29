// This file Copyright © 2008-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface BlocklistScheduler : NSObject

@property(nonatomic, class, readonly) BlocklistScheduler* scheduler;

- (void)updateSchedule;
- (void)cancelSchedule;

@end
