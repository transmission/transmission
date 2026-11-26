// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PeerProgressIndicatorCell.h"
#import "NSStringAdditions.h"

@interface PeerProgressIndicatorCell ()

@property(nonatomic, copy) NSDictionary* fAttributes;

@end

@implementation PeerProgressIndicatorCell

- (id)copyWithZone:(NSZone*)zone
{
    PeerProgressIndicatorCell* copy = [super copyWithZone:zone];
    copy->_fAttributes = _fAttributes;

    return copy;
}

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"DisplayPeerProgressBarNumber"])
    {
        if (!self.fAttributes)
        {
            NSMutableParagraphStyle* paragraphStyle = [NSParagraphStyle.defaultParagraphStyle mutableCopy];
            paragraphStyle.alignment = NSTextAlignmentRight;

            self.fAttributes = @{
                NSFontAttributeName : [NSFont systemFontOfSize:11.0],
                NSForegroundColorAttributeName : NSColor.labelColor,
                NSParagraphStyleAttributeName : paragraphStyle
            };
        }

        [[NSString percentString:self.floatValue longDecimals:NO] drawInRect:cellFrame withAttributes:self.fAttributes];
    }
    else
    {
        //attributes not needed anymore
        if (self.fAttributes)
        {
            self.fAttributes = nil;
        }

        [super drawWithFrame:cellFrame inView:controlView];
        if (self.seed)
        {
            NSImage* checkImage = [NSImage imageNamed:@"CompleteCheck"];

            NSSize const imageSize = checkImage.size;
            NSRect const rect = NSMakeRect(
                floor(NSMidX(cellFrame) - imageSize.width * 0.5),
                floor(NSMidY(cellFrame) - imageSize.height * 0.5),
                imageSize.width,
                imageSize.height);

            [checkImage drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0
                    respectFlipped:YES
                             hints:nil];
        }
    }
}

@end

@interface PeerProgressIndicatorCellView ()
@property(nonatomic, strong) NSTextField* textProgressView;
@property(nonatomic, strong) NSLevelIndicator* levelIndicator;
@property(nonatomic, strong) NSImageView* checkImageView;
@end

@implementation PeerProgressIndicatorCellView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect])
    {
        _textProgressView = [[NSTextField alloc] initWithFrame:frameRect];
        _textProgressView.editable = NO;
        _textProgressView.selectable = NO;
        _textProgressView.bordered = NO;
        _textProgressView.drawsBackground = NO;
        _textProgressView.alignment = NSTextAlignmentCenter;
        _textProgressView.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];

        _levelIndicator = [[NSLevelIndicator alloc] initWithFrame:frameRect];
        _levelIndicator.levelIndicatorStyle = NSLevelIndicatorStyleContinuousCapacity;
        _levelIndicator.editable = NO;
        _levelIndicator.criticalValue = 0.3f;
        _levelIndicator.warningValue = 0.7f;
        _levelIndicator.maxValue = 1.0f;

        _checkImageView = [[NSImageView alloc] initWithFrame:frameRect];
        _checkImageView.imageScaling = NSImageScaleProportionallyDown;
        _checkImageView.imageAlignment = NSImageAlignCenter;
        _checkImageView.image = [NSImage imageNamed:@"CompleteCheck"];

        [self addSubview:_textProgressView];
        [self addSubview:_levelIndicator];
        [self addSubview:_checkImageView];

        _textProgressView.translatesAutoresizingMaskIntoConstraints = NO;
        _levelIndicator.translatesAutoresizingMaskIntoConstraints = NO;
        _checkImageView.translatesAutoresizingMaskIntoConstraints = NO;

        [NSLayoutConstraint activateConstraints:@[
            // Text Progress
            [_textProgressView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [_textProgressView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [_textProgressView.topAnchor constraintEqualToAnchor:self.topAnchor],
            [_textProgressView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],

            // Level Indicator
            [_levelIndicator.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:2],
            [_levelIndicator.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-2],
            [_levelIndicator.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_levelIndicator.heightAnchor constraintEqualToAnchor:self.heightAnchor],

            // ImageView
            [_checkImageView.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [_checkImageView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_checkImageView.widthAnchor constraintEqualToAnchor:self.heightAnchor],
            [_checkImageView.heightAnchor constraintEqualToAnchor:self.heightAnchor],
        ]];
    }
    return self;
}

- (void)updateProgress:(float)progressValue isSeed:(BOOL)isSeed
{
    self.textProgressView.stringValue = [NSString percentString:progressValue longDecimals:NO];
    self.levelIndicator.floatValue = progressValue;

    BOOL const showText = [NSUserDefaults.standardUserDefaults boolForKey:@"DisplayPeerProgressBarNumber"];

    self.textProgressView.hidden = showText == NO;
    self.levelIndicator.hidden = showText;
    self.checkImageView.hidden = showText || !isSeed;
}

@end

@interface PeerTextCellView ()
@property(nonatomic, strong) NSTextField* textFieldView;
@end

@implementation PeerTextCellView
- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect])
    {
        _textFieldView = [[NSTextField alloc] initWithFrame:frameRect];
        _textFieldView.editable = NO;
        _textFieldView.selectable = NO;
        _textFieldView.bordered = NO;
        _textFieldView.drawsBackground = NO;
        _textFieldView.alignment = NSTextAlignmentLeft;
        _textFieldView.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightRegular];

        [self addSubview:_textFieldView];

        _textFieldView.translatesAutoresizingMaskIntoConstraints = NO;

        [NSLayoutConstraint activateConstraints:@[
            [_textFieldView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [_textFieldView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [_textFieldView.topAnchor constraintEqualToAnchor:self.topAnchor],
            [_textFieldView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
        ]];
    }
    return self;
}

- (void)updateText:(nullable NSString*)text
{
    self.textFieldView.stringValue = text;
}

@end

@interface EncryptionImageCellView ()
@property(nonatomic, strong) NSImageView* iconView;
@end

@implementation EncryptionImageCellView
- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect])
    {
        _iconView = [[NSImageView alloc] initWithFrame:frameRect];
        _iconView.imageScaling = NSImageScaleProportionallyDown;
        _iconView.imageAlignment = NSImageAlignCenter;
        _iconView.image = [NSImage imageWithSystemSymbolName:@"lock.fill" accessibilityDescription:nil];

        [self addSubview:_iconView];

        _iconView.translatesAutoresizingMaskIntoConstraints = NO;

        [NSLayoutConstraint activateConstraints:@[
            [_iconView.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [_iconView.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
            [_iconView.widthAnchor constraintEqualToAnchor:self.heightAnchor],
            [_iconView.heightAnchor constraintEqualToAnchor:self.heightAnchor],
        ]];
    }
    return self;
}

- (void)updateEncrypted:(BOOL)encrypted
{
    self.iconView.hidden = encrypted == NO;
}
@end
