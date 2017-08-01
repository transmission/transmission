/******************************************************************************
 * Copyright (c) 2007-2012 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#import "StatsWindowController.h"
#import "Controller.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define UPDATE_SECONDS 1.0

@interface StatsWindowController (Private)

- (void) updateStats;

- (void) performResetStats;
- (void) resetSheetClosed: (NSAlert *) alert returnCode: (NSInteger) code contextInfo: (void *) info;

@end

@implementation StatsWindowController

StatsWindowController * fStatsWindowInstance = nil;
tr_session * fLib = NULL;
+ (StatsWindowController *) statsWindow
{
    if (!fStatsWindowInstance)
    {
        if ((fStatsWindowInstance = [[self alloc] init]))
        {
            fLib = [(Controller *)[NSApp delegate] sessionHandle];
        }
    }
    return fStatsWindowInstance;
}

- (id) init
{
    return [super initWithWindowNibName: @"StatsWindow"];
}

- (void) awakeFromNib
{
    [self updateStats];

    fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_SECONDS target: self selector: @selector(updateStats) userInfo: nil repeats: YES];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSModalPanelRunLoopMode];
    [[NSRunLoop currentRunLoop] addTimer: fTimer forMode: NSEventTrackingRunLoopMode];

    [[self window] setRestorationClass: [self class]];

    [[self window] setTitle: NSLocalizedString(@"Statistics", "Stats window -> title")];

    //set label text
    [fUploadedLabelField setStringValue: [NSLocalizedString(@"Uploaded", "Stats window -> label") stringByAppendingString: @":"]];
    [fDownloadedLabelField setStringValue: [NSLocalizedString(@"Downloaded", "Stats window -> label") stringByAppendingString: @":"]];
    [fRatioLabelField setStringValue: [NSLocalizedString(@"Ratio", "Stats window -> label") stringByAppendingString: @":"]];
    [fTimeLabelField setStringValue: [NSLocalizedString(@"Running Time", "Stats window -> label") stringByAppendingString: @":"]];
    [fNumOpenedLabelField setStringValue: [NSLocalizedString(@"Program Started", "Stats window -> label") stringByAppendingString: @":"]];

    //size of all labels
    const CGFloat oldWidth = [fUploadedLabelField frame].size.width;

    NSArray * labels = @[fUploadedLabelField, fDownloadedLabelField, fRatioLabelField, fTimeLabelField, fNumOpenedLabelField];

    CGFloat maxWidth = CGFLOAT_MIN;
    for (NSTextField * label in labels)
    {
        [label sizeToFit];

        const CGFloat width = [label frame].size.width;
        maxWidth = MAX(maxWidth, width);
    }

    for (NSTextField * label in labels)
    {
        NSRect frame = [label frame];
        frame.size.width = maxWidth;
        [label setFrame: frame];
    }

    //resize window for new label width - fields are set in nib to adjust correctly
    NSRect windowRect = [[self window] frame];
    windowRect.size.width += maxWidth - oldWidth;
    [[self window] setFrame: windowRect display: YES];

    //resize reset button
    const CGFloat oldButtonWidth = [fResetButton frame].size.width;

    [fResetButton setTitle: NSLocalizedString(@"Reset", "Stats window -> reset button")];
    [fResetButton sizeToFit];

    NSRect buttonFrame = [fResetButton frame];
    buttonFrame.size.width += 10.0;
    buttonFrame.origin.x -= buttonFrame.size.width - oldButtonWidth;
    [fResetButton setFrame: buttonFrame];
}

- (void) windowWillClose: (id) sender
{
    [fTimer invalidate];
    fTimer = nil;
    fStatsWindowInstance = nil;
}

+ (void) restoreWindowWithIdentifier: (NSString *) identifier state: (NSCoder *) state completionHandler: (void (^)(NSWindow *, NSError *)) completionHandler
{
    NSAssert1([identifier isEqualToString: @"StatsWindow"], @"Trying to restore unexpected identifier %@", identifier);

    completionHandler([[StatsWindowController statsWindow] window], nil);
}

- (void) resetStats: (id) sender
{
    if (![[NSUserDefaults standardUserDefaults] boolForKey: @"WarningResetStats"])
    {
        [self performResetStats];
        return;
    }

    NSAlert * alert = [[NSAlert alloc] init];
    [alert setMessageText: NSLocalizedString(@"Are you sure you want to reset usage statistics?", "Stats reset -> title")];
    [alert setInformativeText: NSLocalizedString(@"This will clear the global statistics displayed by Transmission."
                                " Individual transfer statistics will not be affected.", "Stats reset -> message")];
    [alert setAlertStyle: NSWarningAlertStyle];
    [alert addButtonWithTitle: NSLocalizedString(@"Reset", "Stats reset -> button")];
    [alert addButtonWithTitle: NSLocalizedString(@"Cancel", "Stats reset -> button")];
    [alert setShowsSuppressionButton: YES];

    [alert beginSheetModalForWindow: [self window] modalDelegate: self
        didEndSelector: @selector(resetSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (NSString *) windowFrameAutosaveName
{
    return @"StatsWindow";
}

@end

@implementation StatsWindowController (Private)

- (void) updateStats
{
    tr_session_stats statsAll, statsSession;
    tr_sessionGetCumulativeStats(fLib, &statsAll);
    tr_sessionGetStats(fLib, &statsSession);

    NSByteCountFormatter * byteFormatter = [[NSByteCountFormatter alloc] init];
    [byteFormatter setAllowedUnits: NSByteCountFormatterUseBytes];

    [fUploadedField setStringValue: [NSString stringForFileSize: statsSession.uploadedBytes]];
    [fUploadedField setToolTip: [byteFormatter stringFromByteCount: statsSession.uploadedBytes]];
    [fUploadedAllField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ total", "stats total"), [NSString stringForFileSize: statsAll.uploadedBytes]]];
    [fUploadedAllField setToolTip: [byteFormatter stringFromByteCount: statsAll.uploadedBytes]];

    [fDownloadedField setStringValue: [NSString stringForFileSize: statsSession.downloadedBytes]];
    [fDownloadedField setToolTip: [byteFormatter stringFromByteCount: statsSession.downloadedBytes]];
    [fDownloadedAllField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ total", "stats total"), [NSString stringForFileSize: statsAll.downloadedBytes]]];
    [fDownloadedAllField setToolTip: [byteFormatter stringFromByteCount: statsAll.downloadedBytes]];


    [fRatioField setStringValue: [NSString stringForRatio: statsSession.ratio]];

    NSString * totalRatioString = statsAll.ratio != TR_RATIO_NA
        ? [NSString stringWithFormat: NSLocalizedString(@"%@ total", "stats total"), [NSString stringForRatio: statsAll.ratio]]
        : NSLocalizedString(@"Total N/A", "stats total");
    [fRatioAllField setStringValue: totalRatioString];

    if ([NSApp isOnYosemiteOrBetter]) {
        static NSDateComponentsFormatter *timeFormatter;
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            timeFormatter = [NSDateComponentsFormatter new];
            timeFormatter.unitsStyle = NSDateComponentsFormatterUnitsStyleFull;
            timeFormatter.maximumUnitCount = 3;
            timeFormatter.allowedUnits = NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitWeekOfMonth | NSCalendarUnitDay | NSCalendarUnitHour | NSCalendarUnitMinute;
        });

        [fTimeField setStringValue: [timeFormatter stringFromTimeInterval:statsSession.secondsActive]];
        [fTimeAllField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ total", "stats total"), [timeFormatter stringFromTimeInterval:statsAll.secondsActive]]];
    }
    else {
        [fTimeField setStringValue: [NSString timeString: statsSession.secondsActive includesTimeRemainingPhrase:NO showSeconds: NO]];
        [fTimeAllField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ total", "stats total"), [NSString timeString: statsAll.secondsActive includesTimeRemainingPhrase:NO showSeconds: NO]]];
    }

    if (statsAll.sessionCount == 1)
        [fNumOpenedField setStringValue: NSLocalizedString(@"1 time", "stats window -> times opened")];
    else
        [fNumOpenedField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@ times", "stats window -> times opened"), [NSString formattedUInteger: statsAll.sessionCount]]];
}

- (void) performResetStats
{
    tr_sessionClearStats(fLib);
    [self updateStats];
}

- (void) resetSheetClosed: (NSAlert *) alert returnCode: (NSInteger) code contextInfo: (void *) info
{
    [[alert window] orderOut: nil];

    if ([[alert suppressionButton] state] == NSOnState)
        [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningResetStats"];

    if (code == NSAlertFirstButtonReturn)
        [self performResetStats];
}

@end
