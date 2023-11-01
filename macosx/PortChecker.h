// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSUInteger, PortStatus) { //
    PortStatusChecking,
    PortStatusOpen,
    PortStatusClosed,
    PortStatusError
};

@protocol PortCheckerDelegate;

@interface PortChecker : NSObject

@property(nonatomic, readonly) PortStatus status;

- (instancetype)initForPort:(NSInteger)portNumber delay:(BOOL)delay withDelegate:(NSObject<PortCheckerDelegate>*)delegate;
- (void)cancelProbe;

@end

@protocol PortCheckerDelegate<NSObject>

- (void)portCheckerDidFinishProbing:(PortChecker*)portChecker;

@end

NS_ASSUME_NONNULL_END
