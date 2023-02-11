// This file Copyright © 2022-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface NSKeyedUnarchiver (NSUnarchiverAdditions)

+ (nullable id)deprecatedUnarchiveObjectWithData:(NSData*)data;

@end

NS_ASSUME_NONNULL_END
