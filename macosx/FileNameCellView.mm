// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#import "FileNameCellView.h"
#import "FileListNode.h"
#import "Torrent.h"
#import "NSStringAdditions.h"

static CGFloat const kPaddingHorizontal = 2.0;
static CGFloat const kImageFolderSize = 16.0;
static CGFloat const kImageIconSize = 32.0;
static CGFloat const kPaddingBetweenImageAndTitle = 4.0;
static CGFloat const kPaddingAboveTitleFile = 2.0;
static CGFloat const kPaddingBelowStatusFile = 2.0;
static CGFloat const kPaddingBetweenNameAndFolderStatus = 4.0;

@interface FileNameCellView ()
@property(nonatomic, weak) NSImageView* iconView;
@property(nonatomic, weak) NSTextField* nameField;
@property(nonatomic, weak) NSTextField* statusField;
@property(nonatomic, strong) NSArray<NSLayoutConstraint*>* dynamicConstraints;
@end

@implementation FileNameCellView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if ((self = [super initWithFrame:frameRect]))
    {
        // Create icon view
        NSImageView* iconView = [[NSImageView alloc] initWithFrame:NSZeroRect];
        iconView.translatesAutoresizingMaskIntoConstraints = NO;
        iconView.imageScaling = NSImageScaleProportionallyDown;
        [self addSubview:iconView];
        _iconView = iconView;

        // Create name field
        NSTextField* nameField = [[NSTextField alloc] initWithFrame:NSZeroRect];
        nameField.translatesAutoresizingMaskIntoConstraints = NO;
        nameField.editable = NO;
        nameField.selectable = NO;
        nameField.bordered = NO;
        nameField.backgroundColor = NSColor.clearColor;
        nameField.font = [NSFont messageFontOfSize:12.0];
        nameField.lineBreakMode = NSLineBreakByTruncatingMiddle;
        [self addSubview:nameField];
        _nameField = nameField;
        self.textField = nameField;

        // Create status field
        NSTextField* statusField = [[NSTextField alloc] initWithFrame:NSZeroRect];
        statusField.translatesAutoresizingMaskIntoConstraints = NO;
        statusField.editable = NO;
        statusField.selectable = NO;
        statusField.bordered = NO;
        statusField.backgroundColor = NSColor.clearColor;
        statusField.font = [NSFont messageFontOfSize:9.0];
        statusField.textColor = NSColor.secondaryLabelColor;
        statusField.lineBreakMode = NSLineBreakByTruncatingTail;
        [self addSubview:statusField];
        _statusField = statusField;

        // Setup constraints
        [self setupConstraints];
    }
    return self;
}

- (void)setupConstraints
{
    NSImageView* iconView = self.iconView;
    NSTextField* nameField = self.nameField;

    // Fixed constraints that don't change
    [NSLayoutConstraint activateConstraints:@[
        // Icon view constraints
        [iconView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:kPaddingHorizontal],
        [iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
        [iconView.widthAnchor constraintEqualToConstant:kImageIconSize],
        [iconView.heightAnchor constraintEqualToConstant:kImageIconSize],

        // Name field leading constraint
        [nameField.leadingAnchor constraintEqualToAnchor:iconView.trailingAnchor constant:kPaddingBetweenImageAndTitle],
    ]];

    self.dynamicConstraints = @[];
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

    // Update icon
    self.iconView.image = node.icon;

    // Update icon size constraints based on folder/file
    CGFloat const imageSize = node.isFolder ? kImageFolderSize : kImageIconSize;
    for (NSLayoutConstraint* constraint in self.iconView.constraints)
    {
        if (constraint.firstAttribute == NSLayoutAttributeWidth || constraint.firstAttribute == NSLayoutAttributeHeight)
        {
            constraint.constant = imageSize;
        }
    }

    // Update name
    self.nameField.stringValue = node.name;

    // Update status
    Torrent* torrent = node.torrent;
    CGFloat const progress = [torrent fileProgress:node];
    NSString* percentString = [NSString percentString:progress longDecimals:YES];

    NSString* status = [NSString stringWithFormat:NSLocalizedString(@"%@ of %@", "Inspector -> Files tab -> file status string"),
                                                  percentString,
                                                  [NSString stringForFileSize:node.size]];
    self.statusField.stringValue = status;

    // Update layout constraints based on folder vs file
    [NSLayoutConstraint deactivateConstraints:self.dynamicConstraints];

    NSTextField* nameField = self.nameField;
    NSTextField* statusField = self.statusField;

    if (node.isFolder)
    {
        // For folders, status appears next to name, both centered
        self.statusField.hidden = NO;
        self.dynamicConstraints = @[
            [nameField.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [nameField.trailingAnchor constraintLessThanOrEqualToAnchor:statusField.leadingAnchor
                                                               constant:-kPaddingBetweenNameAndFolderStatus],

            [statusField.leadingAnchor constraintEqualToAnchor:nameField.trailingAnchor constant:kPaddingBetweenNameAndFolderStatus],
            [statusField.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [statusField.trailingAnchor constraintLessThanOrEqualToAnchor:self.trailingAnchor],
        ];
    }
    else
    {
        // For files, status appears below name
        self.statusField.hidden = NO;
        self.dynamicConstraints = @[
            [nameField.topAnchor constraintEqualToAnchor:self.topAnchor constant:kPaddingAboveTitleFile],
            [nameField.trailingAnchor constraintLessThanOrEqualToAnchor:self.trailingAnchor],

            [statusField.leadingAnchor constraintEqualToAnchor:nameField.leadingAnchor],
            [statusField.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [statusField.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-kPaddingBelowStatusFile],
        ];
    }

    [NSLayoutConstraint activateConstraints:self.dynamicConstraints];

    // Update colors based on background style and check state
    [self updateColors];

    // Update tooltip
    [self updateTooltip];
}

- (void)updateTooltip
{
    if (!self.node)
    {
        return;
    }

    FileListNode* node = self.node;
    Torrent* torrent = node.torrent;

    NSString* path = [torrent fileLocation:node];
    if (!path)
    {
        path = [node.path stringByAppendingPathComponent:node.name];
    }
    self.toolTip = path;
}

- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle
{
    [super setBackgroundStyle:backgroundStyle];
    [self updateColors];
}

- (void)updateColors
{
    if (!self.node)
    {
        return;
    }

    FileListNode* node = self.node;
    Torrent* torrent = node.torrent;

    if (self.backgroundStyle == NSBackgroundStyleEmphasized)
    {
        self.nameField.textColor = NSColor.whiteColor;
        self.statusField.textColor = NSColor.whiteColor;
    }
    else if ([torrent checkForFiles:node.indexes] == NSControlStateValueOff)
    {
        self.nameField.textColor = NSColor.disabledControlTextColor;
        self.statusField.textColor = NSColor.disabledControlTextColor;
    }
    else
    {
        self.nameField.textColor = NSColor.controlTextColor;
        self.statusField.textColor = NSColor.secondaryLabelColor;
    }
}

@end
