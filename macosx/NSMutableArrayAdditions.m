/******************************************************************************
 * Copyright (c) 2011-2012 Transmission authors and contributors
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

#import "NSMutableArrayAdditions.h"

@implementation NSMutableArray (NSMutableArrayAdditions)

/*
 Note: This assumes Apple implemented this as an array under the hood.
 If the underlying data structure is a linked-list, for example, then this might be less
 efficient than simply removing the object and re-adding it.
 */
- (void) moveObjectAtIndex: (NSUInteger) fromIndex toIndex: (NSUInteger) toIndex
{
    if (fromIndex == toIndex)
        return;

    id object = self[fromIndex];

    //shift objects - more efficient than simply removing the object and re-inserting the object
    if (fromIndex < toIndex)
    {
        for (NSUInteger i = fromIndex; i < toIndex; ++i)
            self[i] = self[i+1];
    }
    else
    {
        for (NSUInteger i = fromIndex; i > toIndex; --i)
            self[i] = self[i-1];
    }
    self[toIndex] = object;

}

@end
