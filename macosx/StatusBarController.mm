// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "StatusBarController.h"
#import "NSStringAdditions.h"
#import "Utils.h"

typedef NSString* StatusRatioType NS_TYPED_EXTENSIBLE_ENUM;

static StatusRatioType const StatusRatioTypeTotal = @"RatioTotal";
static StatusRatioType const StatusRatioTypeSession = @"RatioSession";

typedef NSString* StatusTransferType NS_TYPED_EXTENSIBLE_ENUM;

static StatusTransferType const StatusTransferTypeTotal = @"TransferTotal";
static StatusTransferType const StatusTransferTypeSession = @"TransferSession";

typedef NS_ENUM(NSUInteger, StatusTag) {
    StatusTagTotalRatio = 0,
    StatusTagSessionRatio = 1,
    StatusTagTotalTransfer = 2,
    StatusTagSessionTransfer = 3
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

@end

@implementation StatusBarController

static NSArray<NSString*>* StatusBarStatOrder()
{
    static NSArray<NSString*>* const kOrder = @[ StatusTransferTypeSession, StatusTransferTypeTotal, StatusRatioTypeSession, StatusRatioTypeTotal ];
    return kOrder;
}

- (NSString*)statIdentifierForTag:(StatusTag)tag
{
    switch (tag)
    {
    case StatusTagTotalRatio:
        return StatusRatioTypeTotal;
    case StatusTagSessionRatio:
        return StatusRatioTypeSession;
    case StatusTagTotalTransfer:
        return StatusTransferTypeTotal;
    case StatusTagSessionTransfer:
        return StatusTransferTypeSession;
    default:
        NSAssert1(NO, @"Unknown status label tag received: %ld", (long)tag);
        return StatusRatioTypeTotal;
    }
}

- (NSArray<NSString*>*)enabledStatusBarStats
{
    NSUserDefaults* const defaults = NSUserDefaults.standardUserDefaults;
    NSArray<NSString*>* stored = [defaults arrayForKey:@"StatusBarStats"];
    if (stored.count == 0)
    {
        NSString* const statusLabel = [defaults stringForKey:@"StatusLabel"] ?: StatusRatioTypeTotal;
        return @[ statusLabel ];
    }

    NSMutableArray<NSString*>* const enabled = [NSMutableArray array];
    NSSet<NSString*>* const requested = [NSSet setWithArray:stored];
    for (NSString* stat in StatusBarStatOrder())
    {
        if ([requested containsObject:stat])
        {
            [enabled addObject:stat];
        }
    }

    if (enabled.count == 0)
    {
        NSString* const statusLabel = [defaults stringForKey:@"StatusLabel"] ?: StatusRatioTypeTotal;
        return @[ statusLabel ];
    }

    return enabled;
}

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
    [super awakeFromNib];
    //localize menu items
    [self.fStatusButton.menu itemWithTag:StatusTagTotalRatio].title = NSLocalizedString(@"Total Ratio", "Status Bar -> status menu");
    [self.fStatusButton.menu itemWithTag:StatusTagSessionRatio].title = NSLocalizedString(@"Session Ratio", "Status Bar -> status menu");
    [self.fStatusButton.menu itemWithTag:StatusTagTotalTransfer].title = NSLocalizedString(@"Total Transfer", "Status Bar -> status menu");
    [self.fStatusButton.menu itemWithTag:StatusTagSessionTransfer].title = NSLocalizedString(@"Session Transfer", "Status Bar -> status menu");

    self.fStatusButton.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalDLField.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalULField.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalDLImageView.cell.backgroundStyle = NSBackgroundStyleRaised;
    self.fTotalULImageView.cell.backgroundStyle = NSBackgroundStyleRaised;

    [self updateSpeedFieldsToolTips];

    //update when speed limits are changed
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateSpeedFieldsToolTips) name:@"SpeedLimitUpdate"
                                             object:nil];
}

- (void)updateWithDownload:(CGFloat)dlRate upload:(CGFloat)ulRate
{
    //set rates
    if (!isSpeedEqual(self.fPreviousDownloadRate, dlRate))
    {
        self.fTotalDLField.stringValue = [NSString stringForSpeed:dlRate];
        self.fPreviousDownloadRate = dlRate;
    }

    if (!isSpeedEqual(self.fPreviousUploadRate, ulRate))
    {
        self.fTotalULField.stringValue = [NSString stringForSpeed:ulRate];
        self.fPreviousUploadRate = ulRate;
    }

    NSArray<NSString*>* const statusBarStats = [self enabledStatusBarStats];
    if (statusBarStats.count <= 1)
    {
        NSString* const statusLabel = statusBarStats.firstObject ?: StatusRatioTypeTotal;
        NSString* statusString;
        BOOL total;
        if ((total = [statusLabel isEqualToString:StatusRatioTypeTotal]) || [statusLabel isEqualToString:StatusRatioTypeSession])
        {
            auto const stats = total ? tr_sessionGetCumulativeStats(self.fLib) : tr_sessionGetStats(self.fLib);

            statusString = [NSLocalizedString(@"Ratio", "status bar -> status label")
                stringByAppendingFormat:@": %@", [NSString stringForRatio:stats.ratio]];
        }
        else //StatusTransferTypeTotal or StatusTransferTypeSession
        {
            total = [statusLabel isEqualToString:StatusTransferTypeTotal];

            auto const stats = total ? tr_sessionGetCumulativeStats(self.fLib) : tr_sessionGetStats(self.fLib);

            statusString = [NSString stringWithFormat:@"%@: %@  %@: %@",
                                                      NSLocalizedString(@"DL", "status bar -> status label"),
                                                      [NSString stringForFileSize:stats.downloadedBytes],
                                                      NSLocalizedString(@"UL", "status bar -> status label"),
                                                      [NSString stringForFileSize:stats.uploadedBytes]];
        }

        if (![self.fStatusButton.title isEqualToString:statusString])
        {
            self.fStatusButton.title = statusString;
        }
        return;
    }

    auto const sessionStats = tr_sessionGetStats(self.fLib);
    auto const totalStats = tr_sessionGetCumulativeStats(self.fLib);

    BOOL const hasSessionTransfer = [statusBarStats containsObject:StatusTransferTypeSession];
    BOOL const hasTotalTransfer = [statusBarStats containsObject:StatusTransferTypeTotal];
    BOOL const hasSessionRatio = [statusBarStats containsObject:StatusRatioTypeSession];
    BOOL const hasTotalRatio = [statusBarStats containsObject:StatusRatioTypeTotal];

    BOOL const disambiguateTransfer = hasSessionTransfer && hasTotalTransfer;
    BOOL const disambiguateRatio = hasSessionRatio && hasTotalRatio;

    NSMutableArray<NSString*>* const parts = [NSMutableArray arrayWithCapacity:statusBarStats.count];
    for (NSString* stat in statusBarStats)
    {
        if ([stat isEqualToString:StatusTransferTypeSession])
        {
            NSString* const download = [NSString stringForFileSize:sessionStats.downloadedBytes];
            NSString* const upload = [NSString stringForFileSize:sessionStats.uploadedBytes];
            [parts addObject:[NSString stringWithFormat:@"DL: %@  UL: %@", download, upload]];
        }
        else if ([stat isEqualToString:StatusTransferTypeTotal])
        {
            NSString* const download = [NSString stringForFileSize:totalStats.downloadedBytes];
            NSString* const upload = [NSString stringForFileSize:totalStats.uploadedBytes];
            if (disambiguateTransfer)
            {
                NSString* const prefix = NSLocalizedString(@"Total", "status bar -> status label (disambiguate total transfer)");
                [parts addObject:[NSString stringWithFormat:@"%@ DL: %@  UL: %@", prefix, download, upload]];
            }
            else
            {
                [parts addObject:[NSString stringWithFormat:@"DL: %@  UL: %@", download, upload]];
            }
        }
        else if ([stat isEqualToString:StatusRatioTypeSession])
        {
            NSString* const label = NSLocalizedString(@"Ratio", "status bar -> status label");
            [parts addObject:[NSString stringWithFormat:@"%@: %@", label, [NSString stringForRatio:sessionStats.ratio]]];
        }
        else if ([stat isEqualToString:StatusRatioTypeTotal])
        {
            if (disambiguateRatio)
            {
                NSString* const label = NSLocalizedString(@"Total", "status bar -> status label (disambiguate total ratio)");
                [parts addObject:[NSString stringWithFormat:@"%@: %@", label, [NSString stringForRatio:totalStats.ratio]]];
            }
            else
            {
                NSString* const label = NSLocalizedString(@"Ratio", "status bar -> status label");
                [parts addObject:[NSString stringWithFormat:@"%@: %@", label, [NSString stringForRatio:totalStats.ratio]]];
            }
        }
    }

    NSString* const statusString = [parts componentsJoinedByString:@" • "];
    if (![self.fStatusButton.title isEqualToString:statusString])
    {
        self.fStatusButton.title = statusString;
    }
}

- (void)setStatusLabel:(id)sender
{
    NSString* const stat = [self statIdentifierForTag:(StatusTag)[sender tag]];
    NSEvent* const event = NSApp.currentEvent;
    BOOL const optionHeld = (event.modifierFlags & NSEventModifierFlagOption) != 0;
    NSUserDefaults* const defaults = NSUserDefaults.standardUserDefaults;

    if (optionHeld)
    {
        NSSet<NSString*>* const requested = [NSSet setWithArray:[defaults arrayForKey:@"StatusBarStats"] ?: [self enabledStatusBarStats]];
        NSMutableSet<NSString*>* const modified = [requested mutableCopy];

        if ([modified containsObject:stat])
        {
            if (modified.count > 1)
            {
                [modified removeObject:stat];
            }
        }
        else
        {
            [modified addObject:stat];
        }

        NSMutableArray<NSString*>* const ordered = [NSMutableArray array];
        for (NSString* knownStat in StatusBarStatOrder())
        {
            if ([modified containsObject:knownStat])
            {
                [ordered addObject:knownStat];
            }
        }

        if (ordered.count > 1)
        {
            [defaults setObject:ordered forKey:@"StatusBarStats"];
            [defaults setObject:ordered.firstObject forKey:@"StatusLabel"];
        }
        else
        {
            NSString* const single = ordered.firstObject ?: StatusRatioTypeTotal;
            [defaults removeObjectForKey:@"StatusBarStats"];
            [defaults setObject:single forKey:@"StatusLabel"];
        }
    }
    else
    {
        [defaults removeObjectForKey:@"StatusBarStats"];
        [defaults setObject:stat forKey:@"StatusLabel"];
    }

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
            uploadText = [NSString localizedStringWithFormat:NSLocalizedString(@"%ld KB/s", "Status Bar -> speed tooltip"),
                                                             [NSUserDefaults.standardUserDefaults integerForKey:@"UploadLimit"]];
        }
        else
        {
            uploadText = NSLocalizedString(@"unlimited", "Status Bar -> speed tooltip");
        }

        if ([NSUserDefaults.standardUserDefaults boolForKey:@"CheckDownload"])
        {
            downloadText = [NSString localizedStringWithFormat:NSLocalizedString(@"%ld KB/s", "Status Bar -> speed tooltip"),
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
        NSString* const stat = [self statIdentifierForTag:(StatusTag)menuItem.tag];
        NSArray<NSString*>* const enabled = [self enabledStatusBarStats];
        if (enabled.count > 1)
        {
            menuItem.state = [enabled containsObject:stat] ? NSControlStateValueOn : NSControlStateValueOff;
        }
        else
        {
            menuItem.state = [stat isEqualToString:[NSUserDefaults.standardUserDefaults stringForKey:@"StatusLabel"]] ?
                NSControlStateValueOn :
                NSControlStateValueOff;
        }
        return YES;
    }

    return YES;
}

@end
