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
#import "NSStringAdditions.h"
#include "utils.h" //tr_httpParseURL

@interface CreatorWindowController (Private)

+ (NSString *) chooseFile;
- (void) locationSheetClosed: (NSSavePanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
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
        int count = fInfo->fileCount;
        if (count != 1)
            fileString = [NSString stringWithFormat: NSLocalizedString(@"%d Files", "Create torrent -> info"), count];
        else
            fileString = NSLocalizedString(@"1 File", "Create torrent -> info");
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
    NSString * tracker;
    if ((tracker = [fDefaults stringForKey: @"CreatorTracker"]))
        [fTrackerField setStringValue: tracker];
    
    if ([fDefaults objectForKey: @"CreatorPrivate"])
        [fPrivateCheck setState: [fDefaults boolForKey: @"CreatorPrivate"] ? NSOnState : NSOffState];
    
    if ([fDefaults objectForKey: @"CreatorOpen"])
        [fOpenCheck setState: [fDefaults boolForKey: @"CreatorOpen"] ? NSOnState : NSOffState];
}

- (void) dealloc
{
    [fPath release];
    [fLocation release];
    
    if (fInfo)
        tr_metaInfoBuilderFree(fInfo);
    
    if (fTimer)
        [fTimer invalidate];
    
    [super dealloc];
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
    NSString * trackerString = [fTrackerField stringValue];
    if ([trackerString rangeOfString: @"://"].location == NSNotFound)
        trackerString = [@"http://" stringByAppendingString: trackerString];
    
    //parse tracker string
    if (tr_httpParseURL([trackerString UTF8String], -1, NULL, NULL, NULL))
    {
        NSAlert * alert = [[[NSAlert alloc] init] autorelease];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> warning -> button")];
        [alert setMessageText: NSLocalizedString(@"The tracker address cannot be blank.", "Create torrent -> warning -> title")];
        [alert setInformativeText: NSLocalizedString(@"Change the tracker address to create the torrent.",
                                                    "Create torrent -> warning -> info")];
        [alert setAlertStyle: NSWarningAlertStyle];
        
        //check common reasons for failure
        if (![trackerString hasPrefix: @"http://"])
            [alert setMessageText: NSLocalizedString(@"The tracker address must begin with \"http://\".",
                                                    "Create torrent -> warning -> message")];
        else if ([trackerString length] <= 7) //don't allow blank addresses
            [alert setMessageText: NSLocalizedString(@"The tracker address cannot be blank.", "Create torrent -> warning -> message")];
        else
            [alert setMessageText: NSLocalizedString(@"The tracker address is invalid.", "Create torrent -> warning -> message")];
        
        [alert beginSheetModalForWindow: [self window] modalDelegate: self didEndSelector: nil contextInfo: nil];
        return;
    }
    
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
                    "Choose a new name or directory to create the torrent.",
                    "Create torrent -> file already exists warning -> warning"),
                    [pathComponents objectAtIndex: count-1], [pathComponents objectAtIndex: count-2]]];
        [alert setAlertStyle: NSWarningAlertStyle];
        
        [alert beginSheetModalForWindow: [self window] modalDelegate: self didEndSelector: nil contextInfo: nil];
        return;
    }
    
    fOpenTorrent = [fOpenCheck state] == NSOnState;
    
    //store values
    [fDefaults setObject: trackerString forKey: @"CreatorTracker"];
    [fDefaults setBool: [fPrivateCheck state] == NSOnState forKey: @"CreatorPrivate"];
    [fDefaults setBool: fOpenTorrent forKey: @"CreatorOpen"];
    [fDefaults setObject: [fLocation stringByDeletingLastPathComponent] forKey: @"CreatorLocation"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"BeginCreateTorrentFile" object: fLocation userInfo: nil];
    tr_makeMetaInfo(fInfo, [fLocation UTF8String], [trackerString UTF8String], [[fCommentView string] UTF8String],
                    [fPrivateCheck state] == NSOnState);
    
    fTimer = [NSTimer scheduledTimerWithTimeInterval: 0.1 target: self selector: @selector(checkProgress)
                        userInfo: nil repeats: YES];
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
                if (fOpenTorrent)
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
            
            NSWindow * window = [self window];
            
            NSRect windowRect = [window frame];
            float difference = [fProgressView frame].size.height - [[window contentView] frame].size.height;
            windowRect.origin.y -= difference;
            windowRect.size.height += difference;
            
            //don't allow vertical resizing
            float height = windowRect.size.height;
            [window setMinSize: NSMakeSize([window minSize].width, height)];
            [window setMaxSize: NSMakeSize([window maxSize].width, height)];
            
            [window setContentView: fProgressView];
            [window setFrame: windowRect display: YES animate: YES];
            [fProgressView setHidden: NO];
            
            [[window standardWindowButton:NSWindowCloseButton] setEnabled: NO];
        }
    }
}

- (void) failureSheetClosed: (NSAlert *) alert returnCode: (int) code contextInfo: (void *) info
{
    [[alert window] orderOut: nil];
    [[self window] close];
}

@end
