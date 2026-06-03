// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PeerProgressIndicatorCell.h"
#import "NSStringAdditions.h"

@interface PeerProgressIndicatorCell ()

@property(nonatomic, copy, class, readonly) NSDictionary* attributes;

@end

@implementation PeerProgressIndicatorCell

+ (NSDictionary *)attributes {
    static NSDictionary *attributes = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        NSMutableParagraphStyle* paragraphStyle = [NSParagraphStyle.defaultParagraphStyle mutableCopy];
        paragraphStyle.alignment = NSTextAlignmentRight;

        attributes = @{
            NSFontAttributeName : [NSFont systemFontOfSize:11.0],
            NSForegroundColorAttributeName : NSColor.labelColor,
            NSParagraphStyleAttributeName : paragraphStyle
        };
    });
    return attributes;
}

- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView
{
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"DisplayPeerProgressBarNumber"])
    {
        [[NSString percentString:self.floatValue longDecimals:NO] drawInRect:cellFrame withAttributes:self.class.attributes];
    }
    else
    {
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
