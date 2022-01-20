// This file Copyright © 2007-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "BadgeView.h"
#import "NSStringAdditions.h"

#define BETWEEN_PADDING 2.0

@interface BadgeView (Private)

- (void)badge:(NSImage*)badge string:(NSString*)string atHeight:(CGFloat)height adjustForQuit:(BOOL)quit;

@end

@implementation BadgeView

- (instancetype)initWithLib:(tr_session*)lib
{
    if ((self = [super init]))
    {
        fLib = lib;

        fDownloadRate = 0.0;
        fUploadRate = 0.0;
        fQuitting = NO;
    }
    return self;
}

- (BOOL)setRatesWithDownload:(CGFloat)downloadRate upload:(CGFloat)uploadRate
{
    //only needs update if the badges were displayed or are displayed now
    if (fDownloadRate == downloadRate && fUploadRate == uploadRate)
    {
        return NO;
    }

    fDownloadRate = downloadRate;
    fUploadRate = uploadRate;
    return YES;
}

- (void)setQuitting
{
    fQuitting = YES;
}

- (void)drawRect:(NSRect)rect
{
    [NSApp.applicationIconImage drawInRect:rect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];

    if (fQuitting)
    {
        NSImage* quitBadge = [NSImage imageNamed:@"QuitBadge"];
        [self badge:quitBadge string:NSLocalizedString(@"Quitting", "Dock Badger -> quit")
                 atHeight:(NSHeight(rect) - quitBadge.size.height) * 0.5
            adjustForQuit:YES];
        return;
    }

    BOOL const upload = fUploadRate >= 0.1;
    BOOL const download = fDownloadRate >= 0.1;
    CGFloat bottom = 0.0;
    if (upload)
    {
        NSImage* uploadBadge = [NSImage imageNamed:@"UploadBadge"];
        [self badge:uploadBadge string:[NSString stringForSpeedAbbrev:fUploadRate] atHeight:bottom adjustForQuit:NO];
        if (download)
        {
            bottom += uploadBadge.size.height + BETWEEN_PADDING; //download rate above upload rate
        }
    }
    if (download)
    {
        [self badge:[NSImage imageNamed:@"DownloadBadge"] string:[NSString stringForSpeedAbbrev:fDownloadRate] atHeight:bottom
            adjustForQuit:NO];
    }
}

@end

@implementation BadgeView (Private)

- (void)badge:(NSImage*)badge string:(NSString*)string atHeight:(CGFloat)height adjustForQuit:(BOOL)quit
{
    if (!fAttributes)
    {
        NSShadow* stringShadow = [[NSShadow alloc] init];
        stringShadow.shadowOffset = NSMakeSize(2.0, -2.0);
        stringShadow.shadowBlurRadius = 4.0;

        fAttributes = [[NSMutableDictionary alloc] initWithCapacity:3];
        fAttributes[NSForegroundColorAttributeName] = NSColor.whiteColor;
        fAttributes[NSShadowAttributeName] = stringShadow;
    }

    NSRect badgeRect;
    badgeRect.size = badge.size;
    badgeRect.origin.x = 0.0;
    badgeRect.origin.y = height;

    [badge drawInRect:badgeRect fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1.0];

    //make sure text fits on the badge
    CGFloat fontSize = 26.0;
    NSSize stringSize;
    do
    {
        fAttributes[NSFontAttributeName] = [NSFont boldSystemFontOfSize:fontSize];
        stringSize = [string sizeWithAttributes:fAttributes];
        fontSize -= 1.0;
    } while (NSWidth(badgeRect) < stringSize.width);

    //string is in center of image
    NSRect stringRect;
    stringRect.origin.x = NSMidX(badgeRect) - stringSize.width * 0.5;
    stringRect.origin.y = NSMidY(badgeRect) - stringSize.height * 0.5 + (quit ? 2.0 : 1.0); //adjust for shadow, extra for quit
    stringRect.size = stringSize;

    [string drawInRect:stringRect withAttributes:fAttributes];
}

@end
