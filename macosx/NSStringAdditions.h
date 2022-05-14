// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@interface NSString (NSStringAdditions)

@property(nonatomic, class, readonly) NSString* ellipsis;
- (NSString*)stringByAppendingEllipsis;

+ (NSString*)formattedUInteger:(NSUInteger)value;

+ (NSString*)stringForFileSize:(uint64_t)size;
+ (NSString*)stringForFilePartialSize:(uint64_t)partialSize fullSize:(uint64_t)fullSize;

+ (NSString*)stringForSpeed:(CGFloat)speed;
+ (NSString*)stringForSpeedAbbrev:(CGFloat)speed;
+ (NSString*)stringForRatio:(CGFloat)ratio;

+ (NSString*)percentString:(CGFloat)progress longDecimals:(BOOL)longDecimals;

// simple compare method for strings with numbers (works for IP addresses)
- (NSComparisonResult)compareNumeric:(NSString*)string;

// like componentsSeparatedByCharactersInSet:, but excludes blank values
- (NSArray<NSString*>*)nonEmptyComponentsSeparatedByCharactersInSet:(NSCharacterSet*)separators;

@end
