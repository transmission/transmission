/******************************************************************************
 * Copyright (c) 2008-2012 Transmission authors and contributors
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

- (id) initWithFolder: (BOOL) isFolder name: (NSString *) name path: (NSString *) path torrent: (Torrent *) torrent;

@end

@implementation FileListNode

#warning remove ivars in header when 64-bit only (or it compiles in 32-bit mode)
@synthesize name = fName;
@synthesize path = fPath;
@synthesize torrent = fTorrent;
@synthesize size = fSize;
@synthesize icon = fIcon;
@synthesize isFolder = fIsFolder;
@synthesize indexes = fIndexes;
@synthesize children = fChildren;

- (id) initWithFolderName: (NSString *) name path: (NSString *) path torrent: (Torrent *) torrent
{
    if ((self = [self initWithFolder: YES name: name path: path torrent: torrent]))
    {
        fChildren = [[NSMutableArray alloc] init];
        fSize = 0;
    }

    return self;
}

- (id) initWithFileName: (NSString *) name path: (NSString *) path size: (uint64_t) size index: (NSUInteger) index torrent: (Torrent *) torrent
{
    if ((self = [self initWithFolder: NO name: name path: path torrent: torrent]))
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
    return self;
}


- (NSString *) description
{
    if (!fIsFolder)
        return [NSString stringWithFormat: @"%@ (%ld)", fName, [fIndexes firstIndex]];
    else
        return [NSString stringWithFormat: @"%@ (folder: %@)", fName, fIndexes];
}

- (NSImage *) icon
{
    if (!fIcon)
        fIcon = [[NSWorkspace sharedWorkspace] iconForFileType: fIsFolder ? NSFileTypeForHFSTypeCode(kGenericFolderIcon)
                                                                            : [fName pathExtension]];
    return fIcon;
}

- (NSMutableArray *) children
{
    NSAssert(fIsFolder, @"method can only be invoked on folders");

    return fChildren;
}

- (BOOL) updateFromOldName: (NSString *) oldName toNewName: (NSString *) newName inPath: (NSString *) path
{
    NSParameterAssert(oldName != nil);
    NSParameterAssert(newName != nil);
    NSParameterAssert(path != nil);

    NSArray * lookupPathComponents = [path pathComponents];
    NSArray * thesePathComponents = [self.path pathComponents];

    if ([lookupPathComponents isEqualToArray: thesePathComponents]) //this node represents what's being renamed
    {
        if ([oldName isEqualToString: self.name])
        {
            fName = [newName copy];
            fIcon = nil;
            return YES;
        }
    }
    else if ([lookupPathComponents count] < [thesePathComponents count]) //what's being renamed is part of this node's path
    {
        lookupPathComponents = [lookupPathComponents arrayByAddingObject: oldName];
        const BOOL allSame = NSNotFound == [lookupPathComponents indexOfObjectWithOptions: NSEnumerationConcurrent passingTest: ^BOOL(NSString * name, NSUInteger idx, BOOL * stop) {
            return ![name isEqualToString: thesePathComponents[idx]];
        }];

        if (allSame)
        {
            NSString * oldPathPrefix = [path stringByAppendingPathComponent: oldName];
            NSString * newPathPrefix = [path stringByAppendingPathComponent: newName];

            fPath = [fPath stringByReplacingCharactersInRange: NSMakeRange(0, [oldPathPrefix length]) withString: newPathPrefix];
            return YES;
        }
    }

    return NO;
}

@end

@implementation FileListNode (Private)

- (id) initWithFolder: (BOOL) isFolder name: (NSString *) name path: (NSString *) path torrent: (Torrent *) torrent
{
    if ((self = [super init]))
    {
        fIsFolder = isFolder;
        fName = [name copy];
        fPath = [path copy];

        fIndexes = [[NSMutableIndexSet alloc] init];

        fTorrent = torrent;
    }

    return self;
}

@end
