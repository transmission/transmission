// This file Copyright (c) 2007-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "StatsWindowController.h"
#import "Controller.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define UPDATE_SECONDS 1.0

@interface StatsWindowController (Private)

- (void)updateStats;

- (void)performResetStats;

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

    fTimer = [NSTimer scheduledTimerWithTimeInterval:UPDATE_SECONDS target:self selector:@selector(updateStats) userInfo:nil
                                             repeats:YES];
    [NSRunLoop.currentRunLoop addTimer:fTimer forMode:NSModalPanelRunLoopMode];
    [NSRunLoop.currentRunLoop addTimer:fTimer forMode:NSEventTrackingRunLoopMode];

    self.window.restorationClass = [self class];

    self.window.title = NSLocalizedString(@"Statistics", "Stats window -> title");

    //set label text
    fUploadedLabelField.stringValue = [NSLocalizedString(@"Uploaded", "Stats window -> label") stringByAppendingString:@":"];
    fDownloadedLabelField.stringValue = [NSLocalizedString(@"Downloaded", "Stats window -> label") stringByAppendingString:@":"];
    fRatioLabelField.stringValue = [NSLocalizedString(@"Ratio", "Stats window -> label") stringByAppendingString:@":"];
    fTimeLabelField.stringValue = [NSLocalizedString(@"Running Time", "Stats window -> label") stringByAppendingString:@":"];
    fNumOpenedLabelField.stringValue = [NSLocalizedString(@"Program Started", "Stats window -> label") stringByAppendingString:@":"];

    //size of all labels
    CGFloat const oldWidth = fUploadedLabelField.frame.size.width;

    NSArray* labels = @[ fUploadedLabelField, fDownloadedLabelField, fRatioLabelField, fTimeLabelField, fNumOpenedLabelField ];

    CGFloat maxWidth = CGFLOAT_MIN;
    for (NSTextField* label in labels)
    {
        [label sizeToFit];

        CGFloat const width = label.frame.size.width;
        maxWidth = MAX(maxWidth, width);
    }

    for (NSTextField* label in labels)
    {
        NSRect frame = label.frame;
        frame.size.width = maxWidth;
        label.frame = frame;
    }

    //resize window for new label width - fields are set in nib to adjust correctly
    NSRect windowRect = self.window.frame;
    windowRect.size.width += maxWidth - oldWidth;
    [self.window setFrame:windowRect display:YES];

    //resize reset button
    CGFloat const oldButtonWidth = fResetButton.frame.size.width;

    fResetButton.title = NSLocalizedString(@"Reset", "Stats window -> reset button");
    [fResetButton sizeToFit];

    NSRect buttonFrame = fResetButton.frame;
    buttonFrame.size.width += 10.0;
    buttonFrame.origin.x -= buttonFrame.size.width - oldButtonWidth;
    fResetButton.frame = buttonFrame;
}

- (void)windowWillClose:(id)sender
{
    [fTimer invalidate];
    fTimer = nil;
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
        [alert.window orderOut:nil];

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

@end

@implementation StatsWindowController (Private)

- (void)updateStats
{
    tr_session_stats statsAll, statsSession;
    tr_sessionGetCumulativeStats(fLib, &statsAll);
    tr_sessionGetStats(fLib, &statsSession);

    NSByteCountFormatter* byteFormatter = [[NSByteCountFormatter alloc] init];
    byteFormatter.allowedUnits = NSByteCountFormatterUseBytes;

    fUploadedField.stringValue = [NSString stringForFileSize:statsSession.uploadedBytes];
    fUploadedField.toolTip = [byteFormatter stringFromByteCount:statsSession.uploadedBytes];
    fUploadedAllField.stringValue = [NSString
        stringWithFormat:NSLocalizedString(@"%@ total", "stats total"), [NSString stringForFileSize:statsAll.uploadedBytes]];
    fUploadedAllField.toolTip = [byteFormatter stringFromByteCount:statsAll.uploadedBytes];

    fDownloadedField.stringValue = [NSString stringForFileSize:statsSession.downloadedBytes];
    fDownloadedField.toolTip = [byteFormatter stringFromByteCount:statsSession.downloadedBytes];
    fDownloadedAllField.stringValue = [NSString
        stringWithFormat:NSLocalizedString(@"%@ total", "stats total"), [NSString stringForFileSize:statsAll.downloadedBytes]];
    fDownloadedAllField.toolTip = [byteFormatter stringFromByteCount:statsAll.downloadedBytes];

    fRatioField.stringValue = [NSString stringForRatio:statsSession.ratio];

    NSString* totalRatioString = statsAll.ratio != TR_RATIO_NA ?
        [NSString stringWithFormat:NSLocalizedString(@"%@ total", "stats total"), [NSString stringForRatio:statsAll.ratio]] :
        NSLocalizedString(@"Total N/A", "stats total");
    fRatioAllField.stringValue = totalRatioString;

    static NSDateComponentsFormatter* timeFormatter;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        timeFormatter = [NSDateComponentsFormatter new];
        timeFormatter.unitsStyle = NSDateComponentsFormatterUnitsStyleFull;
        timeFormatter.maximumUnitCount = 3;
        timeFormatter.allowedUnits = NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitWeekOfMonth | NSCalendarUnitDay |
            NSCalendarUnitHour | NSCalendarUnitMinute;
    });

    fTimeField.stringValue = [timeFormatter stringFromTimeInterval:statsSession.secondsActive];
    fTimeAllField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"%@ total", "stats total"),
                                                           [timeFormatter stringFromTimeInterval:statsAll.secondsActive]];

    if (statsAll.sessionCount == 1)
    {
        fNumOpenedField.stringValue = NSLocalizedString(@"1 time", "stats window -> times opened");
    }
    else
    {
        fNumOpenedField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"%@ times", "stats window -> times opened"),
                                                                 [NSString formattedUInteger:statsAll.sessionCount]];
    }
}

- (void)performResetStats
{
    tr_sessionClearStats(fLib);
    [self updateStats];
}

@end
