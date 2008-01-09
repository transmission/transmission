/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2008 Transmission authors and contributors
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
#import "GroupsWindowController.h"
#import "NSStringAdditions.h"
#import "NSMenuAdditions.h"
#import "ExpandedPathToIconTransformer.h"

@interface AddWindowController (Private)

- (void) folderChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) contextInfo;
- (void) setGroupsMenu;
- (void) changeGroupValue: (id) sender;

@end

@implementation AddWindowController

- (id) initWithTorrent: (Torrent *) torrent destination: (NSString *) path controller: (Controller *) controller
        deleteTorrent: (torrentFileState) deleteTorrent
{
    if ((self = [super initWithWindowNibName: @"AddWindow"]))
    {
        fTorrent = torrent;
        if (path)
            fDestination = [[path stringByExpandingTildeInPath] retain];
        
        fController = controller;
        
        fDeleteTorrent = deleteTorrent == TORRENT_FILE_DELETE || (deleteTorrent == TORRENT_FILE_DEFAULT
                            && [[NSUserDefaults standardUserDefaults] boolForKey: @"DeleteOriginalTorrent"]);
        fDeleteEnable = deleteTorrent == TORRENT_FILE_DEFAULT;
        
        fGroupValue = -1;
    }
    return self;
}

- (void) awakeFromNib
{
    [[self window] setAcceptsMouseMovedEvents: YES];
    
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateGroupMenu:)
        name: @"UpdateGroups" object: nil];
    
    [fFileController setTorrent: fTorrent];
    
    NSString * name = [fTorrent name];
    [[self window] setTitle: name];
    [fNameField setStringValue: name];
    
    NSImage * icon = [[fTorrent icon] copy];
    [icon setFlipped: NO];
    [fIconView setImage: icon];
    [icon release];
    
    NSString * statusString = [NSString stringForFileSize: [fTorrent size]];
    if ([fTorrent folder])
    {
        NSString * fileString;
        int count = [fTorrent fileCount];
        if (count != 1)
            fileString = [NSString stringWithFormat: NSLocalizedString(@"%d Files, ", "Add torrent -> info"), count];
        else
            fileString = NSLocalizedString(@"1 File, ", "Add torrent -> info");
        statusString = [fileString stringByAppendingString: statusString];
    }
    [fStatusField setStringValue: statusString];
    
    [self setGroupsMenu];
    [fGroupPopUp selectItemWithTag: -1];
    
    [fStartCheck setState: [[NSUserDefaults standardUserDefaults] boolForKey: @"AutoStartDownload"] ? NSOnState : NSOffState];
    
    [fDeleteCheck setState: fDeleteTorrent ? NSOnState : NSOffState];
    [fDeleteCheck setEnabled: fDeleteEnable];
    
    if (fDestination)
    {
        [fLocationField setStringValue: [fDestination stringByAbbreviatingWithTildeInPath]];
        [fLocationField setToolTip: fDestination];
        
        ExpandedPathToIconTransformer * iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
        [fLocationImageView setImage: [iconTransformer transformedValue: fDestination]];
        [iconTransformer release];
    }
    else
    {
        [fLocationField setStringValue: @""];
        [fLocationImageView setImage: nil];
    }
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
    
    [fDestination release];
    
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
    
    [panel beginSheetForDirectory: nil file: nil types: nil modalForWindow: [self window] modalDelegate: self
            didEndSelector: @selector(folderChoiceClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) add: (id) sender
{
    [fTorrent setWaitToStart: [fStartCheck state] == NSOnState];
    [fTorrent setGroupValue: [[fGroupPopUp selectedItem] tag]];
    
    if ([fDeleteCheck state] == NSOnState)
        [fTorrent trashTorrent];
    
    //ensure last, since it releases this controller
    [fController askOpenConfirmed: self add: YES];
}

- (void) cancelAdd: (id) sender
{
    [fController askOpenConfirmed: self add: NO];
}

- (void) updateGroupMenu: (NSNotification *) notification
{
    [self setGroupsMenu];
    if (![fGroupPopUp selectItemWithTag: fGroupValue])
    {
        fGroupValue = -1;
        [fGroupPopUp selectItemWithTag: fGroupValue];
    }
}

- (void) showGroupsWindow: (id) sender
{
    [fGroupPopUp selectItemWithTag: fGroupValue];
    [fController showGroups: sender];
}

- (void) mouseMoved: (NSEvent *) event
{
    [fFileController setHoverRowForEvent: event];
}

@end

@implementation AddWindowController (Private)

- (void) folderChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) contextInfo
{
    if (code == NSOKButton)
    {
        [fDestination release];
        fDestination = [[[openPanel filenames] objectAtIndex: 0] retain];
        
        [fLocationField setStringValue: [fDestination stringByAbbreviatingWithTildeInPath]];
        [fLocationField setToolTip: fDestination];
        
        ExpandedPathToIconTransformer * iconTransformer = [[ExpandedPathToIconTransformer alloc] init];
        [fLocationImageView setImage: [iconTransformer transformedValue: fDestination]];
        [iconTransformer release];
        
        [fTorrent changeDownloadFolder: fDestination];
        
        [fFileController reloadData];
    }
    else
    {
        if (!fDestination)
            [self performSelectorOnMainThread: @selector(cancelAdd:) withObject: nil waitUntilDone: NO];
    }
}

- (void) setGroupsMenu
{
    NSMenu * menu = [fGroupPopUp menu];
    
    int i;
    for (i = [menu numberOfItems]-1 - 2; i >= 0; i--)
        [menu removeItemAtIndex: i];
        
    NSMenu * groupMenu = [[GroupsWindowController groups] groupMenuWithTarget: self action: @selector(changeGroupValue:) isSmall: NO];
    [menu appendItemsFromMenu: groupMenu atIndexes: [NSIndexSet indexSetWithIndexesInRange:
            NSMakeRange(0, [groupMenu numberOfItems])] atBottom: NO];
}

- (void) changeGroupValue: (id) sender
{
    fGroupValue = [sender tag];
}

@end
