// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface PeerProgressIndicatorCell : NSLevelIndicatorCell

@property(nonatomic) BOOL seed;

@end

@interface PeerProgressIndicatorCellView : NSTableCellView

- (void)updateProgress:(float)progressValue isSeed:(BOOL)isSeed;

@end

@interface PeerTextCellView : NSTableCellView
- (void)updateText:(nullable NSString*)text;
@end

@interface EncryptionImageCellView : NSTableCellView
- (void)updateEncrypted:(BOOL)encrypted;
@end
