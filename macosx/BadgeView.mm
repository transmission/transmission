// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "BadgeView.h"
#import "NSStringAdditions.h"
#import "NSImageAdditions.h"
#import "Utils.h"

static CGFloat const kBetweenPadding = 2.0;
static NSImage* kWhiteUpArrow = [[NSImage imageNamed:@"UpArrowTemplate"] imageWithColor:NSColor.whiteColor];
static NSImage* kWhiteDownArrow = [[NSImage imageNamed:@"DownArrowTemplate"] imageWithColor:NSColor.whiteColor];
static CGSize kArrowInset;
static CGSize kArrowSize;

typedef NS_ENUM(NSInteger, ArrowDirection) {
    ArrowDirectionUp,
    ArrowDirectionDown,
};

@interface BadgeView2 ()

@property(nonatomic) NSMutableDictionary* fAttributes;

@property(nonatomic) CGFloat fDownloadRate;
@property(nonatomic) CGFloat fUploadRate;

@end

@implementation BadgeView2

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
    if (isSpeedEqual(self.fDownloadRate, downloadRate) && isSpeedEqual(self.fUploadRate, uploadRate))
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

#import <QuartzCore/QuartzCore.h>

@interface BadgeView ()

@property(nonatomic, strong) CALayer* downloadGroup;
@property(nonatomic, strong) CATextLayer* downloadText;
@property(nonatomic, strong) CALayer* downloadArrow;

@property(nonatomic, strong) CALayer* uploadGroup;
@property(nonatomic, strong) CATextLayer* uploadText;
@property(nonatomic, strong) CALayer* uploadArrow;

@property(nonatomic) CGFloat fDownloadRate;
@property(nonatomic) CGFloat fUploadRate;

@property(nonatomic) CGPoint arrowInset;
@property(nonatomic) CGSize arrowSize;

@property(nonatomic) NSFont* font;

@end

@implementation BadgeView

- (instancetype)initWithFrame:(NSRect)frameRect
{
    if (self = [super initWithFrame:frameRect])
    {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;

        // AppIcon layer
        self.layer.contents = NSApp.applicationIconImage;

        self.font = [NSFont boldSystemFontOfSize:26.0];

        [self setupArrow];
        [self setupSublayers];
    }
    return self;
}

- (void)setupArrow
{
    NSImage* downImage = [NSImage imageNamed:@"DownArrowTemplate"];
    CGFloat ratio = downImage.size.width / downImage.size.height;
    CGFloat arrowHeight = [self.font capHeight] + 4;
    CGSize arrowSize = CGSizeMake(arrowHeight * ratio, arrowHeight);

    self.arrowSize = arrowSize;

    NSSize badgeSize = [NSImage imageNamed:@"DownloadBadge"].size;
    self.arrowInset = { badgeSize.height * 0.2, badgeSize.height * 0.1 };
}

- (void)setupSublayers
{
    /// group down
    NSDictionary* downloadGroupLayers = [self createBadgeGroupForDownload:YES];
    self.downloadGroup = downloadGroupLayers[@"group"];
    self.downloadText = downloadGroupLayers[@"text"];
    self.downloadArrow = downloadGroupLayers[@"arrow"];

    /// group up
    NSDictionary* uploadGroupLayers = [self createBadgeGroupForDownload:NO];
    self.uploadGroup = uploadGroupLayers[@"group"];
    self.uploadText = uploadGroupLayers[@"text"];
    self.uploadArrow = uploadGroupLayers[@"arrow"];

    [self.layer addSublayer:self.downloadGroup];
    [self.layer addSublayer:self.uploadGroup];
}

- (NSDictionary*)createBadgeGroupForDownload:(BOOL)isDown
{
    CALayer* group = [CALayer layer];
    NSImage* groupImage = [NSImage imageNamed:isDown ? @"DownloadBadge" : @"UploadBadge"];
    group.contents = groupImage;
    group.contentsGravity = kCAGravityResizeAspect;
    CGRect groupFrame = CGRectMake(0, 0, groupImage.size.width, groupImage.size.height);
    group.frame = groupFrame;

    CALayer* arrow = [CALayer layer];
    arrow.contents = (id)[[NSImage imageNamed:isDown ? @"DownArrowTemplate" : @"UpArrowTemplate"] imageWithColor:NSColor.whiteColor];
    CGPoint arrowOrigin = CGPointMake(self.arrowInset.x, self.arrowInset.y + (isDown ? -0.5 : 0.5) + 1.0); // 1.0 for shadow
    arrow.frame = (CGRect){ arrowOrigin, self.arrowSize };

    CATextLayer* text = [CATextLayer layer];
    text.contentsScale = 2.0;
    text.font = (__bridge CFTypeRef)self.font;
    text.fontSize = 26.0;
    text.alignmentMode = kCAAlignmentCenter;
    text.shadowOpacity = 0.5;
    text.shadowOffset = CGSizeMake(2, -2);
    text.shadowRadius = 4;

    CGPoint textOrigin = (CGPoint){ self.arrowInset.x + self.arrowSize.width, 0.0 };
    CGFloat textWidth = groupFrame.size.width - textOrigin.x - groupFrame.size.height / 2; // subtract groupImage.size.height / 2. It is a radius of a badge.
    CGSize textSize = (CGSize){ textWidth, groupFrame.size.height - textOrigin.y };
    text.frame = (CGRect){ textOrigin, textSize };

    [group addSublayer:arrow];
    [group addSublayer:text];
    return @{ @"group" : group, @"text" : text, @"arrow" : arrow };
}

- (BOOL)setRatesWithDownload:(CGFloat)downloadRate upload:(CGFloat)uploadRate
{
    //only needs update if the badges were displayed or are displayed now
    if (isSpeedEqual(self.fDownloadRate, downloadRate) && isSpeedEqual(self.fUploadRate, uploadRate))
    {
        return NO;
    }

    self.fDownloadRate = downloadRate;
    self.fUploadRate = uploadRate;

    [self setNeedsDisplay:YES];

    return YES;
}

- (BOOL)wantsUpdateLayer
{
    return YES;
}

- (void)updateLayer
{
    CGFloat currentBottom = 10.0; // Bottom offset

    self.downloadText.string = [NSString stringForSpeedAbbrevCompact:self.fDownloadRate];
    [self updateBadgeForGroup:self.downloadGroup rate:self.fDownloadRate offset:currentBottom];

    if (self.downloadGroup.hidden == NO)
    {
        currentBottom += self.downloadGroup.frame.size.height + 2.0;
    }

    self.uploadText.string = [NSString stringForSpeedAbbrevCompact:self.fUploadRate];
    [self updateBadgeForGroup:self.uploadGroup rate:self.fUploadRate offset:currentBottom];
}

- (void)updateBadgeForGroup:(CALayer*)group rate:(CGFloat)rate offset:(CGFloat)offset
{
    group.hidden = rate < 0.1;
    group.frame = (CGRect){ CGPointMake(0, offset), group.frame.size };
}

@end
