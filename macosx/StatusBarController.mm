// This file Copyright © 2011-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>

#import "StatusBarController.h"
#import "NSStringAdditions.h"

#define STATUS_RATIO_TOTAL @"RatioTotal"
#define STATUS_RATIO_SESSION @"RatioSession"
#define STATUS_TRANSFER_TOTAL @"TransferTotal"
#define STATUS_TRANSFER_SESSION @"TransferSession"

typedef NS_ENUM(unsigned int, statusTag) {
    STATUS_RATIO_TOTAL_TAG = 0,
    STATUS_RATIO_SESSION_TAG = 1,
    STATUS_TRANSFER_TOTAL_TAG = 2,
    STATUS_TRANSFER_SESSION_TAG = 3
};

@interface StatusBarController ()

@property(nonatomic) IBOutlet NSButton* fStatusButton;
@property(nonatomic) IBOutlet NSTextField* fTotalDLField;
@property(nonatomic) IBOutlet NSTextField* fTotalULField;
@property(nonatomic) IBOutlet NSImageView* fTotalDLImageView;
@property(nonatomic) IBOutlet NSImageView* fTotalULImageView;

@property(nonatomic, readonly) tr_session* fLib;

@property(nonatomic) CGFloat fPreviousDownloadRate;
@property(nonatomic) CGFloat fPreviousUploadRate;

- (void)resizeStatusButton;

@end

@implementation StatusBarController

- (instancetype)initWithLib:(tr_session*)lib
{
    if ((self = [super initWithNibName:@"StatusBar" bundle:nil]))
    {
        _fLib = lib;

        _fPreviousDownloadRate = -1.0;
        _fPreviousUploadRate = -1.0;
    }

    return self;
}

- (void)awakeFromNib
{
    //localize menu items
    [self.fStatusButton.menu itemWithTag:STATUS_RATIO_TOTAL_TAG].title = NSLocalizedString(@"Total Ratio", "Status Bar -> status menu");
    [self.fStatusButton.menu itemWithTag:STATUS_RATIO_SESSION_TAG].title = NSLocalizedString(@"Session Ratio", "Status Bar -> status menu");
    [self.fStatusButton.menu itemWithTag:STATUS_TRANSFER_TOTAL_TAG].title = NSLocalizedString(@"Total Transfer", "Status Bar -> status menu");
    [self.fStatusButton.menu itemWithTag:STATUS_TRANSFER_SESSION_TAG].title = NSLocalizedString(@"Session Transfer", "Status Bar -> status menu");

    self.fStatusButton.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalDLField.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalULField.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalDLImageView.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalULImageView.cell.backgroundStyle = NSBackgroundStyleRaised;

    [self updateSpeedFieldsToolTips];

    //update when speed limits are changed
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateSpeedFieldsToolTips) name:@"SpeedLimitUpdate"
                                             object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(resizeStatusButton)
                                               name:NSWindowDidResizeNotification
                                             object:self.view.window];
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)updateWithDownload:(CGFloat)dlRate upload:(CGFloat)ulRate
{
    //set rates
    if (dlRate != self.fPreviousDownloadRate)
    {
        self.fTotalDLField.stringValue = [NSString stringForSpeed:dlRate];
        self.fPreviousDownloadRate = dlRate;
    }

    if (ulRate != self.fPreviousUploadRate)
    {
        self.fTotalULField.stringValue = [NSString stringForSpeed:ulRate];
        self.fPreviousUploadRate = ulRate;
    }

    //set status button text
    NSString *statusLabel = [NSUserDefaults.standardUserDefaults stringForKey:@"StatusLabel"], *statusString;
    BOOL total;
    if ((total = [statusLabel isEqualToString:STATUS_RATIO_TOTAL]) || [statusLabel isEqualToString:STATUS_RATIO_SESSION])
    {
        tr_session_stats stats;
        if (total)
        {
            tr_sessionGetCumulativeStats(self.fLib, &stats);
        }
        else
        {
            tr_sessionGetStats(self.fLib, &stats);
        }

        statusString = [NSLocalizedString(@"Ratio", "status bar -> status label")
            stringByAppendingFormat:@": %@", [NSString stringForRatio:stats.ratio]];
    }
    else //STATUS_TRANSFER_TOTAL or STATUS_TRANSFER_SESSION
    {
        total = [statusLabel isEqualToString:STATUS_TRANSFER_TOTAL];

        tr_session_stats stats;
        if (total)
        {
            tr_sessionGetCumulativeStats(self.fLib, &stats);
        }
        else
        {
            tr_sessionGetStats(self.fLib, &stats);
        }

        statusString = [NSString stringWithFormat:@"%@: %@  %@: %@",
                                                  NSLocalizedString(@"DL", "status bar -> status label"),
                                                  [NSString stringForFileSize:stats.downloadedBytes],
                                                  NSLocalizedString(@"UL", "status bar -> status label"),
                                                  [NSString stringForFileSize:stats.uploadedBytes]];
    }

    if (![self.fStatusButton.title isEqualToString:statusString])
    {
        self.fStatusButton.title = statusString;
        [self resizeStatusButton];
    }
}

- (void)setStatusLabel:(id)sender
{
    NSString* statusLabel;
    switch ([sender tag])
    {
    case STATUS_RATIO_TOTAL_TAG:
        statusLabel = STATUS_RATIO_TOTAL;
        break;
    case STATUS_RATIO_SESSION_TAG:
        statusLabel = STATUS_RATIO_SESSION;
        break;
    case STATUS_TRANSFER_TOTAL_TAG:
        statusLabel = STATUS_TRANSFER_TOTAL;
        break;
    case STATUS_TRANSFER_SESSION_TAG:
        statusLabel = STATUS_TRANSFER_SESSION;
        break;
    default:
        NSAssert1(NO, @"Unknown status label tag received: %ld", [sender tag]);
        return;
    }

    [NSUserDefaults.standardUserDefaults setObject:statusLabel forKey:@"StatusLabel"];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];
}

- (void)updateSpeedFieldsToolTips
{
    NSString *uploadText, *downloadText;

    if ([NSUserDefaults.standardUserDefaults boolForKey:@"SpeedLimit"])
    {
        NSString* speedString = [NSString stringWithFormat:@"%@ (%@)",
                                                           NSLocalizedString(@"%ld KB/s", "Status Bar -> speed tooltip"),
                                                           NSLocalizedString(@"Speed Limit", "Status Bar -> speed tooltip")];

        uploadText = [NSString stringWithFormat:speedString, [NSUserDefaults.standardUserDefaults integerForKey:@"SpeedLimitUploadLimit"]];
        downloadText = [NSString stringWithFormat:speedString, [NSUserDefaults.standardUserDefaults integerForKey:@"SpeedLimitDownloadLimit"]];
    }
    else
    {
        if ([NSUserDefaults.standardUserDefaults boolForKey:@"CheckUpload"])
        {
            uploadText = [NSString stringWithFormat:NSLocalizedString(@"%ld KB/s", "Status Bar -> speed tooltip"),
                                                    [NSUserDefaults.standardUserDefaults integerForKey:@"UploadLimit"]];
        }
        else
        {
            uploadText = NSLocalizedString(@"unlimited", "Status Bar -> speed tooltip");
        }

        if ([NSUserDefaults.standardUserDefaults boolForKey:@"CheckDownload"])
        {
            downloadText = [NSString stringWithFormat:NSLocalizedString(@"%ld KB/s", "Status Bar -> speed tooltip"),
                                                      [NSUserDefaults.standardUserDefaults integerForKey:@"DownloadLimit"]];
        }
        else
        {
            downloadText = NSLocalizedString(@"unlimited", "Status Bar -> speed tooltip");
        }
    }

    uploadText = [NSLocalizedString(@"Global upload limit", "Status Bar -> speed tooltip") stringByAppendingFormat:@": %@", uploadText];
    downloadText = [NSLocalizedString(@"Global download limit", "Status Bar -> speed tooltip") stringByAppendingFormat:@": %@", downloadText];

    self.fTotalULField.toolTip = uploadText;
    self.fTotalDLField.toolTip = downloadText;
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL const action = menuItem.action;

    //enable sort options
    if (action == @selector(setStatusLabel:))
    {
        NSString* statusLabel;
        switch (menuItem.tag)
        {
        case STATUS_RATIO_TOTAL_TAG:
            statusLabel = STATUS_RATIO_TOTAL;
            break;
        case STATUS_RATIO_SESSION_TAG:
            statusLabel = STATUS_RATIO_SESSION;
            break;
        case STATUS_TRANSFER_TOTAL_TAG:
            statusLabel = STATUS_TRANSFER_TOTAL;
            break;
        case STATUS_TRANSFER_SESSION_TAG:
            statusLabel = STATUS_TRANSFER_SESSION;
            break;
        default:
            NSAssert1(NO, @"Unknown status label tag received: %ld", menuItem.tag);
            statusLabel = STATUS_RATIO_TOTAL;
        }

        menuItem.state = [statusLabel isEqualToString:[NSUserDefaults.standardUserDefaults stringForKey:@"StatusLabel"]] ?
            NSControlStateValueOn :
            NSControlStateValueOff;
        return YES;
    }

    return YES;
}

#pragma mark - Private

- (void)resizeStatusButton
{
    [self.fStatusButton sizeToFit];

    //width ends up being too long
    NSRect statusFrame = self.fStatusButton.frame;
    statusFrame.size.width -= 25.0;

    CGFloat const difference = NSMaxX(statusFrame) + 5.0 - NSMinX(self.fTotalDLImageView.frame);
    if (difference > 0.0)
    {
        statusFrame.size.width -= difference;
    }

    self.fStatusButton.frame = statusFrame;
}

@end
