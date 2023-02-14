// This file Copyright © 2008-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@interface BonjourController : NSObject<NSNetServiceDelegate>

@property(nonatomic, class, readonly) BonjourController* defaultController;
@property(nonatomic, class, readonly) BOOL defaultControllerExists;

- (void)startWithPort:(int)port;
- (void)stop;

@end
