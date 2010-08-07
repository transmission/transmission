/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2010 Transmission authors and contributors
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

#import "InfoOptionsViewController.h"
#import "Torrent.h"

#define OPTION_POPUP_GLOBAL 0
#define OPTION_POPUP_NO_LIMIT 1
#define OPTION_POPUP_LIMIT 2

#define OPTION_POPUP_PRIORITY_HIGH 0
#define OPTION_POPUP_PRIORITY_NORMAL 1
#define OPTION_POPUP_PRIORITY_LOW 2

#define INVALID -99

@interface InfoOptionsViewController (Private)

- (void) setupInfo;

@end

@implementation InfoOptionsViewController

- (id) init
{
    if ((self = [super initWithNibName: @"InfoOptionsView" bundle: nil]))
    {
        [self setTitle: NSLocalizedString(@"Options", "Inspector view -> title")];
    }
    
    return self;
}

- (void) dealloc
{
    [fTorrents release];
    
    [super dealloc];
}

- (void) setInfoForTorrents: (NSArray *) torrents
{
    //don't check if it's the same in case the metadata changed
    [fTorrents release];
    fTorrents = [torrents retain];
    
    fSet = NO;
}

- (void) updateInfo
{
    if (!fSet)
        [self setupInfo];
    
    fSet = YES;
}

- (void) updateOptions
{
    if ([fTorrents count] == 0)
        return;
    
    //get bandwidth info
    NSEnumerator * enumerator = [fTorrents objectEnumerator];
    Torrent * torrent = [enumerator nextObject]; //first torrent
    
    NSInteger uploadUseSpeedLimit = [torrent usesSpeedLimit: YES] ? NSOnState : NSOffState,
                uploadSpeedLimit = [torrent speedLimit: YES],
                downloadUseSpeedLimit = [torrent usesSpeedLimit: NO] ? NSOnState : NSOffState,
                downloadSpeedLimit = [torrent speedLimit: NO],
                globalUseSpeedLimit = [torrent usesGlobalSpeedLimit] ? NSOnState : NSOffState;
    
    while ((torrent = [enumerator nextObject])
            && (uploadUseSpeedLimit != NSMixedState || uploadSpeedLimit != INVALID
                || downloadUseSpeedLimit != NSMixedState || downloadSpeedLimit != INVALID
                || globalUseSpeedLimit != NSMixedState))
    {
        if (uploadUseSpeedLimit != NSMixedState && uploadUseSpeedLimit != ([torrent usesSpeedLimit: YES] ? NSOnState : NSOffState))
            uploadUseSpeedLimit = NSMixedState;
        
        if (uploadSpeedLimit != INVALID && uploadSpeedLimit != [torrent speedLimit: YES])
            uploadSpeedLimit = INVALID;
        
        if (downloadUseSpeedLimit != NSMixedState && downloadUseSpeedLimit != ([torrent usesSpeedLimit: NO] ? NSOnState : NSOffState))
            downloadUseSpeedLimit = NSMixedState;
        
        if (downloadSpeedLimit != INVALID && downloadSpeedLimit != [torrent speedLimit: NO])
            downloadSpeedLimit = INVALID;
        
        if (globalUseSpeedLimit != NSMixedState && globalUseSpeedLimit != ([torrent usesGlobalSpeedLimit] ? NSOnState : NSOffState))
            globalUseSpeedLimit = NSMixedState;
    }
    
    //set upload view
    [fUploadLimitCheck setState: uploadUseSpeedLimit];
    [fUploadLimitCheck setEnabled: YES];
    
    [fUploadLimitLabel setEnabled: uploadUseSpeedLimit == NSOnState];
    [fUploadLimitField setEnabled: uploadUseSpeedLimit == NSOnState];
    if (uploadSpeedLimit != INVALID)
        [fUploadLimitField setIntValue: uploadSpeedLimit];
    else
        [fUploadLimitField setStringValue: @""];
    
    //set download view
    [fDownloadLimitCheck setState: downloadUseSpeedLimit];
    [fDownloadLimitCheck setEnabled: YES];
    
    [fDownloadLimitLabel setEnabled: downloadUseSpeedLimit == NSOnState];
    [fDownloadLimitField setEnabled: downloadUseSpeedLimit == NSOnState];
    if (downloadSpeedLimit != INVALID)
        [fDownloadLimitField setIntValue: downloadSpeedLimit];
    else
        [fDownloadLimitField setStringValue: @""];
    
    //set global check
    [fGlobalLimitCheck setState: globalUseSpeedLimit];
    [fGlobalLimitCheck setEnabled: YES];
    
    //get ratio info
    enumerator = [fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent
    
    NSInteger checkRatio = [torrent ratioSetting];
    CGFloat ratioLimit = [torrent ratioLimit];
    
    while ((torrent = [enumerator nextObject]) && (checkRatio != INVALID || ratioLimit != INVALID))
    {
        if (checkRatio != INVALID && checkRatio != [torrent ratioSetting])
            checkRatio = INVALID;
        
        if (ratioLimit != INVALID && ratioLimit != [torrent ratioLimit])
            ratioLimit = INVALID;
    }
    
    //set ratio view
    NSInteger index;
    if (checkRatio == TR_RATIOLIMIT_SINGLE)
        index = OPTION_POPUP_LIMIT;
    else if (checkRatio == TR_RATIOLIMIT_UNLIMITED)
        index = OPTION_POPUP_NO_LIMIT;
    else if (checkRatio == TR_RATIOLIMIT_GLOBAL)
        index = OPTION_POPUP_GLOBAL;
    else
        index = -1;
    [fRatioPopUp selectItemAtIndex: index];
    [fRatioPopUp setEnabled: YES];
    
    [fRatioLimitField setHidden: checkRatio != TR_RATIOLIMIT_SINGLE];
    if (ratioLimit != INVALID)
        [fRatioLimitField setFloatValue: ratioLimit];
    else
        [fRatioLimitField setStringValue: @""];
    
    //get priority info
    enumerator = [fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent
    
    NSInteger priority = [torrent priority];
    
    while ((torrent = [enumerator nextObject]) && priority != INVALID)
    {
        if (priority != INVALID && priority != [torrent priority])
            priority = INVALID;
    }
    
    //set priority view
    if (priority == TR_PRI_HIGH)
        index = OPTION_POPUP_PRIORITY_HIGH;
    else if (priority == TR_PRI_NORMAL)
        index = OPTION_POPUP_PRIORITY_NORMAL;
    else if (priority == TR_PRI_LOW)
        index = OPTION_POPUP_PRIORITY_LOW;
    else
        index = -1;
    [fPriorityPopUp selectItemAtIndex: index];
    [fPriorityPopUp setEnabled: YES];
    
    //get peer info
    enumerator = [fTorrents objectEnumerator];
    torrent = [enumerator nextObject]; //first torrent
    
    NSInteger maxPeers = [torrent maxPeerConnect];
    
    while ((torrent = [enumerator nextObject]))
    {
        if (maxPeers != [torrent maxPeerConnect])
        {
            maxPeers = INVALID;
            break;
        }
    }
    
    //set peer view
    [fPeersConnectField setEnabled: YES];
    [fPeersConnectLabel setEnabled: YES];
    if (maxPeers != INVALID)
        [fPeersConnectField setIntValue: maxPeers];
    else
        [fPeersConnectField setStringValue: @""];
}

- (void) setUseSpeedLimit: (id) sender
{
    const BOOL upload = sender == fUploadLimitCheck;
    
    if ([sender state] == NSMixedState)
        [sender setState: NSOnState];
    const BOOL limit = [sender state] == NSOnState;
    
    for (Torrent * torrent in fTorrents)
        [torrent setUseSpeedLimit: limit upload: upload];
    
    NSTextField * field = upload ? fUploadLimitField : fDownloadLimitField;
    [field setEnabled: limit];
    if (limit)
    {
        [field selectText: self];
        [[[self view] window] makeKeyAndOrderFront: self];
    }
    
    NSTextField * label = upload ? fUploadLimitLabel : fDownloadLimitLabel;
    [label setEnabled: limit];
}

- (void) setUseGlobalSpeedLimit: (id) sender
{
    if ([sender state] == NSMixedState)
        [sender setState: NSOnState];
    const BOOL limit = [sender state] == NSOnState;
    
    for (Torrent * torrent in fTorrents)
        [torrent setUseGlobalSpeedLimit: limit];
}

- (void) setSpeedLimit: (id) sender
{
    const BOOL upload = sender == fUploadLimitField;
    const NSInteger limit = [sender intValue];
    
    for (Torrent * torrent in fTorrents)
        [torrent setSpeedLimit: limit upload: upload];
}

- (void) setRatioSetting: (id) sender
{
    NSInteger setting;
    bool single = NO;
    switch ([sender indexOfSelectedItem])
    {
        case OPTION_POPUP_LIMIT:
            setting = TR_RATIOLIMIT_SINGLE;
            single = YES;
            break;
        case OPTION_POPUP_NO_LIMIT:
            setting = TR_RATIOLIMIT_UNLIMITED;
            break;
        case OPTION_POPUP_GLOBAL:
            setting = TR_RATIOLIMIT_GLOBAL;
            break;
        default:
            NSAssert1(NO, @"Unknown option selected in ratio popup: %d", [sender indexOfSelectedItem]);
            return;
    }
    
    for (Torrent * torrent in fTorrents)
        [torrent setRatioSetting: setting];
    
    [fRatioLimitField setHidden: !single];
    if (single)
    {
        [fRatioLimitField selectText: self];
        [[[self view] window] makeKeyAndOrderFront: self];
    }
}

- (void) setRatioLimit: (id) sender
{
    CGFloat limit = [sender floatValue];
    
    for (Torrent * torrent in fTorrents)
        [torrent setRatioLimit: limit];
}

- (void) setPriority: (id) sender
{
    tr_priority_t priority;
    switch ([sender indexOfSelectedItem])
    {
        case OPTION_POPUP_PRIORITY_HIGH:
            priority = TR_PRI_HIGH;
            break;
        case OPTION_POPUP_PRIORITY_NORMAL:
            priority = TR_PRI_NORMAL;
            break;
        case OPTION_POPUP_PRIORITY_LOW:
            priority = TR_PRI_LOW;
            break;
        default:
            NSAssert1(NO, @"Unknown option selected in priority popup: %d", [sender indexOfSelectedItem]);
            return;
    }
    
    for (Torrent * torrent in fTorrents)
        [torrent setPriority: priority];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: nil];
}

- (void) setPeersConnectLimit: (id) sender
{
    NSInteger limit = [sender intValue];
    
    for (Torrent * torrent in fTorrents)
        [torrent setMaxPeerConnect: limit];
}

- (BOOL) control: (NSControl *) control textShouldBeginEditing: (NSText *) fieldEditor
{
    [fInitialString release];
    fInitialString = [[control stringValue] retain];
    
    return YES;
}

- (BOOL) control: (NSControl *) control didFailToFormatString: (NSString *) string errorDescription: (NSString *) error
{
    NSBeep();
    if (fInitialString)
    {
        [control setStringValue: fInitialString];
        [fInitialString release];
        fInitialString = nil;
    }
    return NO;
}

@end

@implementation InfoOptionsViewController (Private)

- (void) setupInfo
{
    if ([fTorrents count] == 0)
    {
        [fUploadLimitCheck setEnabled: NO];
        [fUploadLimitCheck setState: NSOffState];
        [fUploadLimitField setEnabled: NO];
        [fUploadLimitLabel setEnabled: NO];
        [fUploadLimitField setStringValue: @""];
        
        [fDownloadLimitCheck setEnabled: NO];
        [fDownloadLimitCheck setState: NSOffState];
        [fDownloadLimitField setEnabled: NO];
        [fDownloadLimitLabel setEnabled: NO];
        [fDownloadLimitField setStringValue: @""];
        
        [fGlobalLimitCheck setEnabled: NO];
        [fGlobalLimitCheck setState: NSOffState];
        
        [fPriorityPopUp setEnabled: NO];
        [fPriorityPopUp selectItemAtIndex: -1];
        
        [fRatioPopUp setEnabled: NO];
        [fRatioPopUp selectItemAtIndex: -1];
        [fRatioLimitField setHidden: YES];
        [fRatioLimitField setStringValue: @""];
        
        [fPeersConnectField setEnabled: NO];
        [fPeersConnectField setStringValue: @""];
        [fPeersConnectLabel setEnabled: NO];
    }
    else
        [self updateOptions];
}

@end
