// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "BadgeView.h"
#import "NSStringAdditions.h"
#import "NSImageAdditions.h"

static CGFloat const kBetweenPadding = 2.0;
static NSImage* whiteUpArrow = [[NSImage imageNamed:@"UpArrowTemplate"] imageWithColor:NSColor.whiteColor];
static NSImage* whiteDownArrow = [[NSImage imageNamed:@"DownArrowTemplate"] imageWithColor:NSColor.whiteColor];

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
        [self badge:downloadBadge arrow:whiteDownArrow string:[NSString stringForSpeedAbbrev:self.fDownloadRate] atHeight:bottom];

        if (upload)
        {
            bottom += downloadBadge.size.height + kBetweenPadding; //upload rate above download rate
        }
    }
    if (upload)
    {
        [self badge:[NSImage imageNamed:@"UploadBadge"] arrow:whiteUpArrow
              string:[NSString stringForSpeedAbbrev:self.fUploadRate]
            atHeight:bottom];
    }
}

- (void)badge:(NSImage*)badge arrow:(NSImage*)arrow string:(NSString*)string atHeight:(CGFloat)height
{
    NSRect badgeRect = { { 0.0, height }, badge.size };
    [badge drawInRect:badgeRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];

    CGFloat arrowInset = badge.size.height * 0.1;
    CGFloat arrowRatio = arrow.size.width / arrow.size.height;
    NSRect arrowRect = { { arrowInset, height + arrowInset }, { badge.size.height * 0.8 * arrowRatio, badge.size.height * 0.8 } };
    [arrow drawInRect:arrowRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];

    //make sure text fits on the badge
    CGFloat fontSize = 26.0;
    NSSize stringSize;
    do
    {
        self.fAttributes[NSFontAttributeName] = [NSFont boldSystemFontOfSize:fontSize];
        stringSize = [string sizeWithAttributes:self.fAttributes];
        fontSize -= 1.0;
    } while (NSWidth(badgeRect) - 2 * NSWidth(arrowRect) - 2 * arrowInset < stringSize.width);

    //string is in center of image
    NSRect stringRect;
    stringRect.origin.x = NSMidX(badgeRect) - stringSize.width * 0.5;
    stringRect.origin.y = NSMidY(badgeRect) - stringSize.height * 0.5 + 1.0; //adjust for shadow
    stringRect.size = stringSize;

    [string drawInRect:stringRect withAttributes:self.fAttributes];
}

@end
