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
#import "ExpandedPathToIconTransformer.h"

@interface AddWindowController (Private)

- (void) folderChoiceClosed: (NSOpenPanel *) openPanel returnCode: (int) code contextInfo: (void *) contextInfo;

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
    }
    return self;
}

- (void) awakeFromNib
{
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateGroupMenu:)
        name: @"UpdateGroups" object: nil];
    
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
    
    [fGroupPopUp setMenu: [[GroupsWindowController groups] groupMenuWithTarget: nil action: NULL isSmall: NO]];
    
    [fStartCheck setState: [[NSUserDefaults standardUserDefaults] boolForKey: @"AutoStartDownload"] ? NSOnState : NSOffState];
    
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
        //if there is no destination, prompt for one right away
        [self setDestination: nil];
    }
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [fDestination release];
    
    [super dealloc];
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
    
    [fController askOpenConfirmed: fTorrent];
    
    if (fDeleteTorrent)
        [fTorrent trashTorrent];
    
    [self release];
}

- (void) cancelAdd: (id) sender
{
    [fTorrent closeRemoveTorrent];
    [fTorrent release];
    
    [self release];
}

- (void) updateGroupMenu: (NSNotification *) notification
{
    int groupValue = [[fGroupPopUp selectedItem] tag];
    [fGroupPopUp setMenu: [[GroupsWindowController groups] groupMenuWithTarget: nil action: NULL isSmall: NO]];
    [fGroupPopUp selectItemWithTag: groupValue];
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
        
        #warning make sure to reload file table
    }
    else
    {
        if (!fDestination)
            [self cancelAdd: nil];
    }
}

@end
