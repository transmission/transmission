/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2012 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import <Cocoa/Cocoa.h>

@interface NSString (NSStringAdditions)

+ (NSString *) ellipsis;
- (NSString *) stringByAppendingEllipsis;

+ (NSString *) formattedUInteger: (NSUInteger) value;

+ (NSString *) stringForFileSize: (uint64_t) size;
+ (NSString *) stringForFilePartialSize: (uint64_t) partialSize fullSize: (uint64_t) fullSize;

+ (NSString *) stringForSpeed: (CGFloat) speed;
+ (NSString *) stringForSpeedAbbrev: (CGFloat) speed;
+ (NSString *) stringForRatio: (CGFloat) ratio;

+ (NSString *) percentString: (CGFloat) progress longDecimals: (BOOL) longDecimals;

+ (NSString *) timeString: (uint64_t) seconds includesTimeRemainingPhrase: (BOOL) includesTimeRemainingPhrase showSeconds: (BOOL) showSeconds;
+ (NSString *) timeString: (uint64_t) seconds includesTimeRemainingPhrase: (BOOL) includesTimeRemainingPhrase showSeconds: (BOOL) showSeconds maxFields: (NSUInteger) max;

- (NSComparisonResult) compareNumeric: (NSString *) string; //simple compare method for strings with numbers (works for IP addresses)

- (NSArray *) betterComponentsSeparatedByCharactersInSet: (NSCharacterSet *) separators; //like componentsSeparatedByCharactersInSet:, but excludes blank values

@end
