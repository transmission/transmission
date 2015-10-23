/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2008-2012 Transmission authors and contributors
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

#import "AddWindowController.h"
#import "Controller.h"
#import "ExpandedPathToIconTransformer.h"
#import "FileOutlineController.h"
#import "GroupsController.h"
#import "NSStringAdditions.h"
#import "Torrent.h"

#define UPDATE_SECONDS 1.0

#define POPUP_PRIORITY_HIGH 0
#define POPUP_PRIORITY_NORMAL 1
#define POPUP_PRIORITY_LOW 2

@interface AddWindowController (Private)

- (void) updateFiles;

- (void) confirmAdd;

- (void) setDestinationPath: (NSString *) destination determinationType: (TorrentDeterminationType) determinationType;

- (void) setGroupsMenu;
- (void) changeGroupValue: (id) sender;

- (void) sameNameAlertDidEnd: (NSAlert *) alert returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo;

@end

@implementation AddWindowController

- (id) initWithTorrent: (Torrent *) torrent destination: (NSString *) path lockDestination: (BOOL) lockDestination
    controller: (Controller *) controller torrentFile: (NSString *) torrentFile
    deleteTorrentCheckEnableInitially: (BOOL) deleteTorrent canToggleDelete: (BOOL) canToggleDelete
{
    if ((self = [super initWithWindowNibName: @"AddWindow"]))
    {
        fTorrent = torrent;
        fDestination = [[path stringByExpandingTildeInPath] retain];
        fLockDestination = lockDestination;
        
        fController = controller;
        
        fTorrentFile = [[torrentFile stringByExpandingTildeInPath] retain];
        
        fDeleteTorrentEnableInitially = deleteTorrent;
        fCanToggleDelete = canToggleDelete;
        
        fGroupValue = [torrent groupValue];
        fGroupValueDetermination = TorrentDeterminationAutomatic;

        [fVerifyIndicator setUsesThreadedAnimation: YES];
    }
    return self;
}

- (void) awakeFromNib
{
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateCheckButtons:) name: @"TorrentFileCheckChange" object: fTorrent];
    
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateGroupMenu:) name: @"UpdateGroups" object: nil];
    
    [fFileController setTorrent: fTorrent];
    
    NSString * name = [fTorrent name];
    [[self window] setTitle: name];
    [fNameField setStringValue: name];
    [fNameField setToolTip: name];
    
    [fIconView setImage: [fTorrent icon]];
    
    if (![fTorrent isFolder])
    {
        [fFileFilterField setHidden: YES];
        [fCheckAllButton setHidden: YES];
        [fUncheckAllButton setHidden: YES];
        
        NSRect scrollFrame = [fFileScrollView frame];
        const CGFloat diff = NSMinY([fFileScrollView frame]) - NSMinY([fFileFilterField frame]);
        scrollFrame.origin.y -= diff;
        scrollFrame.size.height += diff;
        [fFileScrollView setFrame: scrollFrame];
    }
    else
        [self updateCheckButtons: nil];
    
    [self setGroupsMenu];
    [fGroupPopUp selectItemWithTag: fGroupValue];
    
    NSInteger priorityIndex;
    switch ([fTorrent priority])
    {
        case TR_PRI_HIGH: priorityIndex = POPUP_PRIORITY_HIGH; break;
        case TR_PRI_NORMAL: priorityIndex = POPUP_PRIORITY_NORMAL; break;
        case TR_PRI_LOW: priorityIndex = POPUP_PRIORITY_LOW; break;
        default: NSAssert1(NO, @"Unknown priority for adding torrent: %d", [fTorrent priority]);
    }
    [fPriorityPopUp selectItemAtIndex: priorityIndex];
    
    [fStartCheck setState: [[NSUserDefaults standardUserDefaults] boolForKey: @"AutoStartDownload"] ? NSOnState : NSOffState];
    
    [fDeleteCheck setState: fDeleteTorrentEnableInitially ? NSOnState : NSOffState];
    [fDeleteCheck setEnabled: fCanToggleDelete];
    
    if (fDestination)
        [self setDestinationPath: fDestination determinationType: (fLockDestination ? TorrentDeterminationUserSpecified : TorrentDeterminationAutomatic)];
    else
    {
        [fLocationField setStringValue: @""];
        [fLocationImageView setImage: nil];
    }
    
    fTimer = [[NSTimer scheduledTimerWithTimeInterval: UPDATE_SECONDS target: self
                selector: @selector(updateFiles) userInfo: nil repeats: YES] retain];
    [self updateFiles];
}

- (void) windowDidLoad
{
    //if there is no destination, prompt for one right away
    if (!fDestination)
        [self setDestination: nil];
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fTimer invalidate];
    [fTimer release];
    
    [fDestination release];
    [fTorrentFile release];
    
    [super dealloc];
}

- (Torrent *) torrent
{
    return fTorrent;
}

- (void) setDestination: (id) sender
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Open torrent -> prompt")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: NO];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: YES];
    
    [panel setMessage: [NSString stringWithFormat: NSLocalizedString(@"Select the download folder for \"%@\"",
                        "Add -> select destination folder"), [fTorrent name]]];
    
    [panel beginSheetModalForWindow: [self window] completionHandler: ^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            fLockDestination = YES;
            [self setDestinationPath: [[[panel URLs] objectAtIndex: 0] path] determinationType: TorrentDeterminationUserSpecified];
        }
        else
        {
            if (!fDestination)
                [self performSelectorOnMainThread: @selector(cancelAdd:) withObject: nil waitUntilDone: NO];
        }
    }];
}

- (void) add: (id) sender
{
    if ([[fDestination lastPathComponent] isEqualToString: [fTorrent name]]
        && [[NSUserDefaults standardUserDefaults] boolForKey: @"WarningFolderDataSameName"])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: NSLocalizedString(@"The destination directory and root data directory have the same name.",
                                "Add torrent -> same name -> title")];
        [alert setInformativeText: NSLocalizedString(@"If you are attempting to use already existing data,"
            " the root data directory should be inside the destination directory.", "Add torrent -> same name -> message")];
        [alert setAlertStyle: NSWarningAlertStyle];
        [alert addButtonWithTitle: NSLocalizedString(@"Cancel", "Add torrent -> same name -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Add", "Add torrent -> same name -> button")];
        [alert setShowsSuppressionButton: YES];
        
        [alert beginSheetModalForWindow: [self window] modalDelegate: self
            didEndSelector: @selector(sameNameAlertDidEnd:returnCode:contextInfo:) contextInfo: nil];
    }
    else
        [self confirmAdd];
}

- (void) cancelAdd: (id) sender
{
    [[self window] performClose: sender];
}

//only called on cancel
- (BOOL) windowShouldClose: (id) window
{
    [fTimer invalidate];
    [fTimer release];
    fTimer = nil;
    
    [fFileController setTorrent: nil]; //avoid a crash when window tries to update
    
    [fController askOpenConfirmed: self add: NO];
    return YES;
}

- (void) setFileFilterText: (id) sender
{
    [fFileController setFilterText: [sender stringValue]];
}

- (IBAction) checkAll: (id) sender
{
    [fFileController checkAll];
}

- (IBAction) uncheckAll: (id) sender
{
    [fFileController uncheckAll];
}

- (void) verifyLocalData: (id) sender
{
    [fTorrent resetCache];
    [self updateFiles];
}

- (void) changePriority: (id) sender
{
    tr_priority_t priority;
    switch ([sender indexOfSelectedItem])
    {
        case POPUP_PRIORITY_HIGH: priority = TR_PRI_HIGH; break;
        case POPUP_PRIORITY_NORMAL: priority = TR_PRI_NORMAL; break;
        case POPUP_PRIORITY_LOW: priority = TR_PRI_LOW; break;
        default: NSAssert1(NO, @"Unknown priority tag for adding torrent: %ld", [sender tag]);
    }
    [fTorrent setPriority: priority];
}

- (void) updateCheckButtons: (NSNotification *) notification
{
    NSString * statusString = [NSString stringForFileSize: [fTorrent size]];
    if ([fTorrent isFolder])
    {
        //check buttons
        //keep synced with identical code in InfoFileViewController.m
        const NSInteger filesCheckState = [fTorrent checkForFiles: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fTorrent fileCount])]];
        [fCheckAllButton setEnabled: filesCheckState != NSOnState]; //if anything is unchecked
        [fUncheckAllButton setEnabled: ![fTorrent allDownloaded]]; //if there are any checked files that aren't finished
        
        //status field
        NSString * fileString;
        NSInteger count = [fTorrent fileCount];
        if (count != 1)
            fileString = [NSString stringWithFormat: NSLocalizedString(@"%@ files", "Add torrent -> info"),
                            [NSString formattedUInteger: count]];
        else
            fileString = NSLocalizedString(@"1 file", "Add torrent -> info");
        
        NSString * selectedString = [NSString stringWithFormat: NSLocalizedString(@"%@ selected", "Add torrent -> info"),
                                        [NSString stringForFileSize: [fTorrent totalSizeSelected]]];
        
        statusString = [NSString stringWithFormat: @"%@, %@ (%@)", fileString, statusString, selectedString];
    }
    
    [fStatusField setStringValue: statusString];
}

- (void) updateGroupMenu: (NSNotification *) notification
{
    [self setGroupsMenu];
    if (![fGroupPopUp selectItemWithTag: fGroupValue])
    {
        fGroupValue = -1;
		fGroupValueDetermination = TorrentDeterminationAutomatic;
        [fGroupPopUp selectItemWithTag: fGroupValue];
    }
}

@end

@implementation AddWindowController (Private)

- (void) updateFiles
{
    [fTorrent update];
    
    [fFileController refresh];
    
    [self updateCheckButtons: nil]; //call in case button state changed by checking
    
    if ([fTorrent isChecking])
    {
        const BOOL waiting = [fTorrent isCheckingWaiting];
        [fVerifyIndicator setIndeterminate: waiting];
        if (waiting)
            [fVerifyIndicator startAnimation: self];
        else
            [fVerifyIndicator setDoubleValue: [fTorrent checkingProgress]];
    }
    else {
        [fVerifyIndicator setIndeterminate: YES]; //we want to hide when stopped, which only applies when indeterminate
        [fVerifyIndicator stopAnimation: self];
    }
}

- (void) confirmAdd
{
    [fTimer invalidate];
    [fTimer release];
    fTimer = nil;
    [fTorrent setGroupValue: fGroupValue  determinationType: fGroupValueDetermination];

    if (fTorrentFile && fCanToggleDelete && [fDeleteCheck state] == NSOnState)
        [Torrent trashFile: fTorrentFile error: nil];
    
    if ([fStartCheck state] == NSOnState)
        [fTorrent startTransfer];
    
    [fFileController setTorrent: nil]; //avoid a crash when window tries to update
    
    [self close];
    [fController askOpenConfirmed: self add: YES]; //ensure last, since it releases this controller
}

- (void) setDestinationPath: (NSString *) destination determinationType: (TorrentDeterminationType) determinationType
{
    destination = [destination stringByExpandingTildeInPath];
    if (!fDestination || ![fDestination isEqualToString: destination])
    { 
        [fDestination release];
        fDestination = [destination retain];
        
        [fTorrent changeDownloadFolderBeforeUsing: fDestination determinationType: determinationType];
    }
    
    [fLocationField setStringValue: [fDestination stringByAbbreviatingWithTildeInPath]];
    [fLocationField setToolTip: fDestination];
    
    ExpandedPathToIconTransformer * iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
    [fLocationImageView setImage: [iconTransformer transformedValue: fDestination]];
    [iconTransformer release];
}

- (void) setGroupsMenu
{
    NSMenu * groupMenu = [[GroupsController groups] groupMenuWithTarget: self action: @selector(changeGroupValue:) isSmall: NO];
    [fGroupPopUp setMenu: groupMenu];
}

- (void) changeGroupValue: (id) sender
{
    NSInteger previousGroup = fGroupValue;
    fGroupValue = [sender tag];
    fGroupValueDetermination = TorrentDeterminationUserSpecified;

    if (!fLockDestination)
    {
        if ([[GroupsController groups] usesCustomDownloadLocationForIndex: fGroupValue])
            [self setDestinationPath: [[GroupsController groups] customDownloadLocationForIndex: fGroupValue] determinationType: TorrentDeterminationAutomatic];
        else if ([fDestination isEqualToString: [[GroupsController groups] customDownloadLocationForIndex: previousGroup]])
            [self setDestinationPath: [[NSUserDefaults standardUserDefaults] stringForKey: @"DownloadFolder"] determinationType: TorrentDeterminationAutomatic];
        else;
    }
}

- (void) sameNameAlertDidEnd: (NSAlert *) alert returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo
{
    if ([[alert suppressionButton] state] == NSOnState)
        [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningFolderDataSameName"];
    
    [alert release];
    
    if (returnCode == NSAlertSecondButtonReturn)
        [self performSelectorOnMainThread: @selector(confirmAdd) withObject: nil waitUntilDone: NO];
}

@end
