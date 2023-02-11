// This file Copyright © 2011-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GlobalOptionsPopoverViewController.h"

@interface GlobalOptionsPopoverViewController ()

@property(nonatomic, readonly) tr_session* fHandle;
@property(nonatomic, readonly) NSUserDefaults* fDefaults;

@property(nonatomic) IBOutlet NSTextField* fUploadLimitField;
@property(nonatomic) IBOutlet NSTextField* fDownloadLimitField;

@property(nonatomic) IBOutlet NSTextField* fRatioStopField;
@property(nonatomic) IBOutlet NSTextField* fIdleStopField;

@property(nonatomic, copy) NSString* fInitialString;

@end

@implementation GlobalOptionsPopoverViewController

- (instancetype)initWithHandle:(tr_session*)handle
{
    if ((self = [super initWithNibName:@"GlobalOptionsPopover" bundle:nil]))
    {
        _fHandle = handle;

        _fDefaults = NSUserDefaults.standardUserDefaults;
    }

    return self;
}

- (void)awakeFromNib
{
    self.fUploadLimitField.integerValue = [self.fDefaults integerForKey:@"UploadLimit"];
    self.fDownloadLimitField.integerValue = [self.fDefaults integerForKey:@"DownloadLimit"];

    self.fRatioStopField.floatValue = [self.fDefaults floatForKey:@"RatioLimit"];
    self.fIdleStopField.integerValue = [self.fDefaults integerForKey:@"IdleLimitMinutes"];

    [self.view setFrameSize:self.view.fittingSize];
}

- (IBAction)updatedDisplayString:(id)sender
{
    [NSNotificationCenter.defaultCenter postNotificationName:@"RefreshTorrentTable" object:nil];
}

- (IBAction)setDownSpeedSetting:(id)sender
{
    tr_sessionLimitSpeed(self.fHandle, TR_DOWN, [self.fDefaults boolForKey:@"CheckDownload"]);

    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (IBAction)setDownSpeedLimit:(id)sender
{
    NSInteger const limit = [sender integerValue];
    [self.fDefaults setInteger:limit forKey:@"DownloadLimit"];
    tr_sessionSetSpeedLimit_KBps(self.fHandle, TR_DOWN, limit);

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateSpeedLimitValuesOutsidePrefs" object:nil];
    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (IBAction)setUpSpeedSetting:(id)sender
{
    tr_sessionLimitSpeed(self.fHandle, TR_UP, [self.fDefaults boolForKey:@"CheckUpload"]);

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateSpeedLimitValuesOutsidePrefs" object:nil];
    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (IBAction)setUpSpeedLimit:(id)sender
{
    NSInteger const limit = [sender integerValue];
    [self.fDefaults setInteger:limit forKey:@"UploadLimit"];
    tr_sessionSetSpeedLimit_KBps(self.fHandle, TR_UP, limit);

    [NSNotificationCenter.defaultCenter postNotificationName:@"SpeedLimitUpdate" object:nil];
}

- (IBAction)setRatioStopSetting:(id)sender
{
    tr_sessionSetRatioLimited(self.fHandle, [self.fDefaults boolForKey:@"RatioCheck"]);

    //reload main table for seeding progress
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    //reload global settings in inspector
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (IBAction)setRatioStopLimit:(id)sender
{
    CGFloat const value = [sender floatValue];
    [self.fDefaults setFloat:value forKey:@"RatioLimit"];
    tr_sessionSetRatioLimit(self.fHandle, value);

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateRatioStopValueOutsidePrefs" object:nil];

    //reload main table for seeding progress
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    //reload global settings in inspector
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (IBAction)setIdleStopSetting:(id)sender
{
    tr_sessionSetIdleLimited(self.fHandle, [self.fDefaults boolForKey:@"IdleLimitCheck"]);

    //reload main table for remaining seeding time
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    //reload global settings in inspector
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (IBAction)setIdleStopLimit:(id)sender
{
    NSInteger const value = [sender integerValue];
    [self.fDefaults setInteger:value forKey:@"IdleLimitMinutes"];
    tr_sessionSetIdleLimit(self.fHandle, value);

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateIdleStopValueOutsidePrefs" object:nil];

    //reload main table for remaining seeding time
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateUI" object:nil];

    //reload global settings in inspector
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGlobalOptions" object:nil];
}

- (BOOL)control:(NSControl*)control textShouldBeginEditing:(NSText*)fieldEditor
{
    self.fInitialString = control.stringValue;

    return YES;
}

- (BOOL)control:(NSControl*)control didFailToFormatString:(NSString*)string errorDescription:(NSString*)error
{
    NSBeep();
    if (self.fInitialString)
    {
        control.stringValue = self.fInitialString;
        self.fInitialString = nil;
    }
    return NO;
}

@end
