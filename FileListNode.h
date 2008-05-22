/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2008 Transmission authors and contributors
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


@interface FileListNode : NSObject <NSCopying>
{
    NSString * fName, * fPath;
    BOOL fIsFolder;
    NSMutableIndexSet * fIndexes;
    
    uint64_t fSize;
    NSImage * fIcon;
    
    NSMutableArray * fChildren;
}

- (id) initWithFolderName: (NSString *) name path: (NSString *) path;
- (id) initWithFileName: (NSString *) name path: (NSString *) path size: (uint64_t) size index: (int) index;

- (void) insertChild: (FileListNode *) child;
- (void) insertIndex: (NSUInteger) index;

- (BOOL) isFolder;
- (NSString *) name;
- (NSString *) fullPath;
- (NSIndexSet *) indexes;

- (uint64_t) size;
- (NSImage *) icon;

- (NSArray *) children;

@end
