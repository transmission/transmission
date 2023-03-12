// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "BadgeView.h"
#import "NSStringAdditions.h"
#import "NSImageAdditions.h"

static CGFloat const kBetweenPadding = 2.0;
static NSImage* kWhiteUpArrow = [[NSImage imageNamed:@"UpArrowTemplate"] imageWithColor:NSColor.whiteColor];
static NSImage* kWhiteDownArrow = [[NSImage imageNamed:@"DownArrowTemplate"] imageWithColor:NSColor.whiteColor];
static CGSize kArrowInset;
static CGSize kArrowSize;

typedef NS_ENUM(NSInteger, ArrowDirection) {
    ArrowDirectionUp,
    ArrowDirectionDown,
};

@interface BadgeView ()

@property(nonatomic) NSMutableDictionary* fAttributes;

@property(nonatomic) CGFloat fDownloadRate;
@property(nonatomic) CGFloat fUploadRate;

@end

@implementation BadgeView

- (instancetype)init
{
    if ((self = [super init]))
    {
        _fDownloadRate = 0.0;
        _fUploadRate = 0.0;

        NSShadow* stringShadow = [[NSShadow alloc] init];
        stringShadow.shadowOffset = NSMakeSize(2.0, -2.0);
        stringShadow.shadowBlurRadius = 4.0;

        _fAttributes = [[NSMutableDictionary alloc] initWithCapacity:3];
        _fAttributes[NSForegroundColorAttributeName] = NSColor.whiteColor;
        _fAttributes[NSShadowAttributeName] = stringShadow;
        _fAttributes[NSFontAttributeName] = [NSFont boldSystemFontOfSize:26.0];

        // DownloadBadge and UploadBadge should have the same size
        NSSize badgeSize = [NSImage imageNamed:@"DownloadBadge"].size;
        // DownArrowTemplate and UpArrowTemplate should have the same size
        CGFloat arrowWidthHeightRatio = kWhiteDownArrow.size.width / kWhiteDownArrow.size.height;

        // arrow height equal to font capital letter height + shadow
        CGFloat arrowHeight = [_fAttributes[NSFontAttributeName] capHeight] + 4;

        kArrowInset = { badgeSize.height * 0.2, badgeSize.height * 0.1 };
        kArrowSize = { arrowHeight * arrowWidthHeightRatio, arrowHeight };
    }
    return self;
}

- (BOOL)setRatesWithDownload:(CGFloat)downloadRate upload:(CGFloat)uploadRate
{
    //only needs update if the badges were displayed or are displayed now
    if (self.fDownloadRate == downloadRate && self.fUploadRate == uploadRate)
    {
        return NO;
    }

    self.fDownloadRate = downloadRate;
    self.fUploadRate = uploadRate;
    return YES;
}

- (void)drawRect:(NSRect)rect
{
    [NSApp.applicationIconImage drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];

    BOOL const upload = self.fUploadRate >= 0.1;
    BOOL const download = self.fDownloadRate >= 0.1;
    CGFloat bottom = 0.0;
    if (download)
    {
        NSImage* downloadBadge = [NSImage imageNamed:@"DownloadBadge"];
        [self badge:downloadBadge arrow:ArrowDirectionDown string:[NSString stringForSpeedAbbrevCompact:self.fDownloadRate]
            atHeight:bottom];

        if (upload)
        {
            bottom += downloadBadge.size.height + kBetweenPadding; //upload rate above download rate
        }
    }
    if (upload)
    {
        [self badge:[NSImage imageNamed:@"UploadBadge"] arrow:ArrowDirectionUp
              string:[NSString stringForSpeedAbbrevCompact:self.fUploadRate]
            atHeight:bottom];
    }
}

- (void)badge:(NSImage*)badge arrow:(ArrowDirection)arrowDirection string:(NSString*)string atHeight:(CGFloat)height
{
    // background
    NSRect badgeRect = { { 0.0, height }, badge.size };
    [badge drawInRect:badgeRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];

    //string is in center of image
    NSSize stringSize = [string sizeWithAttributes:self.fAttributes];
    NSRect stringRect;
    stringRect.origin.x = NSMidX(badgeRect) - stringSize.width * 0.5 + kArrowInset.width; // adjust for arrow
    stringRect.origin.y = NSMidY(badgeRect) - stringSize.height * 0.5 + 1.0; // adjust for shadow
    stringRect.size = stringSize;
    [string drawInRect:stringRect withAttributes:self.fAttributes];

    // arrow
    NSImage* arrow = arrowDirection == ArrowDirectionUp ? kWhiteUpArrow : kWhiteDownArrow;
    NSRect arrowRect = { { kArrowInset.width, stringRect.origin.y + kArrowInset.height + (arrowDirection == ArrowDirectionUp ? 0.5 : -0.5) },
                         kArrowSize };
    [arrow drawInRect:arrowRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];
}

@end
