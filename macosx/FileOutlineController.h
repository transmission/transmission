// This file Copyright (c) 2008-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@class Torrent;
@class FileOutlineView;

@interface FileOutlineController : NSObject
{
    Torrent* fTorrent;
    NSMutableArray* fFileList;

    IBOutlet FileOutlineView* fOutline;

    NSString* fFilterText;
}

@property(nonatomic, readonly) FileOutlineView* outlineView;

- (void)setTorrent:(Torrent*)torrent;

- (void)setFilterText:(NSString*)text;

- (void)refresh;

- (void)setCheck:(id)sender;
- (void)setOnlySelectedCheck:(id)sender;
- (void)checkAll;
- (void)uncheckAll;
- (void)setPriority:(id)sender;

- (void)revealFile:(id)sender;

- (void)renameSelected:(id)sender;

@end
