/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2008 Transmission authors and contributors
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

#import "CreatorWindowController.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import "utils.h" //tr_httpIsValidURL

#define TRACKER_ADD_TAG 0
#define TRACKER_REMOVE_TAG 1

@interface CreatorWindowController (Private)

+ (NSString *) chooseFile;
- (void) updateEnableOpenCheckForTrackers;
- (void) locationSheetClosed: (NSSavePanel *) openPanel returnCode: (int) code contextInfo: (void *) info;

- (void) createBlankAddressAlertDidEnd: (NSAlert *) alert returnCode: (int) returnCode contextInfo: (void *) contextInfo;
- (void) createReal;
- (void) checkProgress;
- (void) failureSheetClosed: (NSAlert *) alert returnCode: (int) code contextInfo: (void *) info;

@end

@implementation CreatorWindowController

+ (void) createTorrentFile: (tr_handle *) handle
{
    //get file/folder for torrent
    NSString * path;
    if (!(path = [CreatorWindowController chooseFile]))
        return;
    
    CreatorWindowController * creator = [[self alloc] initWithHandle: handle path: path];
    [creator showWindow: nil];
}

+ (void) createTorrentFile: (tr_handle *) handle forFile: (NSString *) file
{
    CreatorWindowController * creator = [[self alloc] initWithHandle: handle path: file];
    [creator showWindow: nil];
}

- (id) initWithHandle: (tr_handle *) handle path: (NSString *) path
{
    if ((self = [super initWithWindowNibName: @"Creator"]))
    {
        fStarted = NO;
        
        fPath = [path retain];
        fInfo = tr_metaInfoBuilderCreate(handle, [fPath UTF8String]);
        
        if (fInfo->fileCount == 0)
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> no files -> button")];
            [alert setMessageText: NSLocalizedString(@"This folder contains no files.",
                                                    "Create torrent -> no files -> title")];
            [alert setInformativeText: NSLocalizedString(@"There must be at least one file in a folder to create a torrent file.",
                                                        "Create torrent -> no files -> warning")];
            [alert setAlertStyle: NSWarningAlertStyle];
            
            [alert runModal];
            [alert release];
            
            [self release];
            return nil;
        }
        if (fInfo->totalSize == 0)
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> zero size -> button")];
            [alert setMessageText: NSLocalizedString(@"The total file size is zero bytes.",
                                                    "Create torrent -> zero size -> title")];
            [alert setInformativeText: NSLocalizedString(@"A torrent file cannot be created for files with no size.",
                                                        "Create torrent -> zero size -> warning")];
            [alert setAlertStyle: NSWarningAlertStyle];
            
            [alert runModal];
            [alert release];
            
            [self release];
            return nil;
        }
        
        fDefaults = [NSUserDefaults standardUserDefaults];
        
        //get list of trackers
        if (!(fTrackers = [[fDefaults arrayForKey: @"CreatorTrackers"] mutableCopy]))
        {
            fTrackers = [[NSMutableArray alloc] initWithCapacity: 1];
            
            //check for single tracker from versions before 1.3
            NSString * tracker;
            if ((tracker = [fDefaults stringForKey: @"CreatorTracker"]))
            {
                [fDefaults removeObjectForKey: @"CreatorTracker"];
                if (![tracker isEqualToString: @""])
                {
                    [fTrackers addObject: tracker];
                    [fDefaults setObject: fTrackers forKey: @"CreatorTrackers"];
                }
            }
        }
        
        //remove potentially invalid addresses
        for (NSInteger i = [fTrackers count]-1; i >= 0; i--)
        {
            if (!tr_httpIsValidURL([[fTrackers objectAtIndex: i] UTF8String]))
                [fTrackers removeObjectAtIndex: i];
        }
    }
    return self;
}

- (void) awakeFromNib
{
    NSString * name = [fPath lastPathComponent];
    
    [[self window] setTitle: name];
    
    [fNameField setStringValue: name];
    [fNameField setToolTip: fPath];
    
    BOOL multifile = !fInfo->isSingleFile;
    
    NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFileType: multifile
                        ? NSFileTypeForHFSTypeCode('fldr') : [fPath pathExtension]];
    [icon setSize: [fIconView frame].size];
    [fIconView setImage: icon];
    
    NSString * statusString = [NSString stringForFileSize: fInfo->totalSize];
    if (multifile)
    {
        NSString * fileString;
        NSInteger count = fInfo->fileCount;
        if (count != 1)
            fileString = [NSString stringWithFormat: NSLocalizedString(@"%d files", "Create torrent -> info"), count];
        else
            fileString = NSLocalizedString(@"1 file", "Create torrent -> info");
        statusString = [NSString stringWithFormat: @"%@, %@", fileString, statusString];
    }
    [fStatusField setStringValue: statusString];
    
    NSString * piecesCountString;
    int piecesCount = fInfo->pieceCount;
    if (piecesCount == 1)
        piecesCountString = NSLocalizedString(@"1 piece", "Create torrent -> info");
    else
        piecesCountString = [NSString stringWithFormat: NSLocalizedString(@"%d pieces", "Create torrent -> info"),
                                                                piecesCount];
    [fPiecesField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%@, %@ each", "Create torrent -> info"),
                                        piecesCountString, [NSString stringForFileSize: fInfo->pieceSize]]];
    
    fLocation = [[[[fDefaults stringForKey: @"CreatorLocation"] stringByExpandingTildeInPath] stringByAppendingPathComponent:
                    [name stringByAppendingPathExtension: @"torrent"]] retain];
    [fLocationField setStringValue: [fLocation stringByAbbreviatingWithTildeInPath]];
    [fLocationField setToolTip: fLocation];
    
    //set previously saved values
    if ([fDefaults objectForKey: @"CreatorPrivate"])
        [fPrivateCheck setState: [fDefaults boolForKey: @"CreatorPrivate"] ? NSOnState : NSOffState];
    
    fOpenTorrent = [fDefaults boolForKey: @"CreatorOpen"];
    [self updateEnableOpenCheckForTrackers];
    
    if (![NSApp isOnLeopardOrBetter])
    {
        [fTrackerAddRemoveControl sizeToFit];
        [fTrackerAddRemoveControl setLabel: @"+" forSegment: TRACKER_ADD_TAG];
        [fTrackerAddRemoveControl setLabel: @"-" forSegment: TRACKER_REMOVE_TAG];
    }
}

- (void) dealloc
{
    [fPath release];
    [fLocation release];
    
    [fTrackers release];
    
    if (fInfo)
        tr_metaInfoBuilderFree(fInfo);
    
    [fTimer invalidate];
    
    [super dealloc];
}

- (void) toggleOpenCheck: (id) sender
{
    fOpenTorrent = [fOpenCheck state] == NSOnState;
}

- (void) setLocation: (id) sender
{
    NSSavePanel * panel = [NSSavePanel savePanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Create torrent -> location sheet -> button")];
    [panel setMessage: NSLocalizedString(@"Select the name and location for the torrent file.",
                                        "Create torrent -> location sheet -> message")]; 
    
    [panel setRequiredFileType: @"torrent"];
    [panel setCanSelectHiddenExtension: YES];
    
    [panel beginSheetForDirectory: [fLocation stringByDeletingLastPathComponent] file: [fLocation lastPathComponent]
            modalForWindow: [self window] modalDelegate: self didEndSelector: @selector(locationSheetClosed:returnCode:contextInfo:)
            contextInfo: nil];
}

- (void) create: (id) sender
{
    if ([fTrackers count] == 0 && [fDefaults boolForKey: @"WarningCreatorBlankAddress"])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: NSLocalizedString(@"There is no tracker address.", "Create torrent -> blank address -> title")];
        [alert setInformativeText: NSLocalizedString(@"The torrent file will not be able to be opened."
            " A torrent file with no tracker address is only useful when you plan to upload the file to a tracker website"
            " that will add the address for you.", "Create torrent -> blank address -> message")];
        [alert addButtonWithTitle: NSLocalizedString(@"Create", "Create torrent -> blank address -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Cancel", "Create torrent -> blank address -> button")];
        
        if ([NSApp isOnLeopardOrBetter])
            [alert setShowsSuppressionButton: YES];
        else
            [alert addButtonWithTitle: NSLocalizedString(@"Don't Alert Again", "Create torrent -> blank address -> button")];

        [alert beginSheetModalForWindow: [self window] modalDelegate: self
            didEndSelector: @selector(createBlankAddressAlertDidEnd:returnCode:contextInfo:) contextInfo: nil];
    }
    else
        [self createReal];
}

- (void) cancelCreateWindow: (id) sender
{
    [[self window] close];
}

- (void) windowWillClose: (NSNotification *) notification
{
    [self release];
}

- (void) cancelCreateProgress: (id) sender
{
    fInfo->abortFlag = 1;
    [fTimer fire];
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableView
{
    return [fTrackers count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    return [fTrackers objectAtIndex: row];
}

- (void) addRemoveTracker: (id) sender
{
    //don't allow add/remove when currently adding - it leads to weird results
    if ([fTrackerTable editedRow] != -1)
        return;
    
    if ([[sender cell] tagForSegment: [sender selectedSegment]] == TRACKER_REMOVE_TAG)
    {
        [fTrackers removeObjectsAtIndexes: [fTrackerTable selectedRowIndexes]];
        
        [fTrackerTable deselectAll: self];
        [fTrackerTable reloadData];
        
        [self updateEnableOpenCheckForTrackers];
    }
    else
    {
        [fTrackers addObject: @""];
        [fTrackerTable reloadData];
        
        int row = [fTrackers count] - 1;
        [fTrackerTable selectRow: row byExtendingSelection: NO];
        [fTrackerTable editColumn: 0 row: row withEvent: nil select: YES];
    }
}

- (void) tableView: (NSTableView *) tableView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    NSString * tracker = (NSString *)object;
    
    tracker = [tracker stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    
    if ([tracker rangeOfString: @"://"].location == NSNotFound)
        tracker = [@"http://" stringByAppendingString: tracker];
    
    if (!tr_httpIsValidURL([tracker UTF8String]))
    {
        NSBeep();
        [fTrackers removeObjectAtIndex: row];
    }
    else
    {
        [fTrackers replaceObjectAtIndex: row withObject: tracker];
        [self updateEnableOpenCheckForTrackers];
    }
    
    [fTrackerTable deselectAll: self];
    [fTrackerTable reloadData];
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fTrackerAddRemoveControl setEnabled: [fTrackerTable numberOfSelectedRows] > 0 forSegment: TRACKER_REMOVE_TAG];
}

@end

@implementation CreatorWindowController (Private)

+ (NSString *) chooseFile
{
    NSOpenPanel * panel = [NSOpenPanel openPanel];
    
    [panel setTitle: NSLocalizedString(@"Create Torrent File", "Create torrent -> select file")];
    [panel setPrompt: NSLocalizedString(@"Select", "Create torrent -> select file")];
    [panel setAllowsMultipleSelection: NO];
    [panel setCanChooseFiles: YES];
    [panel setCanChooseDirectories: YES];
    [panel setCanCreateDirectories: NO];

    [panel setMessage: NSLocalizedString(@"Select a file or folder for the torrent file.", "Create torrent -> select file")];
    
    BOOL success = [panel runModal] == NSOKButton;
    return success ? [[panel filenames] objectAtIndex: 0] : nil;
}

- (void) updateEnableOpenCheckForTrackers
{
    BOOL hasTracker = [fTrackers count] > 0;
    [fOpenCheck setEnabled: hasTracker];
    [fOpenCheck setState: (fOpenTorrent && hasTracker) ? NSOnState : NSOffState];
}

- (void) locationSheetClosed: (NSSavePanel *) panel returnCode: (int) code contextInfo: (void *) info
{
    if (code == NSOKButton)
    {
        [fLocation release];
        fLocation = [[panel filename] retain];
        
        [fLocationField setStringValue: [fLocation stringByAbbreviatingWithTildeInPath]];
        [fLocationField setToolTip: fLocation];
    }
}

- (void) createBlankAddressAlertDidEnd: (NSAlert *) alert returnCode: (int) returnCode contextInfo: (void *) contextInfo
{
    if (([NSApp isOnLeopardOrBetter] ? [[alert suppressionButton] state] == NSOnState : returnCode == NSAlertThirdButtonReturn))
        [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningCreatorBlankAddress"];
    
    [alert release];
    
    if (returnCode == NSAlertFirstButtonReturn)
        [self performSelectorOnMainThread: @selector(createReal) withObject: nil waitUntilDone: NO];
}

- (void) createReal
{
    //check if a file with the same name and location already exists
    if ([[NSFileManager defaultManager] fileExistsAtPath: fLocation])
    {
        NSArray * pathComponents = [fLocation pathComponents];
        int count = [pathComponents count];
        
        NSAlert * alert = [[[NSAlert alloc] init] autorelease];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> file already exists warning -> button")];
        [alert setMessageText: NSLocalizedString(@"A torrent file with this name and directory cannot be created.",
                                                "Create torrent -> file already exists warning -> title")];
        [alert setInformativeText: [NSString stringWithFormat:
                NSLocalizedString(@"A file with the name \"%@\" already exists in the directory \"%@\". "
                    "Choose a new name or directory to create the torrent file.",
                    "Create torrent -> file already exists warning -> warning"),
                    [pathComponents objectAtIndex: count-1], [pathComponents objectAtIndex: count-2]]];
        [alert setAlertStyle: NSWarningAlertStyle];
        
        [alert beginSheetModalForWindow: [self window] modalDelegate: self didEndSelector: nil contextInfo: nil];
        return;
    }
    
    //parse non-empty tracker strings
    tr_tracker_info * trackerInfo = tr_new0(tr_tracker_info, [fTrackers count]);
    
    for (NSUInteger i = 0; i < [fTrackers count]; i++)
        trackerInfo[i].announce = (char *)[[fTrackers objectAtIndex: i] UTF8String];
    
    //store values
    [fDefaults setObject: fTrackers forKey: @"CreatorTrackers"];
    [fDefaults setBool: [fPrivateCheck state] == NSOnState forKey: @"CreatorPrivate"];
    [fDefaults setBool: fOpenTorrent forKey: @"CreatorOpen"];
    [fDefaults setObject: [fLocation stringByDeletingLastPathComponent] forKey: @"CreatorLocation"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"BeginCreateTorrentFile" object: fLocation userInfo: nil];
    tr_makeMetaInfo(fInfo, [fLocation UTF8String], trackerInfo, [fTrackers count], [[fCommentView string] UTF8String],
                    [fPrivateCheck state] == NSOnState);
    tr_free(trackerInfo);
    
    fTimer = [NSTimer scheduledTimerWithTimeInterval: 0.1 target: self selector: @selector(checkProgress)
                userInfo: nil repeats: YES];
}

- (void) checkProgress
{
    if (fInfo->isDone)
    {
        [fTimer invalidate];
        fTimer = nil;
        
        NSAlert * alert;
        switch (fInfo->result)
        {
            case TR_MAKEMETA_OK:
                if (fOpenTorrent && [fTrackers count] > 0)
                {
                    NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys: fLocation, @"File",
                                            [fPath stringByDeletingLastPathComponent], @"Path", nil];
                    [[NSNotificationCenter defaultCenter] postNotificationName: @"OpenCreatedTorrentFile" object: self userInfo: dict];
                }
                
                [[self window] close];
                break;
            
            case TR_MAKEMETA_CANCELLED:
                [[self window] close];
                break;
            
            default:
                alert = [[[NSAlert alloc] init] autorelease];
                [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> failed -> button")];
                [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"Creation of \"%@\" failed.",
                                                "Create torrent -> failed -> title"), [fLocation lastPathComponent]]];
                [alert setAlertStyle: NSWarningAlertStyle];
                
                if (fInfo->result == TR_MAKEMETA_IO_READ)
                    [alert setInformativeText: [NSString stringWithFormat: NSLocalizedString(@"Could not read \"%s\": %s.",
                        "Create torrent -> failed -> warning"), fInfo->errfile, strerror(fInfo->my_errno)]];
                else if (fInfo->result == TR_MAKEMETA_IO_WRITE)
                    [alert setInformativeText: [NSString stringWithFormat: NSLocalizedString(@"Could not write \"%s\": %s.",
                        "Create torrent -> failed -> warning"), fInfo->errfile, strerror(fInfo->my_errno)]];
                else; //invalid url should have been caught before creating
                
                [alert beginSheetModalForWindow: [self window] modalDelegate: self
                        didEndSelector: @selector(failureSheetClosed:returnCode:contextInfo:) contextInfo: nil];
        }
    }
    else
    {
        [fProgressIndicator setDoubleValue: (double)fInfo->pieceIndex / fInfo->pieceCount];
        
        if (!fStarted)
        {
            fStarted = YES;
            
            [fProgressView setHidden: YES];
            
            NSWindow * window = [self window];
            
            NSRect windowRect = [window frame];
            CGFloat difference = [fProgressView frame].size.height - [[window contentView] frame].size.height;
            windowRect.origin.y -= difference;
            windowRect.size.height += difference;
            
            //don't allow vertical resizing
            CGFloat height = windowRect.size.height;
            [window setMinSize: NSMakeSize([window minSize].width, height)];
            [window setMaxSize: NSMakeSize([window maxSize].width, height)];
            
            [window setContentView: fProgressView];
            [window setFrame: windowRect display: YES animate: YES];
            [fProgressView setHidden: NO];
            
            [[window standardWindowButton: NSWindowCloseButton] setEnabled: NO];
        }
    }
}

- (void) failureSheetClosed: (NSAlert *) alert returnCode: (int) code contextInfo: (void *) info
{
    [[alert window] orderOut: nil];
    [[self window] close];
}

@end
