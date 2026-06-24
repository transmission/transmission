// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@class FileListNode;

@interface BaseFileNameCellView : NSTableCellView

@property(nonatomic, weak) FileListNode* node;

@end

@interface FileNameCellView : BaseFileNameCellView
@end

@interface FolderNameCellView : BaseFileNameCellView
@end
