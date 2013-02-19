/******************************************************************************
 * $Id$
 *
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

#import <Cocoa/Cocoa.h>

@class Torrent;

@interface FileListNode : NSObject <NSCopying>
{
    NSMutableIndexSet * fIndexes;
    
    NSString * fName;
    NSString * fPath;
    Torrent * fTorrent;
    uint64_t fSize;
    NSImage * fIcon;
    BOOL fIsFolder;
    NSMutableArray * fChildren;
}

@property (nonatomic, copy, readonly) NSString * name;
@property (nonatomic, copy, readonly) NSString * path;

@property (nonatomic, readonly) Torrent * torrent;

@property (nonatomic, readonly) uint64_t size;
@property (nonatomic, retain, readonly) NSImage * icon;
@property (nonatomic, readonly) BOOL isFolder;
@property (nonatomic, retain, readonly) NSMutableArray * children;

@property (nonatomic, retain, readonly) NSIndexSet * indexes;

- (id) initWithFolderName: (NSString *) name path: (NSString *) path torrent: (Torrent *) torrent;
- (id) initWithFileName: (NSString *) name path: (NSString *) path size: (uint64_t) size index: (NSUInteger) index torrent: (Torrent *) torrent;

- (void) insertChild: (FileListNode *) child;
- (void) insertIndex: (NSUInteger) index withSize: (uint64_t) size;

- (NSString *) description;

- (BOOL) updateFromOldName: (NSString *) oldName toNewName: (NSString *) newName inPath: (NSString *) path;

@end
