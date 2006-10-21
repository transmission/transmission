/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#import "PrefsController.h"
#import "StringAdditions.h"
#import "UKKQueue.h"

#define MIN_PORT    1
#define MAX_PORT    65535

#define DOWNLOAD_FOLDER     0
#define DOWNLOAD_TORRENT    2
#define DOWNLOAD_ASK        3

#define UPDATE_DAILY    0
#define UPDATE_WEEKLY   1
#define UPDATE_NEVER    2

#define TOOLBAR_GENERAL     @"General"
#define TOOLBAR_TRANSFERS   @"Transfers"
#define TOOLBAR_BANDWIDTH   @"Bandwidth"
#define TOOLBAR_NETWORK     @"Network"

@interface PrefsController (Private)

- (void) showGeneralPref: (id) sender;
- (void) showTransfersPref: (id) sender;
- (void) showBandwidthPref: (id) sender;
- (void) showNetworkPref: (id) sender;

- (void) setPrefView: (NSView *) view;

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) updatePopUp;

- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) updateImportPopUp;

@end

@implementation PrefsController

- (id) initWithWindowNibName: (NSString *) name handle: (tr_handle_t *) handle
{
    if ((self = [self initWithWindowNibName: name]))
    {
        fDefaults = [NSUserDefaults standardUserDefaults];
        fHandle = handle;
        
        //checks for old version upload speed of -1
        if ([fDefaults integerForKey: @"UploadLimit"] < 0)
        {
            [fDefaults setInteger: 20 forKey: @"UploadLimit"];
            [fDefaults setBool: NO forKey: @"CheckUpload"];
        }
        
        [[self window] update]; //make sure nib is loaded right away
    }
    return self;
}

- (void) dealloc
{
    [fNatStatusTimer invalidate];

    [fDownloadFolder release];
    [fImportFolder release];
    [super dealloc];
}

- (void) awakeFromNib
{
    fToolbar = [[NSToolbar alloc] initWithIdentifier: @"Preferences Toolbar"];
    [fToolbar setDelegate: self];
    [fToolbar setAllowsUserCustomization: NO];
    [[self window] setToolbar: fToolbar];
    [fToolbar setDisplayMode: NSToolbarDisplayModeIconAndLabel];
    [fToolbar setSizeMode: NSToolbarSizeModeRegular];

    [fToolbar setSelectedItemIdentifier: TOOLBAR_GENERAL];
    [self showGeneralPref: nil];
    
    //set download folder
    NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
    fDownloadFolder = [[[fDefaults stringForKey: @"DownloadFolder"] stringByExpandingTildeInPath] retain];
    if ([downloadChoice isEqualToString: @"Constant"])
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
    else if ([downloadChoice isEqualToString: @"Torrent"])
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_TORRENT];
    else
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_ASK];
    [self updatePopUp];
    
    //set auto import
    fImportFolder = [[[fDefaults stringForKey: @"AutoImportDirectory"] stringByExpandingTildeInPath] retain];
    [self updateImportPopUp];
 
    BOOL autoImport = [fDefaults boolForKey: @"AutoImport"];
    [fAutoImportCheck setState: autoImport];
    [fImportFolderPopUp setEnabled: autoImport];
    
    if (autoImport)
        [[UKKQueue sharedFileWatcher] addPath: fImportFolder];
    
    //set auto size
    [fAutoSizeCheck setState: [fDefaults boolForKey: @"AutoSize"]];
    
    //set bind port
    int bindPort = [fDefaults integerForKey: @"BindPort"];
    [fPortField setIntValue: bindPort];
    tr_setBindPort(fHandle, bindPort);
    
    [self updatePortStatus];
    
    //set NAT
    BOOL natShouldEnable = [fDefaults boolForKey: @"NatTraversal"];
    if (natShouldEnable)
        tr_natTraversalEnable(fHandle);
    [fNatCheck setState: natShouldEnable];
    
    fNatStatus = -1;
    [self updateNatStatus];
    fNatStatusTimer = [NSTimer scheduledTimerWithTimeInterval: 5.0 target: self
                        selector: @selector(updateNatStatus) userInfo: nil repeats: YES];
    
    //set upload limit
    BOOL checkUpload = [fDefaults boolForKey: @"CheckUpload"];
    int uploadLimit = [fDefaults integerForKey: @"UploadLimit"];
    
    [fUploadCheck setState: checkUpload];
    [fUploadField setIntValue: uploadLimit];
    [fUploadField setEnabled: checkUpload];

	//set download limit
    BOOL checkDownload = [fDefaults boolForKey: @"CheckDownload"];
    int downloadLimit = [fDefaults integerForKey: @"DownloadLimit"];
    
    [fDownloadCheck setState: checkDownload];
    [fDownloadField setIntValue: downloadLimit];
    [fDownloadField setEnabled: checkDownload];
    
    //set speed limit
    int speedLimitUploadLimit = [fDefaults integerForKey: @"SpeedLimitUploadLimit"];
    [fSpeedLimitUploadField setIntValue: speedLimitUploadLimit];
    
    int speedLimitDownloadLimit = [fDefaults integerForKey: @"SpeedLimitDownloadLimit"];
    [fSpeedLimitDownloadField setIntValue: speedLimitDownloadLimit];
    
    //actually set bandwidth limits
    if ([fDefaults boolForKey: @"SpeedLimit"])
    {
        tr_setUploadLimit(fHandle, speedLimitUploadLimit);
        tr_setDownloadLimit(fHandle, speedLimitDownloadLimit);
    }
    else
    {
        tr_setUploadLimit(fHandle, checkUpload ? uploadLimit : -1);
        tr_setDownloadLimit(fHandle, checkDownload ? downloadLimit : -1);
    }
    
    //set auto speed limit
    BOOL speedLimitAuto = [fDefaults boolForKey: @"SpeedLimitAuto"];
    [fSpeedLimitAutoCheck setState: speedLimitAuto];
    
    int speedLimitAutoOnHour = [fDefaults integerForKey: @"SpeedLimitAutoOnHour"];
    [fSpeedLimitAutoOnField setStringValue: [NSString stringWithFormat: @"%02d", speedLimitAutoOnHour]];
    [fSpeedLimitAutoOnField setEnabled: speedLimitAuto];
    
    int speedLimitAutoOffHour = [fDefaults integerForKey: @"SpeedLimitAutoOffHour"];
    [fSpeedLimitAutoOffField setStringValue: [NSString stringWithFormat: @"%02d", speedLimitAutoOffHour]];
    [fSpeedLimitAutoOffField setEnabled: speedLimitAuto];
    
    //set ratio limit
    BOOL ratioCheck = [fDefaults boolForKey: @"RatioCheck"];
    [fRatioCheck setState: ratioCheck];
    [fRatioField setEnabled: ratioCheck];
    [fRatioField setFloatValue: [fDefaults floatForKey: @"RatioLimit"]];
    
    //set remove and quit prompts
    BOOL isQuitCheck = [fDefaults boolForKey: @"CheckQuit"],
        isRemoveCheck = [fDefaults boolForKey: @"CheckRemove"];
    
    [fQuitCheck setState: isQuitCheck];
    [fRemoveCheck setState: isRemoveCheck];
    
    [fQuitDownloadingCheck setState: [fDefaults boolForKey: @"CheckQuitDownloading"]];
    [fQuitDownloadingCheck setEnabled: isQuitCheck];
    [fRemoveDownloadingCheck setState: [fDefaults boolForKey: @"CheckRemoveDownloading"]];
    [fRemoveDownloadingCheck setEnabled: isRemoveCheck];

    //set dock badging
    [fBadgeDownloadRateCheck setState: [fDefaults boolForKey: @"BadgeDownloadRate"]];
    [fBadgeUploadRateCheck setState: [fDefaults boolForKey: @"BadgeUploadRate"]];
    
    //set play sound
    NSMutableArray * sounds = [NSMutableArray array];
    NSEnumerator * soundEnumerator,
                * soundDirectoriesEnumerator = [[NSArray arrayWithObjects: @"System/Library/Sounds",
                        [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Sounds"], nil] objectEnumerator];
    NSString * soundPath, * sound;
    
    //get list of all sounds and sort alphabetically
    while ((soundPath = [soundDirectoriesEnumerator nextObject]))
        if (soundEnumerator = [[NSFileManager defaultManager] enumeratorAtPath: soundPath])
            while ((sound = [soundEnumerator nextObject]))
            {
                sound = [sound stringByDeletingPathExtension];
                if ([NSSound soundNamed: sound])
                    [sounds addObject: sound];
            }
    
    [sounds sortUsingSelector: @selector(caseInsensitiveCompare:)];
    
    //set download sound
    [fDownloadSoundPopUp removeAllItems];
    [fDownloadSoundPopUp addItemsWithTitles: sounds];
    
    BOOL playDownloadSound = [fDefaults boolForKey: @"PlayDownloadSound"];
    [fPlayDownloadSoundCheck setState: playDownloadSound];
    [fDownloadSoundPopUp setEnabled: playDownloadSound];
    
    int downloadSoundIndex = [fDownloadSoundPopUp indexOfItemWithTitle: [fDefaults stringForKey: @"DownloadSound"]];
    if (downloadSoundIndex >= 0)
        [fDownloadSoundPopUp selectItemAtIndex: downloadSoundIndex];
    else
        [fDefaults setObject: [fDownloadSoundPopUp titleOfSelectedItem] forKey: @"DownloadSound"];
    
    //set seeding sound
    [fSeedingSoundPopUp removeAllItems];
    [fSeedingSoundPopUp addItemsWithTitles: sounds];
    
    BOOL playSeedingSound = [fDefaults boolForKey: @"PlaySeedingSound"];
    [fPlaySeedingSoundCheck setState: playSeedingSound];
    [fSeedingSoundPopUp setEnabled: playSeedingSound];
    
    int seedingSoundIndex = [fDownloadSoundPopUp indexOfItemWithTitle: [fDefaults stringForKey: @"SeedingSound"]];
    if (seedingSoundIndex >= 0)
        [fSeedingSoundPopUp selectItemAtIndex: seedingSoundIndex];
    else
        [fDefaults setObject: [fSeedingSoundPopUp titleOfSelectedItem] forKey: @"SeedingSound"];
    
    //set start settings
    BOOL useQueue = [fDefaults boolForKey: @"Queue"];
    [fQueueCheck setState: useQueue];
    [fQueueNumberField setEnabled: useQueue];
    [fQueueNumberField setIntValue: [fDefaults integerForKey: @"QueueDownloadNumber"]];
    
    [fStartAtOpenCheck setState: [fDefaults boolForKey: @"AutoStartDownload"]];
    
    //set private torrents
    BOOL copyTorrents = [fDefaults boolForKey: @"SavePrivateTorrent"];
    [fCopyTorrentCheck setState: copyTorrents];
    
    [fDeleteOriginalTorrentCheck setEnabled: copyTorrents];
    [fDeleteOriginalTorrentCheck setState: [fDefaults boolForKey: @"DeleteOriginalTorrent"]];

    //set update check
    NSString * updateCheck = [fDefaults stringForKey: @"UpdateCheck"];
    if ([updateCheck isEqualToString: @"Weekly"])
        [fUpdatePopUp selectItemAtIndex: UPDATE_WEEKLY];
    else if ([updateCheck isEqualToString: @"Never"])
        [fUpdatePopUp selectItemAtIndex: UPDATE_NEVER];
    else
        [fUpdatePopUp selectItemAtIndex: UPDATE_DAILY];
}

- (NSToolbarItem *) toolbar: (NSToolbar *) t itemForItemIdentifier:
    (NSString *) ident willBeInsertedIntoToolbar: (BOOL) flag
{
    NSToolbarItem * item;
    item = [[NSToolbarItem alloc] initWithItemIdentifier: ident];

    if ([ident isEqualToString: TOOLBAR_GENERAL])
    {
        [item setLabel: TOOLBAR_GENERAL];
        [item setImage: [NSImage imageNamed: @"Preferences.png"]];
        [item setTarget: self];
        [item setAction: @selector(showGeneralPref:)];
    }
    else if ([ident isEqualToString: TOOLBAR_TRANSFERS])
    {
        [item setLabel: TOOLBAR_TRANSFERS];
        [item setImage: [NSImage imageNamed: @"Transfers.png"]];
        [item setTarget: self];
        [item setAction: @selector(showTransfersPref:)];
    }
    else if ([ident isEqualToString: TOOLBAR_BANDWIDTH])
    {
        [item setLabel: TOOLBAR_BANDWIDTH];
        [item setImage: [NSImage imageNamed: @"Bandwidth.png"]];
        [item setTarget: self];
        [item setAction: @selector(showBandwidthPref:)];
    }
    else if ([ident isEqualToString: TOOLBAR_NETWORK])
    {
        [item setLabel: TOOLBAR_NETWORK];
        [item setImage: [NSImage imageNamed: @"Network.png"]];
        [item setTarget: self];
        [item setAction: @selector(showNetworkPref:)];
    }
    else
    {
        [item release];
        return nil;
    }

    return item;
}

- (NSArray *) toolbarSelectableItemIdentifiers: (NSToolbar *) toolbar
{
    return [self toolbarDefaultItemIdentifiers: toolbar];
}

- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar *) toolbar
{
    return [self toolbarAllowedItemIdentifiers: toolbar];
}

- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar *) toolbar
{
    return [NSArray arrayWithObjects:
            TOOLBAR_GENERAL, TOOLBAR_TRANSFERS,
            TOOLBAR_BANDWIDTH, TOOLBAR_NETWORK, nil];
}

- (void) setPort: (id) sender
{
    int bindPort = [sender intValue];
    if (![[NSString stringWithInt: bindPort] isEqualToString: [sender stringValue]]
            || bindPort < MIN_PORT || bindPort > MAX_PORT)
    {
        NSBeep();
        bindPort = [fDefaults integerForKey: @"BindPort"];
        [sender setIntValue: bindPort];
    }
    else
    {
        tr_setBindPort(fHandle, bindPort);
        [fDefaults setInteger: bindPort forKey: @"BindPort"];
        
        [self updateNatStatus];
        [self updatePortStatus];
    }
}

- (void) updatePortStatus
{
    long sytemVersion;
    [fPortStatusField setStringValue: @""];
    [fPortStatusImage setImage: nil];
    
    Gestalt('sysv', & sytemVersion);
    if (sytemVersion >= 0x1040)
    {
        //NSXML features are unfortunately only available since Mac OS X v10.4
        PortChecker * checker = [[PortChecker alloc] initWithDelegate: self];

        [fPortStatusField setStringValue: [@"Checking port status" stringByAppendingEllipsis]];
        [fPortStatusProgress startAnimation: self];
        
        [checker probePort: [fDefaults integerForKey: @"BindPort"]];
    }
}

- (void) portCheckerDidFinishProbing: (PortChecker *) portChecker
{
    [fPortStatusProgress stopAnimation: self];
    switch ([portChecker status])
    {
        case PORT_STATUS_OPEN:
            [fPortStatusField setStringValue: @"Port is open"];
            [fPortStatusImage setImage: [NSImage imageNamed: @"GreenDot.tiff"]];
            break;
        case PORT_STATUS_STEALTH:
            [fPortStatusField setStringValue: @"Port is stealth"];
            [fPortStatusImage setImage: [NSImage imageNamed: @"RedDot.tiff"]];
            break;
        case PORT_STATUS_CLOSED:
            [fPortStatusField setStringValue: @"Port is closed"];
            [fPortStatusImage setImage: [NSImage imageNamed: @"RedDot.tiff"]];
            break;
        case PORT_STATUS_ERROR:
            [fPortStatusField setStringValue: @"Unable to check port status"];
            [fPortStatusImage setImage: [NSImage imageNamed: @"YellowDot.tiff"]];
            break;
    }
    [portChecker release];
}

- (void) setNat: (id) sender
{
    BOOL enable = [sender state] == NSOnState;
    enable ? tr_natTraversalEnable(fHandle) : tr_natTraversalDisable(fHandle);
    [fDefaults setBool: enable forKey: @"NatTraversal"];
    
    [self updateNatStatus];
}

- (void) updateNatStatus
{
    int status = tr_natTraversalStatus(fHandle);
    if (fNatStatus == status)
        return;
    fNatStatus = status;
    
    if (status == 2)
    {
        [fNatStatusField setStringValue: @"Port successfully mapped"];
        [fNatStatusImage setImage: [NSImage imageNamed: @"GreenDot.tiff"]];
    }
    else if (status == 3 || status == 4)
    {
        [fNatStatusField setStringValue: @"Error mapping port"];
        [fNatStatusImage setImage: [NSImage imageNamed: @"RedDot.tiff"]];
    }
    else
    {
        [fNatStatusField setStringValue: @""];
        [fNatStatusImage setImage: nil];
    }
    
    [self updatePortStatus];
}

- (void) setLimit: (id) sender
{
    NSString * key;
    NSButton * check;
    NSString * type;
    if (sender == fUploadField)
    {
        key = @"UploadLimit";
        check = fUploadCheck;
        type = @"Upload";
    }
    else
    {
        key = @"DownloadLimit";
        check = fDownloadCheck;
        type = @"Download";
    }

    int limit = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", limit]] || limit < 0)
    {
        NSBeep();
        limit = [fDefaults integerForKey: key];
        [sender setIntValue: limit];
    }
    else
    {
        if (![fDefaults boolForKey: @"SpeedLimit"])
        {
            int realLimit = [check state] ? limit : -1;
            if (sender == fUploadField)
                tr_setUploadLimit(fHandle, realLimit);
            else
                tr_setDownloadLimit(fHandle, realLimit);
        }
        
        [fDefaults setInteger: limit forKey: key];
    }
    
    NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                                    [NSNumber numberWithBool: [check state]], @"Enable",
                                    [NSNumber numberWithInt: limit], @"Limit",
                                    type, @"Type", nil];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"LimitGlobalChange" object: dict];
}

- (void) setLimitCheck: (id) sender
{
    NSString * key;
    NSTextField * field;
    if (sender == fUploadCheck)
    {
        key = @"CheckUpload";
        field = fUploadField;
    }
    else
    {
        key = @"CheckDownload";
        field = fDownloadField;
    }
    
    BOOL check = [sender state] == NSOnState;
    [self setLimit: field];
    [field setEnabled: check];
    
    [fDefaults setBool: check forKey: key];
}

- (void) setLimitEnabled: (BOOL) enable type: (NSString *) type
{
    NSButton * check = [type isEqualToString: @"Upload"] ? fUploadCheck : fDownloadCheck;
    [check setState: enable ? NSOnState : NSOffState];
    [self setLimitCheck: check];
}

- (void) setQuickLimit: (int) limit type: (NSString *) type
{
    NSButton * check;
    if ([type isEqualToString: @"Upload"])
    {
        [fUploadField setIntValue: limit];
        check = fUploadCheck;
    }
    else
    {
        [fDownloadField setIntValue: limit];
        check = fDownloadCheck;
    }
    [check setState: NSOnState];
    [self setLimitCheck: check];
}

- (void) enableSpeedLimit: (BOOL) enable
{
    if ([fDefaults boolForKey: @"SpeedLimit"] != enable)
    {
        [fDefaults setBool: enable forKey: @"SpeedLimit"];
        
        if (enable)
        {
            tr_setUploadLimit(fHandle, [fDefaults integerForKey: @"SpeedLimitUploadLimit"]);
            tr_setDownloadLimit(fHandle, [fDefaults integerForKey: @"SpeedLimitDownloadLimit"]);
        }
        else
        {
            tr_setUploadLimit(fHandle, [fUploadCheck state] ? [fDefaults integerForKey: @"UploadLimit"] : -1);
            tr_setDownloadLimit(fHandle, [fDownloadCheck state] ? [fDefaults integerForKey: @"DownloadLimit"] : -1);
        }
    }
}

- (void) setSpeedLimit: (id) sender
{
    NSString * key = sender == fSpeedLimitUploadField ? @"SpeedLimitUploadLimit" : @"SpeedLimitDownloadLimit";

    int limit = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", limit]] || limit < 0)
    {
        NSBeep();
        limit = [fDefaults integerForKey: key];
        [sender setIntValue: limit];
    }
    else
    {
        if ([fDefaults boolForKey: @"SpeedLimit"])
        {
            if (sender == fSpeedLimitUploadField)
                tr_setUploadLimit(fHandle, limit);
            else
                tr_setDownloadLimit(fHandle, limit);
        }
        
        [fDefaults setInteger: limit forKey: key];
    }
}

- (void) setAutoSpeedLimitCheck: (id) sender
{
    BOOL check = [sender state] == NSOnState;
    
    [fDefaults setBool: check forKey: @"SpeedLimitAuto"];

    [self setAutoSpeedLimitHour: fSpeedLimitAutoOnField];
    [fSpeedLimitAutoOnField setEnabled: check];
    
    [self setAutoSpeedLimitHour: fSpeedLimitAutoOffField];
    [fSpeedLimitAutoOffField setEnabled: check];
}

- (void) setAutoSpeedLimitHour: (id) sender
{
    NSString * key = (sender == fSpeedLimitAutoOnField) ? @"SpeedLimitAutoOnHour" : @"SpeedLimitAutoOffHour";

    int hour = [sender intValue];
    
    //allow numbers under ten in the format 0x
    if (!([[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%d", hour]]
        || [[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%02d", hour]]) || hour < 0 || hour > 23
        || [fSpeedLimitAutoOnField intValue] == [fSpeedLimitAutoOffField intValue])
    {
        NSBeep();
        hour = [fDefaults integerForKey: key];
        [sender setStringValue: [NSString stringWithFormat: @"%02d", hour]];
    }
    else
        [fDefaults setInteger: hour forKey: key];
    
    [sender setStringValue: [NSString stringWithFormat: @"%02d", hour]]; //ensure number has 2 digits
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSpeedLimitChange" object: self];
}

- (void) setRatio: (id) sender
{
    float ratioLimit = [sender floatValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithFormat: @"%.2f", ratioLimit]]
            || ratioLimit < 0)
    {
        NSBeep();
        ratioLimit = [fDefaults floatForKey: @"RatioLimit"];
        [sender setFloatValue: ratioLimit];
    }
    else
        [fDefaults setFloat: ratioLimit forKey: @"RatioLimit"];
    
    NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys:
                                [NSNumber numberWithBool: [fRatioCheck state]], @"Enable",
                                [NSNumber numberWithFloat: ratioLimit], @"Ratio", nil];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"RatioGlobalChange" object: dict];
}

- (void) setRatioCheck: (id) sender
{
    BOOL check = [sender state] == NSOnState;
    [self setRatio: fRatioField];
    [fRatioField setEnabled: check];
    
    [fDefaults setBool: check forKey: @"RatioCheck"];
}

- (void) setRatioEnabled: (BOOL) enable
{
    int state = enable ? NSOnState : NSOffState;
    
    [fRatioCheck setState: state];
    [self setRatioCheck: fRatioCheck];
}

- (void) setQuickRatio: (float) ratioLimit
{
    [fRatioField setFloatValue: ratioLimit];
    
    [fRatioCheck setState: NSOnState];
    [self setRatioCheck: fRatioCheck];
}

- (void) setShowMessage: (id) sender
{
    BOOL state = [sender state];

    if (sender == fQuitCheck)
    {
        [fDefaults setBool: state forKey: @"CheckQuit"];
        [fQuitDownloadingCheck setEnabled: state];
    }
    else if (sender == fRemoveCheck)
    {
        [fDefaults setBool: state forKey: @"CheckRemove"];
        [fRemoveDownloadingCheck setEnabled: state];
    }
    if (sender == fQuitDownloadingCheck)
        [fDefaults setBool: state forKey: @"CheckQuitDownloading"];
    else if (sender == fRemoveDownloadingCheck)
        [fDefaults setBool: state forKey: @"CheckRemoveDownloading"];
    else;
}

- (void) setBadge: (id) sender
{   
    if (sender == fBadgeDownloadRateCheck)
        [fDefaults setBool: [sender state] forKey: @"BadgeDownloadRate"];
    else if (sender == fBadgeUploadRateCheck)
        [fDefaults setBool: [sender state] forKey: @"BadgeUploadRate"];
    else;
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"DockBadgeChange" object: self];
}

- (void) setPlaySound: (id) sender
{
    BOOL state = [sender state];

    if (sender == fPlayDownloadSoundCheck)
    {
        [fDownloadSoundPopUp setEnabled: state];
        [fDefaults setBool: state forKey: @"PlayDownloadSound"];
    }
    else if (sender == fPlaySeedingSoundCheck)
    {
        [fSeedingSoundPopUp setEnabled: state];
        [fDefaults setBool: state forKey: @"PlaySeedingSound"];
    }
    else;
}

- (void) setSound: (id) sender
{
    //play sound when selecting
    NSString * soundName = [sender titleOfSelectedItem];
    NSSound * sound;
    if ((sound = [NSSound soundNamed: soundName]))
        [sound play];

    if (sender == fDownloadSoundPopUp)
        [fDefaults setObject: soundName forKey: @"DownloadSound"];
    else if (sender == fSeedingSoundPopUp)
        [fDefaults setObject: soundName forKey: @"SeedingSound"];
    else;
}

- (void) setUpdate: (id) sender
{
    int index = [fUpdatePopUp indexOfSelectedItem];
    NSTimeInterval seconds;
    if (index == UPDATE_DAILY)
    {
        [fDefaults setObject: @"Daily" forKey: @"UpdateCheck"];
        seconds = 86400;
    }
    else if (index == UPDATE_WEEKLY)
    {
        [fDefaults setObject: @"Weekly" forKey: @"UpdateCheck"];
        seconds = 604800;
    }
    else
    {
        [fDefaults setObject: @"Never" forKey: @"UpdateCheck"];
        seconds = 0;
    }

    [fDefaults setInteger: seconds forKey: @"SUScheduledCheckInterval"];
    [fUpdater scheduleCheckWithInterval: seconds];
}

- (void) setStartAtOpen: (id) sender
{
    [fDefaults setBool: [sender state] == NSOnState forKey: @"AutoStartDownload"];
}

- (void) setUseQueue: (id) sender
{
    BOOL useQueue = [sender state] == NSOnState;
    
    [fDefaults setBool: useQueue forKey: @"Queue"];
    [self setQueueNumber: fQueueNumberField];
    [fQueueNumberField setEnabled: useQueue];
}

- (void) setQueueNumber: (id) sender
{
    int queueNumber = [sender intValue];
    if (![[sender stringValue] isEqualToString: [NSString stringWithInt: queueNumber]] || queueNumber < 1)
    {
        NSBeep();
        queueNumber = [fDefaults integerForKey: @"QueueDownloadNumber"];
        [sender setIntValue: queueNumber];
    }
    else
        [fDefaults setInteger: queueNumber forKey: @"QueueDownloadNumber"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"GlobalStartSettingChange" object: self];
}

- (void) setMoveTorrent: (id) sender
{
    int state = [sender state];
    if (sender == fCopyTorrentCheck)
    {
        [fDefaults setBool: state forKey: @"SavePrivateTorrent"];
        
        [fDeleteOriginalTorrentCheck setEnabled: state];
        if (state == NSOffState)
        {
            [fDeleteOriginalTorrentCheck setState: NSOffState];
            [fDefaults setBool: NO forKey: @"DeleteOriginalTorrent"];
        }
    }
    else
        [fDefaults setBool: state forKey: @"DeleteOriginalTorrent"];
}

- (void) setDownloadLocation: (id) sender
{
    //Download folder
    switch ([fFolderPopUp indexOfSelectedItem])
    {
        case DOWNLOAD_FOLDER:
            [fDefaults setObject: @"Constant" forKey: @"DownloadChoice"];
            break;
        case DOWNLOAD_TORRENT:
            [fDefaults setObject: @"Torrent" forKey: @"DownloadChoice"];
            break;
        case DOWNLOAD_ASK:
            [fDefaults setObject: @"Ask" forKey: @"DownloadChoice"];
            break;
    }
}

- (void) checkUpdate
{
    [fUpdater checkForUpdates: nil];
}

- (void) folderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: @"Select"];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [self window] modalDelegate: self didEndSelector:
        @selector(folderSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) setAutoImport: (id) sender
{
    int state = [fAutoImportCheck state];
    [fDefaults setBool: state forKey: @"AutoImport"];
    [fImportFolderPopUp setEnabled: state];
    
    if (state == NSOnState)
        [[UKKQueue sharedFileWatcher] addPath: fImportFolder];
    else
        [[UKKQueue sharedFileWatcher] removePathFromQueue: fImportFolder];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
}

- (void) importFolderSheetShow: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: @"Select"];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];

    [panel beginSheetForDirectory: nil file: nil types: nil
        modalForWindow: [self window] modalDelegate: self didEndSelector:
        @selector(importFolderSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) setAutoSize: (id) sender
{
    [fDefaults setBool: [sender state] forKey: @"AutoSize"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoSizeSettingChange" object: self];
}

- (void) helpForNetwork: (id) sender
{
    [[NSHelpManager sharedHelpManager] openHelpAnchor: @"PortForwarding"
        inBook: [[NSBundle mainBundle] objectForInfoDictionaryKey: @"CFBundleHelpBookName"]];
}

@end

@implementation PrefsController (Private)

- (void) showGeneralPref: (id) sender
{
    [self setPrefView: fGeneralView];
}

- (void) showTransfersPref: (id) sender
{
    [self setPrefView: fTransfersView];
}

- (void) showBandwidthPref: (id) sender
{
    [self setPrefView: fBandwidthView];
}

- (void) showNetworkPref: (id) sender
{
    [self setPrefView: fNetworkView];
    
    //make sure progress indicator hides itself
    if ([fPortStatusImage image])
        [fPortStatusProgress setDisplayedWhenStopped: NO];
}

- (void) setPrefView: (NSView *) view
{
    NSWindow * window = [self window];
    
    if ([window contentView] == view)
        return;
    
    NSRect windowRect = [window frame];
    int difference = [view frame].size.height - [[window contentView] frame].size.height;
    windowRect.origin.y -= difference;
    windowRect.size.height += difference;

    [window setTitle: [fToolbar selectedItemIdentifier]];
    
    [view setHidden: YES];
    [window setContentView: view];
    [window setFrame: windowRect display: YES animate: YES];
    [view setHidden: NO];
}

- (void) folderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
    {
        [fDownloadFolder release];
        fDownloadFolder = [[[openPanel filenames] objectAtIndex: 0] retain];
        
        [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
        [fDefaults setObject: fDownloadFolder forKey: @"DownloadFolder"];
        [fDefaults setObject: @"Constant" forKey: @"DownloadChoice"];
        
        [self updatePopUp];
    }
    else
    {
        //reset if cancelled
        NSString * downloadChoice = [fDefaults stringForKey: @"DownloadChoice"];
        if ([downloadChoice isEqualToString: @"Constant"])
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_FOLDER];
        else if ([downloadChoice isEqualToString: @"Torrent"])
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_TORRENT];
        else
            [fFolderPopUp selectItemAtIndex: DOWNLOAD_ASK];
    }
}

- (void) updatePopUp
{
    //get and resize the icon
    NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFile: fDownloadFolder];
    [icon setScalesWhenResized: YES];
    [icon setSize: NSMakeSize(16.0, 16.0)];

    //update menu item
    NSMenuItem * menuItem = (NSMenuItem *) [fFolderPopUp itemAtIndex: 0];
    [menuItem setTitle: [fDownloadFolder lastPathComponent]];
    [menuItem setImage: icon];
}

- (void) importFolderSheetClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
    {
        UKKQueue * sharedQueue = [UKKQueue sharedFileWatcher];
        [sharedQueue removePathFromQueue: fImportFolder];
        
        [fImportFolder release];
        fImportFolder = [[[openPanel filenames] objectAtIndex: 0] retain];
        
        [fDefaults setObject: fImportFolder forKey: @"AutoImportDirectory"];
        
        [self updateImportPopUp];
        
        [sharedQueue addPath: fImportFolder];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"AutoImportSettingChange" object: self];
    }
    [fImportFolderPopUp selectItemAtIndex: 0];
}

- (void) updateImportPopUp
{
    //get and resize the icon
    NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFile: fImportFolder];
    [icon setScalesWhenResized: YES];
    [icon setSize: NSMakeSize(16.0, 16.0)];

    //update menu item
    NSMenuItem * menuItem = (NSMenuItem *) [fImportFolderPopUp itemAtIndex: 0];
    [menuItem setTitle: [fImportFolder lastPathComponent]];
    [menuItem setImage: icon];
}

@end
