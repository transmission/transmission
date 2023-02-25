// This file Copyright © 2022-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "NSDataAdditions.h"

@implementation NSData (Additions)

// hexChars from Peter, Aug 19 '14: https://stackoverflow.com/a/25378464
- (NSString*)hexString
{
    char const* hexChars = "0123456789ABCDEF";
    NSUInteger length = self.length;
    unsigned char const* bytes = (unsigned char const*)self.bytes;
    char* chars = (char*)malloc(length * 2);
    if (chars == NULL)
    {
        // malloc returns null if attempting to allocate more memory than the system can provide. Thanks Cœur
        [NSException raise:@"NSInternalInconsistencyException" format:@"failed malloc" arguments:nil];
        return nil;
    }
    char* s = chars;
    NSUInteger i = length;
    while (i--)
    {
        *s++ = hexChars[*bytes >> 4];
        *s++ = hexChars[*bytes & 0xF];
        bytes++;
    }
    return [[NSString alloc] initWithBytesNoCopy:chars length:length * 2 encoding:NSASCIIStringEncoding freeWhenDone:YES];
}

@end
