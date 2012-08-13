/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2011-2012 Transmission authors and contributors
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

#import "StatusBarController.h"
#import "NSStringAdditions.h"

#import "transmission.h"

#define STATUS_RATIO_TOTAL      @"RatioTotal"
#define STATUS_RATIO_SESSION    @"RatioSession"
#define STATUS_TRANSFER_TOTAL   @"TransferTotal"
#define STATUS_TRANSFER_SESSION @"TransferSession"

typedef enum
{
    STATUS_RATIO_TOTAL_TAG = 0,
    STATUS_RATIO_SESSION_TAG = 1,
    STATUS_TRANSFER_TOTAL_TAG = 2,
    STATUS_TRANSFER_SESSION_TAG = 3
} statusTag;

@interface StatusBarController (Private)

- (void) resizeStatusButton;

@end

@implementation StatusBarController

- (id) initWithLib: (tr_session *) lib
{
    if ((self = [super initWithNibName: @"StatusBar" bundle: nil]))
    {
        fLib = lib;
        
        fPreviousDownloadRate = -1.0;
        fPreviousUploadRate = -1.0;
    }
    
    return self;
}

- (void) awakeFromNib
{
    //localize menu items
    [[[fStatusButton menu] itemWithTag: STATUS_RATIO_TOTAL_TAG] setTitle: NSLocalizedString(@"Total Ratio",
        "Status Bar -> status menu")];
    [[[fStatusButton menu] itemWithTag: STATUS_RATIO_SESSION_TAG] setTitle: NSLocalizedString(@"Session Ratio",
        "Status Bar -> status menu")];
    [[[fStatusButton menu] itemWithTag: STATUS_TRANSFER_TOTAL_TAG] setTitle: NSLocalizedString(@"Total Transfer",
        "Status Bar -> status menu")];
    [[[fStatusButton menu] itemWithTag: STATUS_TRANSFER_SESSION_TAG] setTitle: NSLocalizedString(@"Session Transfer",
        "Status Bar -> status menu")];
    
    [[fStatusButton cell] setBackgroundStyle: NSBackgroundStyleRaised];
    [[fTotalDLField cell] setBackgroundStyle: NSBackgroundStyleRaised];
    [[fTotalULField cell] setBackgroundStyle: NSBackgroundStyleRaised];
    [[fTotalDLImageView cell] setBackgroundStyle: NSBackgroundStyleRaised];
    [[fTotalULImageView cell] setBackgroundStyle: NSBackgroundStyleRaised];
    
    [self updateSpeedFieldsToolTips];
    
    //update when speed limits are changed
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateSpeedFieldsToolTips)
        name: @"SpeedLimitUpdate" object: nil];
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(resizeStatusButton)
        name: NSWindowDidResizeNotification object: [[self view] window]];
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [super dealloc];
}

- (void) updateWithDownload: (CGFloat) dlRate upload: (CGFloat) ulRate
{
    //set rates
    if (dlRate != fPreviousDownloadRate)
    {
        [fTotalDLField setStringValue: [NSString stringForSpeed: dlRate]];
        fPreviousDownloadRate = dlRate;
    }
    
    if (ulRate != fPreviousUploadRate)
    {
        [fTotalULField setStringValue: [NSString stringForSpeed: ulRate]];
        fPreviousUploadRate = ulRate;
    }
    
    //set status button text
    NSString * statusLabel = [[NSUserDefaults standardUserDefaults] stringForKey: @"StatusLabel"], * statusString;
    BOOL total;
    if ((total = [statusLabel isEqualToString: STATUS_RATIO_TOTAL]) || [statusLabel isEqualToString: STATUS_RATIO_SESSION])
    {
        tr_session_stats stats;
        if (total)
            tr_sessionGetCumulativeStats(fLib, &stats);
        else
            tr_sessionGetStats(fLib, &stats);
        
        statusString = [NSLocalizedString(@"Ratio", "status bar -> status label") stringByAppendingFormat: @": %@",
                        [NSString stringForRatio: stats.ratio]];
    }
    else //STATUS_TRANSFER_TOTAL or STATUS_TRANSFER_SESSION
    {
        total = [statusLabel isEqualToString: STATUS_TRANSFER_TOTAL];
        
        tr_session_stats stats;
        if (total)
            tr_sessionGetCumulativeStats(fLib, &stats);
        else
            tr_sessionGetStats(fLib, &stats);
        
        statusString = [NSString stringWithFormat: @"%@: %@  %@: %@",
                NSLocalizedString(@"DL", "status bar -> status label"), [NSString stringForFileSize: stats.downloadedBytes],
                NSLocalizedString(@"UL", "status bar -> status label"), [NSString stringForFileSize: stats.uploadedBytes]];
    }
    
    
    if (![[fStatusButton title] isEqualToString: statusString])
    {
        [fStatusButton setTitle: statusString];
        [self resizeStatusButton];
    }
}

- (void) setStatusLabel: (id) sender
{
    NSString * statusLabel;
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
    
    [[NSUserDefaults standardUserDefaults] setObject: statusLabel forKey: @"StatusLabel"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
}

- (void) updateSpeedFieldsToolTips
{
    NSString * uploadText, * downloadText;
    
    if ([[NSUserDefaults standardUserDefaults] boolForKey: @"SpeedLimit"])
    {
        NSString * speedString = [NSString stringWithFormat: @"%@ (%@)", NSLocalizedString(@"%d KB/s", "Status Bar -> speed tooltip"),
                                    NSLocalizedString(@"Speed Limit", "Status Bar -> speed tooltip")];
        
        uploadText = [NSString stringWithFormat: speedString,
                        [[NSUserDefaults standardUserDefaults] integerForKey: @"SpeedLimitUploadLimit"]];
        downloadText = [NSString stringWithFormat: speedString,
                        [[NSUserDefaults standardUserDefaults] integerForKey: @"SpeedLimitDownloadLimit"]];
    }
    else
    {
        if ([[NSUserDefaults standardUserDefaults] boolForKey: @"CheckUpload"])
            uploadText = [NSString stringWithFormat: NSLocalizedString(@"%d KB/s", "Status Bar -> speed tooltip"),
                            [[NSUserDefaults standardUserDefaults] integerForKey: @"UploadLimit"]];
        else
            uploadText = NSLocalizedString(@"unlimited", "Status Bar -> speed tooltip");
        
        if ([[NSUserDefaults standardUserDefaults] boolForKey: @"CheckDownload"])
            downloadText = [NSString stringWithFormat: NSLocalizedString(@"%d KB/s", "Status Bar -> speed tooltip"),
                            [[NSUserDefaults standardUserDefaults] integerForKey: @"DownloadLimit"]];
        else
            downloadText = NSLocalizedString(@"unlimited", "Status Bar -> speed tooltip");
    }
    
    uploadText = [NSLocalizedString(@"Global upload limit", "Status Bar -> speed tooltip")
                    stringByAppendingFormat: @": %@", uploadText];
    downloadText = [NSLocalizedString(@"Global download limit", "Status Bar -> speed tooltip")
                    stringByAppendingFormat: @": %@", downloadText];
    
    [fTotalULField setToolTip: uploadText];
    [fTotalDLField setToolTip: downloadText];
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    const SEL action = [menuItem action];
    
    //enable sort options
    if (action == @selector(setStatusLabel:))
    {
        NSString * statusLabel;
        switch ([menuItem tag])
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
                NSAssert1(NO, @"Unknown status label tag received: %ld", [menuItem tag]);
        }
        
        [menuItem setState: [statusLabel isEqualToString: [[NSUserDefaults standardUserDefaults] stringForKey: @"StatusLabel"]]
                            ? NSOnState : NSOffState];
        return YES;
    }
    
    return YES;
}

@end

@implementation StatusBarController (Private)

- (void) resizeStatusButton
{
    [fStatusButton sizeToFit];
    
    //width ends up being too long
    NSRect statusFrame = [fStatusButton frame];
    statusFrame.size.width -= 25.0;
    
    const CGFloat difference = NSMaxX(statusFrame) + 5.0 - NSMinX([fTotalDLImageView frame]);
    if (difference > 0.0)
        statusFrame.size.width -= difference;
    
    [fStatusButton setFrame: statusFrame];
}

@end
