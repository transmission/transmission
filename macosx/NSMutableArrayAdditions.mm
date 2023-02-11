// This file Copyright © 2011-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "NSMutableArrayAdditions.h"

@implementation NSMutableArray (NSMutableArrayAdditions)

/*
 Note: This assumes Apple implemented this as an array under the hood.
 If the underlying data structure is a linked-list, for example, then this might be less
 efficient than simply removing the object and re-adding it.
 */
- (void)moveObjectAtIndex:(NSUInteger)fromIndex toIndex:(NSUInteger)toIndex
{
    if (fromIndex == toIndex)
    {
        return;
    }

    id object = self[fromIndex];

    //shift objects - more efficient than simply removing the object and re-inserting the object
    if (fromIndex < toIndex)
    {
        for (NSUInteger i = fromIndex; i < toIndex; ++i)
        {
            self[i] = self[i + 1];
        }
    }
    else
    {
        for (NSUInteger i = fromIndex; i > toIndex; --i)
        {
            self[i] = self[i - 1];
        }
    }
    self[toIndex] = object;
}

@end
