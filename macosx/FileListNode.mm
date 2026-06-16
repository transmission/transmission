// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#if __has_feature(modules)
@import AppKit;
#else
#import <AppKit/AppKit.h>
#endif

#import "FileListNode.h"

@interface FileListNode () {
    @protected
    uint64_t _size;
    @protected
    NSImage *_iconInternal;
}
- (instancetype)initWithName:(NSString*)name path:(NSString*)path torrent:(Torrent*)torrent size:(uint64_t)size;
- (NSImage *)createIcon;
@end

@interface OnlyFileListNode () {
    NSIndexSet* _indexesInternal;
}
- (instancetype)initWithFileName:(NSString*)name
                            path:(NSString*)path
                            size:(uint64_t)size
                           index:(NSUInteger)index
                         torrent:(Torrent*)torrent;
@end

@interface OnlyFolderListNode () {
    NSMutableIndexSet* _indexesInternal;
    NSMutableArray* _childrenInternal;
}
- (instancetype)initWithFolderName:(NSString*)name path:(NSString*)path torrent:(Torrent*)torrent;
@end

@implementation FileListNode

- (NSMutableArray<FileListNode *> *)children
{
    return nil;
}

- (void)insertChild:(FileListNode*)child
{
}

- (void)insertIndex:(NSUInteger)index withSize:(uint64_t)size
{
}

- (id)copyWithZone:(NSZone*)zone
{
    //this object is essentially immutable after initial setup
    return self;
}

- (NSString*)description
{
    return @"";
}

- (BOOL)isFolder
{
    return NO;
}

- (NSImage*)createIcon
{
    NSAssert(NO, @"Implement in subclasses");
    return nil;
}

- (NSImage*)icon
{
    if (!_iconInternal)
    {
        _iconInternal = [self createIcon];
    }
    return _iconInternal;
}

- (NSIndexSet*)indexes
{
    return nil;
}

- (BOOL)updateFromOldName:(NSString*)oldName toNewName:(NSString*)newName inPath:(NSString*)path
{
    NSParameterAssert(oldName != nil);
    NSParameterAssert(newName != nil);
    NSParameterAssert(path != nil);

    NSArray* lookupPathComponents = path.pathComponents;
    NSArray* thesePathComponents = self.path.pathComponents;

    if ([lookupPathComponents isEqualToArray:thesePathComponents]) //this node represents what's being renamed
    {
        if ([oldName isEqualToString:self.name])
        {
            _name = [newName copy];
            _iconInternal = nil;
            return YES;
        }
    }
    else if (lookupPathComponents.count < thesePathComponents.count) //what's being renamed is part of this node's path
    {
        lookupPathComponents = [lookupPathComponents arrayByAddingObject:oldName];
        BOOL const allSame = NSNotFound ==
            [lookupPathComponents indexOfObjectWithOptions:NSEnumerationConcurrent
                                               passingTest:^BOOL(NSString* name, NSUInteger idx, BOOL* /*stop*/) {
                                                   return ![name isEqualToString:thesePathComponents[idx]];
                                               }];

        if (allSame)
        {
            NSString* oldPathPrefix = [path stringByAppendingPathComponent:oldName];
            NSString* newPathPrefix = [path stringByAppendingPathComponent:newName];

            _path = [_path stringByReplacingCharactersInRange:NSMakeRange(0, oldPathPrefix.length) withString:newPathPrefix];
            return YES;
        }
    }

    return NO;
}

#pragma mark - Private

- (instancetype)initWithName:(NSString*)name path:(NSString*)path torrent:(Torrent*)torrent size:(uint64_t)size
{
    if ((self = [super init]))
    {
        _name = [name copy];
        _path = [path copy];
        _torrent = torrent;
        _size = size;
    }

    return self;
}

#pragma mark - Class Cluster

+ (instancetype)createWithFolderName:(NSString*)name path:(NSString*)path torrent:(Torrent*)torrent
{
    return [[OnlyFolderListNode alloc] initWithFolderName:name path:path torrent:torrent];
}

+ (instancetype)createWithFileName:(NSString*)name
                            path:(NSString*)path
                            size:(uint64_t)size
                           index:(NSUInteger)index
                         torrent:(Torrent*)torrent
{
    return [[OnlyFileListNode alloc] initWithFileName:name path:path size:size index:index torrent:torrent];
}

@end

@implementation OnlyFileListNode

- (instancetype)initWithFileName:(NSString*)name
                            path:(NSString*)path
                            size:(uint64_t)size
                           index:(NSUInteger)index
                         torrent:(Torrent*)torrent
{
    if (self = [super initWithName:name path:path torrent:torrent size:size])
    {
        _indexesInternal = [NSIndexSet indexSetWithIndex:index];
    }
    return self;
}

- (NSImage*)createIcon
{
    return [NSWorkspace.sharedWorkspace iconForFileType:self.name.pathExtension];
}

- (NSIndexSet*)indexes
{
    return _indexesInternal;
}

- (void)insertChild:(FileListNode*)child
{
    NSAssert(NO, @"method can only be invoked on folders");
}

- (void)insertIndex:(NSUInteger)index withSize:(uint64_t)size
{
    NSAssert(NO, @"method can only be invoked on folders");
}

- (NSString*)description
{
    return [NSString stringWithFormat:@"%@ (%ld)", self.name, _indexesInternal.firstIndex];
}

@end

@implementation OnlyFolderListNode

- (instancetype)initWithFolderName:(NSString*)name path:(NSString*)path torrent:(Torrent*)torrent
{
    if (self = [super initWithName:name path:path torrent:torrent size:0])
    {
        _indexesInternal = [[NSMutableIndexSet alloc] init];
        _childrenInternal = [[NSMutableArray alloc] init];
    }
    return self;
}

- (BOOL)isFolder
{
    return YES;
}

- (NSImage*)createIcon
{
    return [NSWorkspace.sharedWorkspace iconForFileType:NSFileTypeForHFSTypeCode(kGenericFolderIcon)];
}

- (NSIndexSet*)indexes
{
    return _indexesInternal;
}

- (NSMutableArray<FileListNode *> *)children
{
    return _childrenInternal;
}

- (void)insertChild:(FileListNode*)child
{
    [_childrenInternal addObject:child];
}

- (void)insertIndex:(NSUInteger)index withSize:(uint64_t)size
{
    [_indexesInternal addIndex:index];
    _size += size;
}

- (NSString*)description
{
    return [NSString stringWithFormat:@"%@ (folder: %@)", self.name, _indexesInternal];
}

@end
