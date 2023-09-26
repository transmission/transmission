// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "StatsWindowController.h"
#import "Controller.h"
#import "NSStringAdditions.h"

static NSTimeInterval const kUpdateSeconds = 1.0;

@interface StatsWindowController ()<NSWindowRestoration>

@property(nonatomic) IBOutlet NSTextField* fUploadedField;
@property(nonatomic) IBOutlet NSTextField* fUploadedAllField;
@property(nonatomic) IBOutlet NSTextField* fDownloadedField;
@property(nonatomic) IBOutlet NSTextField* fDownloadedAllField;
@property(nonatomic) IBOutlet NSTextField* fRatioField;
@property(nonatomic) IBOutlet NSTextField* fRatioAllField;
@property(nonatomic) IBOutlet NSTextField* fTimeField;
@property(nonatomic) IBOutlet NSTextField* fTimeAllField;
@property(nonatomic) IBOutlet NSTextField* fNumOpenedField;
@property(nonatomic) IBOutlet NSTextField* fUploadedLabelField;
@property(nonatomic) IBOutlet NSTextField* fDownloadedLabelField;
@property(nonatomic) IBOutlet NSTextField* fRatioLabelField;
@property(nonatomic) IBOutlet NSTextField* fTimeLabelField;
@property(nonatomic) IBOutlet NSTextField* fNumOpenedLabelField;
@property(nonatomic) IBOutlet NSButton* fResetButton;
@property(nonatomic) NSTimer* fTimer;

@end

@implementation StatsWindowController

StatsWindowController* fStatsWindowInstance = nil;
tr_session* fLib = NULL;

+ (StatsWindowController*)statsWindow
{
    if (!fStatsWindowInstance)
    {
        if ((fStatsWindowInstance = [[self alloc] init]))
        {
            fLib = ((Controller*)NSApp.delegate).sessionHandle;
        }
    }
    return fStatsWindowInstance;
}

- (instancetype)init
{
    return [super initWithWindowNibName:@"StatsWindow"];
}

- (void)awakeFromNib
{
    [self updateStats];

    self.fTimer = [NSTimer scheduledTimerWithTimeInterval:kUpdateSeconds target:self selector:@selector(updateStats)
                                                 userInfo:nil
                                                  repeats:YES];
    [NSRunLoop.currentRunLoop addTimer:self.fTimer forMode:NSModalPanelRunLoopMode];
    [NSRunLoop.currentRunLoop addTimer:self.fTimer forMode:NSEventTrackingRunLoopMode];

    self.window.restorationClass = [self class];

    self.window.title = NSLocalizedString(@"Statistics", "Stats window -> title");

    //disable fullscreen support
    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenNone;

    //set label text
    self.fUploadedLabelField.stringValue = [NSLocalizedString(@"Uploaded", "Stats window -> label") stringByAppendingString:@":"];
    self.fDownloadedLabelField.stringValue = [NSLocalizedString(@"Downloaded", "Stats window -> label") stringByAppendingString:@":"];
    self.fRatioLabelField.stringValue = [NSLocalizedString(@"Ratio", "Stats window -> label") stringByAppendingString:@":"];
    self.fTimeLabelField.stringValue = [NSLocalizedString(@"Running Time", "Stats window -> label") stringByAppendingString:@":"];
    self.fNumOpenedLabelField.stringValue = [NSLocalizedString(@"Program Started", "Stats window -> label") stringByAppendingString:@":"];

    self.fResetButton.title = NSLocalizedString(@"Reset", "Stats window -> reset button");
}

- (void)windowWillClose:(id)sender
{
    [self.fTimer invalidate];
    self.fTimer = nil;
    fStatsWindowInstance = nil;
}

+ (void)restoreWindowWithIdentifier:(NSString*)identifier
                              state:(NSCoder*)state
                  completionHandler:(void (^)(NSWindow*, NSError*))completionHandler
{
    NSAssert1([identifier isEqualToString:@"StatsWindow"], @"Trying to restore unexpected identifier %@", identifier);

    completionHandler(StatsWindowController.statsWindow.window, nil);
}

- (void)resetStats:(id)sender
{
    if (![NSUserDefaults.standardUserDefaults boolForKey:@"WarningResetStats"])
    {
        [self performResetStats];
        return;
    }

    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = NSLocalizedString(@"Are you sure you want to reset usage statistics?", "Stats reset -> title");
    alert.informativeText = NSLocalizedString(
        @"This will clear the global statistics displayed by Transmission."
         " Individual transfer statistics will not be affected.",
        "Stats reset -> message");
    alert.alertStyle = NSAlertStyleWarning;
    [alert addButtonWithTitle:NSLocalizedString(@"Reset", "Stats reset -> button")];
    [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Stats reset -> button")];
    alert.showsSuppressionButton = YES;

    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
        if (alert.suppressionButton.state == NSControlStateValueOn)
        {
            [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningResetStats"];
        }

        if (returnCode == NSAlertFirstButtonReturn)
        {
            [self performResetStats];
        }
    }];
}

- (NSString*)windowFrameAutosaveName
{
    return @"StatsWindow";
}

#pragma mark - Private

- (void)updateStats
{
    auto const statsAll = tr_sessionGetCumulativeStats(fLib);
    auto const statsSession = tr_sessionGetStats(fLib);

    NSByteCountFormatter* byteFormatter = [[NSByteCountFormatter alloc] init];
    byteFormatter.allowedUnits = NSByteCountFormatterUseBytes;

    self.fUploadedField.stringValue = [NSString stringForFileSize:statsSession.uploadedBytes];
    self.fUploadedField.toolTip = [byteFormatter stringFromByteCount:statsSession.uploadedBytes];
    self.fUploadedAllField.stringValue = [NSString
        stringWithFormat:NSLocalizedString(@"%@ total", "stats total"), [NSString stringForFileSize:statsAll.uploadedBytes]];
    self.fUploadedAllField.toolTip = [byteFormatter stringFromByteCount:statsAll.uploadedBytes];

    self.fDownloadedField.stringValue = [NSString stringForFileSize:statsSession.downloadedBytes];
    self.fDownloadedField.toolTip = [byteFormatter stringFromByteCount:statsSession.downloadedBytes];
    self.fDownloadedAllField.stringValue = [NSString
        stringWithFormat:NSLocalizedString(@"%@ total", "stats total"), [NSString stringForFileSize:statsAll.downloadedBytes]];
    self.fDownloadedAllField.toolTip = [byteFormatter stringFromByteCount:statsAll.downloadedBytes];

    self.fRatioField.stringValue = [NSString stringForRatio:statsSession.ratio];

    NSString* totalRatioString = static_cast<int>(statsAll.ratio) != TR_RATIO_NA ?
        [NSString stringWithFormat:NSLocalizedString(@"%@ total", "stats total"), [NSString stringForRatio:statsAll.ratio]] :
        NSLocalizedString(@"Total N/A", "stats total");
    self.fRatioAllField.stringValue = totalRatioString;

    static NSDateComponentsFormatter* timeFormatter;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        timeFormatter = [NSDateComponentsFormatter new];
        timeFormatter.unitsStyle = NSDateComponentsFormatterUnitsStyleFull;
        timeFormatter.maximumUnitCount = 3;
        timeFormatter.allowedUnits = NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitWeekOfMonth | NSCalendarUnitDay |
            NSCalendarUnitHour | NSCalendarUnitMinute;
    });

    self.fTimeField.stringValue = [timeFormatter stringFromTimeInterval:statsSession.secondsActive];
    self.fTimeAllField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"%@ total", "stats total"),
                                                                [timeFormatter stringFromTimeInterval:statsAll.secondsActive]];

    if (statsAll.sessionCount == 1)
    {
        self.fNumOpenedField.stringValue = NSLocalizedString(@"1 time", "stats window -> times opened");
    }
    else
    {
        self.fNumOpenedField.stringValue = [NSString
            localizedStringWithFormat:NSLocalizedString(@"%llu times", "stats window -> times opened"), statsAll.sessionCount];
    }
}

- (void)performResetStats
{
    tr_sessionClearStats(fLib);
    [self updateStats];
}

@end
