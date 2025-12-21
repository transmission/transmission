// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "FileCheckCellView.h"
#import "FileListNode.h"
#import "Torrent.h"

@interface FileCheckCellView ()
@property(nonatomic, weak) NSButton* checkButton;
@end

@implementation FileCheckCellView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if ((self = [super initWithFrame:frameRect]))
    {
        // Create checkbox button
        NSButton* checkButton = [[NSButton alloc] initWithFrame:NSZeroRect];
        checkButton.translatesAutoresizingMaskIntoConstraints = NO;
        [checkButton setButtonType:NSButtonTypeSwitch];
        checkButton.title = @"";
        checkButton.allowsMixedState = YES;
        checkButton.target = self;
        checkButton.action = @selector(checkButtonClicked:);
        [self addSubview:checkButton];
        _checkButton = checkButton;

        // Setup constraints
        [NSLayoutConstraint activateConstraints:@[
            [checkButton.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [checkButton.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        ]];
    }
    return self;
}

- (void)setNode:(FileListNode*)node
{
    _node = node;
    [self updateDisplay];
}

- (void)updateDisplay
{
    if (!self.node)
    {
        return;
    }

    FileListNode* node = self.node;
    Torrent* torrent = node.torrent;

    // Update checkbox state
    self.checkButton.state = [torrent checkForFiles:node.indexes];
    self.checkButton.enabled = [torrent canChangeDownloadCheckForFiles:node.indexes];

    // Update tooltip
    [self updateTooltip];
}

- (void)updateTooltip
{
    if (!self.node)
    {
        return;
    }

    NSString* tooltip = nil;
    switch (self.checkButton.state)
    {
    case NSControlStateValueOff:
        tooltip = NSLocalizedString(@"Don't Download", "files tab -> tooltip");
        break;
    case NSControlStateValueOn:
        tooltip = NSLocalizedString(@"Download", "files tab -> tooltip");
        break;
    case NSControlStateValueMixed:
        tooltip = NSLocalizedString(@"Download Some", "files tab -> tooltip");
        break;
    }
    self.checkButton.toolTip = tooltip;
}

- (void)checkButtonClicked:(NSButton*)sender
{
    if (!self.node)
    {
        return;
    }

    FileListNode* node = self.node;
    Torrent* torrent = node.torrent;

    NSIndexSet* indexSet;
    if (NSEvent.modifierFlags & NSEventModifierFlagOption)
    {
        indexSet = [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, torrent.fileCount)];
    }
    else
    {
        indexSet = node.indexes;
    }

    [torrent setFileCheckState:sender.state != NSControlStateValueOff ? NSControlStateValueOn : NSControlStateValueOff
                    forIndexes:indexSet];

    // Notify that we need to refresh
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];
}

@end
