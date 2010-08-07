/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2008-2010 Transmission authors and contributors
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

#import "FileListNode.h"

@interface FileListNode (Private)

- (id) initWithFolder: (BOOL) isFolder name: (NSString *) name path: (NSString *) path;

@end

@implementation FileListNode

- (id) initWithFolderName: (NSString *) name path: (NSString *) path
{
    if ((self = [self initWithFolder: YES name: name path: path]))
    {
        fChildren = [[NSMutableArray alloc] init];
        fSize = 0;
    }
    
    return self;
}

- (id) initWithFileName: (NSString *) name path: (NSString *) path size: (uint64_t) size index: (NSUInteger) index
{
    if ((self = [self initWithFolder: NO name: name path: path]))
    {
        fSize = size;
        [fIndexes addIndex: index];
    }
    
    return self;
}

- (void) insertChild: (FileListNode *) child
{
    NSAssert(fIsFolder, @"method can only be invoked on folders");
    
    [fChildren addObject: child];
}

- (void) insertIndex: (NSUInteger) index withSize: (uint64_t) size
{
    NSAssert(fIsFolder, @"method can only be invoked on folders");
    
    [fIndexes addIndex: index];
    fSize += size;
}

- (id) copyWithZone: (NSZone *) zone
{
    //this object is essentially immutable after initial setup
    return [self retain];
}

- (void) dealloc
{
    [fName release];
    [fPath release];
    [fIndexes release];
    
    [fIcon release];
    
    [fChildren release];
    
    [super dealloc];
}

- (BOOL) isFolder
{
    return fIsFolder;
}

- (NSString *) name
{
    return fName;
}

- (NSString *) path
{
    return fPath;
}

- (NSIndexSet *) indexes
{
    return fIndexes;
}

- (uint64_t) size
{
    return fSize;
}

- (NSImage *) icon
{
    if (!fIcon)
        fIcon = [[[NSWorkspace sharedWorkspace] iconForFileType: fIsFolder ? NSFileTypeForHFSTypeCode('fldr')
                                                                            : [fName pathExtension]] retain];
    return fIcon;
}

- (NSArray *) children
{
    NSAssert(fIsFolder, @"method can only be invoked on folders");
    
    return fChildren;
}

@end

@implementation FileListNode (Private)

- (id) initWithFolder: (BOOL) isFolder name: (NSString *) name path: (NSString *) path
{
    if ((self = [super init]))
    {
        fIsFolder = isFolder;
        fName = [name retain];
        fPath = [path retain];
        
        fIndexes = [[NSMutableIndexSet alloc] init];
    }
    
    return self;
}

@end
