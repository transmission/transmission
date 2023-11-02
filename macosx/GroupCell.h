// This file Copyright © 2006-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>
#import "TorrentTableView.h"

@interface GroupCell : NSTableCellView

@property(nonatomic) IBOutlet NSImageView* fGroupIndicatorView;
@property(nonatomic) IBOutlet NSTextField* fGroupTitleField;

@property(nonatomic) IBOutlet NSImageView* fGroupDownloadView;
@property(nonatomic) IBOutlet NSImageView* fGroupUploadAndRatioView;
@property(nonatomic) IBOutlet NSTextField* fGroupDownloadField;
@property(nonatomic) IBOutlet NSTextField* fGroupUploadAndRatioField;

@end
