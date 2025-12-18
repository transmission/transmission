// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@protocol PowerManagerDelegate<NSObject>

- (void)systemWillSleep;
- (void)systemDidWakeUp;

@end

@interface PowerManager : NSObject

@property(nonatomic, class, readonly) PowerManager* shared;

@property(nonatomic, weak) id<PowerManagerDelegate> delegate;
@property(nonatomic) BOOL shouldPreventSleep;

- (instancetype)init NS_UNAVAILABLE;

- (void)start;
- (void)stop;

@end
