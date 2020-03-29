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

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> // tr_urlIsValidTracker()

#import "CreatorWindowController.h"
#import "Controller.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define TRACKER_ADD_TAG 0
#define TRACKER_REMOVE_TAG 1

@interface CreatorWindowController (Private)

+ (NSURL *) chooseFile;

- (void) updateLocationField;
- (void) createBlankAddressAlertDidEnd: (NSAlert *) alert returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo;
- (void) createReal;
- (void) checkProgress;
- (void) failureSheetClosed: (NSAlert *) alert returnCode: (NSInteger) code contextInfo: (void *) info;

@end

NSMutableSet *creatorWindowControllerSet = nil;
@implementation CreatorWindowController

+ (CreatorWindowController *) createTorrentFile: (tr_session *) handle
{
    //get file/folder for torrent
    NSURL * path;
    if (!(path = [CreatorWindowController chooseFile]))
        return nil;

    CreatorWindowController * creator = [[self alloc] initWithHandle: handle path: path];
    [creator showWindow: nil];
    return creator;
}

+ (CreatorWindowController *) createTorrentFile: (tr_session *) handle forFile: (NSURL *) file
{
    CreatorWindowController * creator = [[self alloc] initWithHandle: handle path: file];
    [creator showWindow: nil];
    return creator;
}

- (id) initWithHandle: (tr_session *) handle path: (NSURL *) path
{
    if ((self = [super initWithWindowNibName: @"Creator"]))
    {
        if (!creatorWindowControllerSet) {
            creatorWindowControllerSet = [NSMutableSet set];
        }
        
        fStarted = NO;

        fPath = path;
        fInfo = tr_metaInfoBuilderCreate([[fPath path] UTF8String]);

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

            return nil;
        }

        fDefaults = [NSUserDefaults standardUserDefaults];

        //get list of trackers
        if (!(fTrackers = [[fDefaults arrayForKey: @"CreatorTrackers"] mutableCopy]))
        {
            fTrackers = [[NSMutableArray alloc] init];

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
            if (!tr_urlIsValidTracker([fTrackers[i] UTF8String]))
                [fTrackers removeObjectAtIndex: i];
        }
        
        [creatorWindowControllerSet addObject:self];
    }
    return self;
}

- (void) awakeFromNib
{
    [[self window] setRestorationClass: [self class]];

    NSString * name = [fPath lastPathComponent];

    [[self window] setTitle: name];

    [fNameField setStringValue: name];
    [fNameField setToolTip: [fPath path]];

    const BOOL multifile = fInfo->isFolder;

    NSImage * icon = [[NSWorkspace sharedWorkspace] iconForFileType: multifile
                        ? NSFileTypeForHFSTypeCode(kGenericFolderIcon) : [fPath pathExtension]];
    [icon setSize: [fIconView frame].size];
    [fIconView setImage: icon];

    NSString * statusString = [NSString stringForFileSize: fInfo->totalSize];
    if (multifile)
    {
        NSString * fileString;
        NSInteger count = fInfo->fileCount;
        if (count != 1)
            fileString = [NSString stringWithFormat: NSLocalizedString(@"%@ files", "Create torrent -> info"),
                            [NSString formattedUInteger: count]];
        else
            fileString = NSLocalizedString(@"1 file", "Create torrent -> info");
        statusString = [NSString stringWithFormat: @"%@, %@", fileString, statusString];
    }
    [fStatusField setStringValue: statusString];

    if (fInfo->pieceCount == 1)
        [fPiecesField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"1 piece, %@", "Create torrent -> info"),
                                                            [NSString stringForFileSize: fInfo->pieceSize]]];
    else
        [fPiecesField setStringValue: [NSString stringWithFormat: NSLocalizedString(@"%d pieces, %@ each", "Create torrent -> info"),
                                                            fInfo->pieceCount, [NSString stringForFileSize: fInfo->pieceSize]]];

    fLocation = [[fDefaults URLForKey: @"CreatorLocationURL"] URLByAppendingPathComponent: [name stringByAppendingPathExtension: @"torrent"]];
    if (!fLocation)
    {
        //for 2.5 and earlier
        #warning we still store "CreatorLocation" in Defaults.plist, and not "CreatorLocationURL"
        NSString * location = [fDefaults stringForKey: @"CreatorLocation"];
        fLocation = [[NSURL alloc] initFileURLWithPath: [[location stringByExpandingTildeInPath] stringByAppendingPathComponent: [name stringByAppendingPathExtension: @"torrent"]]];
    }
    [self updateLocationField];

    //set previously saved values
    if ([fDefaults objectForKey: @"CreatorPrivate"])
        [fPrivateCheck setState: [fDefaults boolForKey: @"CreatorPrivate"] ? NSOnState : NSOffState];

    [fOpenCheck setState: [fDefaults boolForKey: @"CreatorOpen"] ? NSOnState : NSOffState];
}

- (void) dealloc
{
    if (fInfo)
        tr_metaInfoBuilderFree(fInfo);

    [fTimer invalidate];
}

+ (void) restoreWindowWithIdentifier: (NSString *) identifier state: (NSCoder *) state completionHandler: (void (^)(NSWindow *, NSError *)) completionHandler
{
    NSURL * path = [state decodeObjectForKey: @"TRCreatorPath"];
    if (!path || ![path checkResourceIsReachableAndReturnError: nil])
    {
        completionHandler(nil, [NSError errorWithDomain: NSURLErrorDomain code: NSURLErrorCannotOpenFile userInfo: nil]);
        return;
    }

    NSWindow * window = [[self createTorrentFile: [(Controller *)[NSApp delegate] sessionHandle] forFile: path] window];
    completionHandler(window, nil);
}

- (void) window: (NSWindow *) window willEncodeRestorableState: (NSCoder *) state
{
    [state encodeObject: fPath forKey: @"TRCreatorPath"];
    [state encodeObject: fLocation forKey: @"TRCreatorLocation"];
    [state encodeObject: fTrackers forKey: @"TRCreatorTrackers"];
    [state encodeInteger: [fOpenCheck state] forKey: @"TRCreatorOpenCheck"];
    [state encodeInteger: [fPrivateCheck state] forKey: @"TRCreatorPrivateCheck"];
    [state encodeObject: [fCommentView string] forKey: @"TRCreatorPrivateComment"];
}

- (void) window: (NSWindow *) window didDecodeRestorableState: (NSCoder *) coder
{
    fLocation = [coder decodeObjectForKey: @"TRCreatorLocation"];
    [self updateLocationField];

    fTrackers = [coder decodeObjectForKey: @"TRCreatorTrackers"];
    [fTrackerTable reloadData];

    [fOpenCheck setState: [coder decodeIntegerForKey: @"TRCreatorOpenCheck"]];
    [fPrivateCheck setState: [coder decodeIntegerForKey: @"TRCreatorPrivateCheck"]];
    [fCommentView setString: [coder decodeObjectForKey: @"TRCreatorPrivateComment"]];
}

- (IBAction) setLocation: (id) sender
{
    NSSavePanel * panel = [NSSavePanel savePanel];

    [panel setPrompt: NSLocalizedString(@"Select", "Create torrent -> location sheet -> button")];
    [panel setMessage: NSLocalizedString(@"Select the name and location for the torrent file.",
                                        "Create torrent -> location sheet -> message")];

    [panel setAllowedFileTypes: @[@"org.bittorrent.torrent", @"torrent"]];
    [panel setCanSelectHiddenExtension: YES];

    [panel setDirectoryURL: [fLocation URLByDeletingLastPathComponent]];
    [panel setNameFieldStringValue: [fLocation lastPathComponent]];

    [panel beginSheetModalForWindow: [self window] completionHandler: ^(NSInteger result) {
        if (result == NSFileHandlingPanelOKButton)
        {
            fLocation = [panel URL];
            [self updateLocationField];
        }
    }];
}

- (IBAction) create: (id) sender
{
    //make sure the trackers are no longer being verified
    if ([fTrackerTable editedRow] != -1)
        [[self window] endEditingFor: fTrackerTable];

    const BOOL isPrivate = [fPrivateCheck state] == NSOnState;
    if ([fTrackers count] == 0
        && [fDefaults boolForKey: isPrivate ? @"WarningCreatorPrivateBlankAddress" : @"WarningCreatorBlankAddress"])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText: NSLocalizedString(@"There are no tracker addresses.", "Create torrent -> blank address -> title")];

        NSString * infoString = isPrivate
                    ? NSLocalizedString(@"A transfer marked as private with no tracker addresses will be unable to connect to peers."
                        " The torrent file will only be useful if you plan to upload the file to a tracker website"
                        " that will add the addresses for you.", "Create torrent -> blank address -> message")
                    : NSLocalizedString(@"The transfer will not contact trackers for peers, and will have to rely solely on"
                        " non-tracker peer discovery methods such as PEX and DHT to download and seed.",
                        "Create torrent -> blank address -> message");

        [alert setInformativeText: infoString];
        [alert addButtonWithTitle: NSLocalizedString(@"Create", "Create torrent -> blank address -> button")];
        [alert addButtonWithTitle: NSLocalizedString(@"Cancel", "Create torrent -> blank address -> button")];
        [alert setShowsSuppressionButton: YES];

        [alert beginSheetModalForWindow: [self window] modalDelegate: self
            didEndSelector: @selector(createBlankAddressAlertDidEnd:returnCode:contextInfo:) contextInfo: nil];
    }
    else
        [self createReal];
}

- (IBAction) cancelCreateWindow: (id) sender
{
    [[self window] close];
}

- (void) windowWillClose: (NSNotification *) notification
{
    [creatorWindowControllerSet removeObject:self];
}

- (IBAction) cancelCreateProgress: (id) sender
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
    return fTrackers[row];
}

- (IBAction) addRemoveTracker: (id) sender
{
    //don't allow add/remove when currently adding - it leads to weird results
    if ([fTrackerTable editedRow] != -1)
        return;

    if ([[sender cell] tagForSegment: [sender selectedSegment]] == TRACKER_REMOVE_TAG)
    {
        [fTrackers removeObjectsAtIndexes: [fTrackerTable selectedRowIndexes]];

        [fTrackerTable deselectAll: self];
        [fTrackerTable reloadData];
    }
    else
    {
        [fTrackers addObject: @""];
        [fTrackerTable reloadData];

        const NSInteger row = [fTrackers count] - 1;
        [fTrackerTable selectRowIndexes: [NSIndexSet indexSetWithIndex: row] byExtendingSelection: NO];
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

    if (!tr_urlIsValidTracker([tracker UTF8String]))
    {
        NSBeep();
        [fTrackers removeObjectAtIndex: row];
    }
    else
        fTrackers[row] = tracker;

    [fTrackerTable deselectAll: self];
    [fTrackerTable reloadData];
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fTrackerAddRemoveControl setEnabled: [fTrackerTable numberOfSelectedRows] > 0 forSegment: TRACKER_REMOVE_TAG];
}

- (void) copy: (id) sender
{
    NSArray * addresses = [fTrackers objectsAtIndexes: [fTrackerTable selectedRowIndexes]];
    NSString * text = [addresses componentsJoinedByString: @"\n"];

    NSPasteboard * pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb writeObjects: @[text]];
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    const SEL action = [menuItem action];

    if (action == @selector(copy:))
        return [[self window] firstResponder] == fTrackerTable && [fTrackerTable numberOfSelectedRows] > 0;

    if (action == @selector(paste:))
        return [[self window] firstResponder] == fTrackerTable
            && [[NSPasteboard generalPasteboard] canReadObjectForClasses: @[[NSString class]] options: nil];

    return YES;
}

- (void) paste: (id) sender
{
    NSMutableArray * tempTrackers = [NSMutableArray array];

    NSArray * items = [[NSPasteboard generalPasteboard] readObjectsForClasses: @[[NSString class]] options: nil];
    NSAssert(items != nil, @"no string items to paste; should not be able to call this method");

    for (NSString * pbItem in items)
    {
        for (NSString * tracker in [pbItem componentsSeparatedByString: @"\n"])
            [tempTrackers addObject: tracker];
    }

    BOOL added = NO;

    for (__strong NSString * tracker in tempTrackers)
    {
        tracker = [tracker stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]];

        if ([tracker rangeOfString: @"://"].location == NSNotFound)
            tracker = [@"http://" stringByAppendingString: tracker];

        if (tr_urlIsValidTracker([tracker UTF8String]))
        {
            [fTrackers addObject: tracker];
            added = YES;
        }
    }

    if (added)
    {
        [fTrackerTable deselectAll: self];
        [fTrackerTable reloadData];
    }
    else
        NSBeep();
}

@end

@implementation CreatorWindowController (Private)

- (void) updateLocationField
{
    NSString * pathString = [fLocation path];
    [fLocationField setStringValue: [pathString stringByAbbreviatingWithTildeInPath]];
    [fLocationField setToolTip: pathString];
}

+ (NSURL *) chooseFile
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
    return success ? [panel URLs][0] : nil;
}

- (void) createBlankAddressAlertDidEnd: (NSAlert *) alert returnCode: (NSInteger) returnCode contextInfo: (void *) contextInfo
{
    if ([[alert suppressionButton] state] == NSOnState)
    {
        [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningCreatorBlankAddress"]; //set regardless of private/public
        if ([fPrivateCheck state] == NSOnState)
            [[NSUserDefaults standardUserDefaults] setBool: NO forKey: @"WarningCreatorPrivateBlankAddress"];
    }


    if (returnCode == NSAlertFirstButtonReturn)
        [self performSelectorOnMainThread: @selector(createReal) withObject: nil waitUntilDone: NO];
}

- (void) createReal
{
    //check if the location currently exists
    if (![[fLocation URLByDeletingLastPathComponent] checkResourceIsReachableAndReturnError: NULL])
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> directory doesn't exist warning -> button")];
        [alert setMessageText: NSLocalizedString(@"The chosen torrent file location does not exist.",
                                                "Create torrent -> directory doesn't exist warning -> title")];
        [alert setInformativeText: [NSString stringWithFormat:
                NSLocalizedString(@"The directory \"%@\" does not currently exist. "
                    "Create this directory or choose a different one to create the torrent file.",
                    "Create torrent -> directory doesn't exist warning -> warning"),
                    [[fLocation URLByDeletingLastPathComponent] path]]];
        [alert setAlertStyle: NSWarningAlertStyle];

        [alert beginSheetModalForWindow: [self window] modalDelegate: self didEndSelector: nil contextInfo: nil];
        return;
    }

    //check if a file with the same name and location already exists
    if ([fLocation checkResourceIsReachableAndReturnError: NULL])
    {
        NSArray * pathComponents = [fLocation pathComponents];
        NSInteger count = [pathComponents count];

        NSAlert * alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> file already exists warning -> button")];
        [alert setMessageText: NSLocalizedString(@"A torrent file with this name and directory cannot be created.",
                                                "Create torrent -> file already exists warning -> title")];
        [alert setInformativeText: [NSString stringWithFormat:
                NSLocalizedString(@"A file with the name \"%@\" already exists in the directory \"%@\". "
                    "Choose a new name or directory to create the torrent file.",
                    "Create torrent -> file already exists warning -> warning"),
                    pathComponents[count-1], pathComponents[count-2]]];
        [alert setAlertStyle: NSWarningAlertStyle];

        [alert beginSheetModalForWindow: [self window] modalDelegate: self didEndSelector: nil contextInfo: nil];
        return;
    }

    //parse non-empty tracker strings
    tr_tracker_info * trackerInfo = tr_new0(tr_tracker_info, [fTrackers count]);

    for (NSUInteger i = 0; i < [fTrackers count]; i++)
    {
        trackerInfo[i].announce = (char *)[fTrackers[i] UTF8String];
        trackerInfo[i].tier = i;
    }

    //store values
    [fDefaults setObject: fTrackers forKey: @"CreatorTrackers"];
    [fDefaults setBool: [fPrivateCheck state] == NSOnState forKey: @"CreatorPrivate"];
    [fDefaults setBool: [fOpenCheck state] == NSOnState forKey: @"CreatorOpen"];
    fOpenWhenCreated = [fOpenCheck state] == NSOnState; //need this since the check box might not exist, and value in prefs might have changed from another creator window
    [fDefaults setURL: [fLocation URLByDeletingLastPathComponent] forKey: @"CreatorLocationURL"];

    [[self window] setRestorable: NO];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"BeginCreateTorrentFile" object: fLocation userInfo: nil];
    tr_makeMetaInfo(fInfo, [[fLocation path] UTF8String], trackerInfo, [fTrackers count], [[fCommentView string] UTF8String], [fPrivateCheck state] == NSOnState);
    tr_free(trackerInfo);

    fTimer = [NSTimer scheduledTimerWithTimeInterval: 0.1 target: self selector: @selector(checkProgress) userInfo: nil repeats: YES];
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
                if (fOpenWhenCreated)
                {
                    NSDictionary * dict = [[NSDictionary alloc] initWithObjects: @[
                        [fLocation path],
                        [[fPath URLByDeletingLastPathComponent] path]]
                        forKeys: @[@"File", @"Path"]];
                    [[NSNotificationCenter defaultCenter] postNotificationName: @"OpenCreatedTorrentFile" object: self userInfo: dict];
                }

                [[self window] close];
                break;

            case TR_MAKEMETA_CANCELLED:
                [[self window] close];
                break;

            default:
                alert = [[NSAlert alloc] init];
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
                else //invalid url should have been caught before creating
                    [alert setInformativeText: [NSString stringWithFormat: @"%@ (%d)",
                        NSLocalizedString(@"An unknown error has occurred.", "Create torrent -> failed -> warning"), fInfo->result]];

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
            [window setFrameAutosaveName: @""];

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

- (void) failureSheetClosed: (NSAlert *) alert returnCode: (NSInteger) code contextInfo: (void *) info
{
    [[alert window] orderOut: nil];
    [[self window] close];
}

@end
