/******************************************************************************
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

#import "GlobalOptionsPopoverViewController.h"

@implementation GlobalOptionsPopoverViewController

- (id) initWithHandle: (tr_session *) handle
{
    if ((self = [super initWithNibName: @"GlobalOptionsPopover" bundle: nil]))
    {
        fHandle = handle;

        fDefaults = [NSUserDefaults standardUserDefaults];
    }

    return self;
}

- (void) awakeFromNib
{
    [fUploadLimitField setIntValue: [fDefaults integerForKey: @"UploadLimit"]];
    [fDownloadLimitField setIntValue: [fDefaults integerForKey: @"DownloadLimit"]];

    [fRatioStopField setFloatValue: [fDefaults floatForKey: @"RatioLimit"]];
    [fIdleStopField setIntegerValue: [fDefaults integerForKey: @"IdleLimitMinutes"]];

    [[self view] setFrameSize: [[self view] fittingSize]];
}

- (IBAction) updatedDisplayString: (id) sender
{
    [[NSNotificationCenter defaultCenter] postNotificationName: @"RefreshTorrentTable" object: nil];
}

- (IBAction) setDownSpeedSetting: (id) sender
{
    tr_sessionLimitSpeed(fHandle, TR_DOWN, [fDefaults boolForKey: @"CheckDownload"]);

    [[NSNotificationCenter defaultCenter] postNotificationName: @"SpeedLimitUpdate" object: nil];
}

- (IBAction) setDownSpeedLimit: (id) sender
{
    const NSInteger limit = [sender integerValue];
    [fDefaults setInteger: limit forKey: @"DownloadLimit"];
    tr_sessionSetSpeedLimit_KBps(fHandle, TR_DOWN, limit);

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateSpeedLimitValuesOutsidePrefs" object: nil];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"SpeedLimitUpdate" object: nil];
}

- (IBAction) setUpSpeedSetting: (id) sender
{
    tr_sessionLimitSpeed(fHandle, TR_UP, [fDefaults boolForKey: @"CheckUpload"]);

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateSpeedLimitValuesOutsidePrefs" object: nil];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"SpeedLimitUpdate" object: nil];
}

- (IBAction) setUpSpeedLimit: (id) sender
{
    const NSInteger limit = [sender integerValue];
    [fDefaults setInteger: limit forKey: @"UploadLimit"];
    tr_sessionSetSpeedLimit_KBps(fHandle, TR_UP, limit);

    [[NSNotificationCenter defaultCenter] postNotificationName: @"SpeedLimitUpdate" object: nil];
}

- (IBAction) setRatioStopSetting: (id) sender
{
    tr_sessionSetRatioLimited(fHandle, [fDefaults boolForKey: @"RatioCheck"]);

    //reload main table for seeding progress
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];

    //reload global settings in inspector
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGlobalOptions" object: nil];
}

- (IBAction) setRatioStopLimit: (id) sender
{
    const CGFloat value = [sender floatValue];
    [fDefaults setFloat: value forKey: @"RatioLimit"];
    tr_sessionSetRatioLimit(fHandle, value);

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateRatioStopValueOutsidePrefs" object: nil];

    //reload main table for seeding progress
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];

    //reload global settings in inspector
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGlobalOptions" object: nil];
}

- (IBAction) setIdleStopSetting: (id) sender
{
    tr_sessionSetIdleLimited(fHandle, [fDefaults boolForKey: @"IdleLimitCheck"]);

    //reload main table for remaining seeding time
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];

    //reload global settings in inspector
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGlobalOptions" object: nil];
}

- (IBAction) setIdleStopLimit: (id) sender
{
    const NSInteger value = [sender integerValue];
    [fDefaults setInteger: value forKey: @"IdleLimitMinutes"];
    tr_sessionSetIdleLimit(fHandle, value);

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateIdleStopValueOutsidePrefs" object: nil];

    //reload main table for remaining seeding time
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];

    //reload global settings in inspector
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGlobalOptions" object: nil];
}

- (BOOL) control: (NSControl *) control textShouldBeginEditing: (NSText *) fieldEditor
{
    fInitialString = [control stringValue];

    return YES;
}

- (BOOL) control: (NSControl *) control didFailToFormatString: (NSString *) string errorDescription: (NSString *) error
{
    NSBeep();
    if (fInitialString)
    {
        [control setStringValue: fInitialString];
        fInitialString = nil;
    }
    return NO;
}

@end
