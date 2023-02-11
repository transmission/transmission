// This file Copyright © 2008-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

@class Torrent;
@class FileOutlineView;

@interface FileOutlineController : NSObject

@property(nonatomic, readonly) FileOutlineView* outlineView;
@property(nonatomic) Torrent* torrent;
@property(nonatomic) NSString* filterText;

- (void)refresh;

- (void)setCheck:(id)sender;
- (void)setOnlySelectedCheck:(id)sender;
- (void)checkAll;
- (void)uncheckAll;
- (void)setPriority:(id)sender;

- (IBAction)revealFile:(id)sender;

- (void)renameSelected:(id)sender;

@end
