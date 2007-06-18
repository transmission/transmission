/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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
#import "StringAdditions.h"

#define DEFAULT_SAVE_LOCATION @"~/Desktop/"

@interface CreatorWindowController (Private)

+ (NSString *) chooseFile;
- (void) locationSheetClosed: (NSSavePanel *) openPanel returnCode: (int) code contextInfo: (void *) info;
- (void) checkProgress;
- (void) failureSheetClosed: (NSAlert *) alert returnCode: (int) code contextInfo: (void *) info;

@end

@implementation CreatorWindowController

+ (void) createTorrentFile: (tr_handle_t *) handle
{
    //get file/folder for torrent
    NSString * path;
    if (!(path = [CreatorWindowController chooseFile]))
        return;
    
    CreatorWindowController * creator = [[self alloc] initWithWindowNibName: @"Creator" handle: handle path: path];
    [creator showWindow: nil];
}

+ (void) createTorrentFile: (tr_handle_t *) handle forFile: (NSString *) file
{
    CreatorWindowController * creator = [[self alloc] initWithWindowNibName: @"Creator" handle: handle path: file];
    [creator showWindow: nil];
}

- (id) initWithWindowNibName: (NSString *) name handle: (tr_handle_t *) handle path: (NSString *) path
{
    if ((self = [super initWithWindowNibName: name]))
    {
        fStarted = NO;
        
        fPath = [path retain];
        fInfo = tr_metaInfoBuilderCreate(handle, [fPath UTF8String]);
        if (fInfo->fileCount == 0)
        {
            NSAlert * alert = [[[NSAlert alloc] init] autorelease];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> no files -> button")];
            [alert setMessageText: NSLocalizedString(@"This folder contains no files.",
                                                    "Create torrent -> no files -> title")];
            [alert setInformativeText: NSLocalizedString(@"There must be at least one file in a folder to create a torrent file.",
                                                        "Create torrent -> no files -> warning")];
            [alert setAlertStyle: NSWarningAlertStyle];
            
            [alert runModal];
            
            [self release];
            return nil;
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
    
    #warning fix when resizing window
    [fIcon setImage: [[NSWorkspace sharedWorkspace] iconForFileType: multifile
                        ? NSFileTypeForHFSTypeCode('fldr') : [fPath pathExtension]]];
    
    NSString * statusString = [NSString stringForFileSize: fInfo->totalSize];
    if (multifile)
    {
        NSString * fileString;
        int count = fInfo->fileCount;
        if (count != 1)
            fileString = [NSString stringWithFormat: NSLocalizedString(@"%d Files, ", "Create torrent -> info"), count];
        else
            fileString = NSLocalizedString(@"1 File, ", "Create torrent -> info");
        statusString = [fileString stringByAppendingString: statusString];
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
    
    fLocation = [[[DEFAULT_SAVE_LOCATION stringByAppendingPathComponent: [name stringByAppendingPathExtension: @"torrent"]]
                                            stringByExpandingTildeInPath] retain];
    [fLocationField setStringValue: [fLocation stringByAbbreviatingWithTildeInPath]];
    [fLocationField setToolTip: fLocation];
}

- (void) dealloc
{
    [fPath release];
    if (fLocation)
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

    [panel setPrompt: @"Select"];
    [panel setRequiredFileType: @"torrent"];
    [panel setCanSelectHiddenExtension: YES];

    [panel beginSheetForDirectory: nil file: [fLocation lastPathComponent] modalForWindow: [self window] modalDelegate: self
            didEndSelector: @selector(locationSheetClosed:returnCode:contextInfo:) contextInfo: nil];
}

- (void) create: (id) sender
{
    //parse tracker string
    NSString * trackerString = [fTrackerField stringValue];
    if ([trackerString rangeOfString: @"://"].location != NSNotFound)
    {
        if (![trackerString hasPrefix: @"http://"])
        {
            NSAlert * alert = [[[NSAlert alloc] init] autorelease];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> http warning -> button")];
            [alert setMessageText: NSLocalizedString(@"The tracker address must begin with \"http://\".",
                                                    "Create torrent -> http warning -> title")];
            [alert setInformativeText: NSLocalizedString(@"Change the tracker address to create the torrent.",
                                                        "Create torrent -> http warning -> warning")];
            [alert setAlertStyle: NSWarningAlertStyle];
            
            [alert beginSheetModalForWindow: [self window] modalDelegate: self didEndSelector: nil contextInfo: nil];
            return;
        }
    }
    else
        trackerString = [@"http://" stringByAppendingString: trackerString];
    
    //don't allow blank addresses
    if ([trackerString length] <= 7)
    {
        NSAlert * alert = [[[NSAlert alloc] init] autorelease];
        [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> no url warning -> button")];
        [alert setMessageText: NSLocalizedString(@"The tracker address cannot be blank.",
                                                "Create torrent -> no url warning -> title")];
        [alert setInformativeText: NSLocalizedString(@"Change the tracker address to create the torrent.",
                                                    "Create torrent -> no url warning -> warning")];
        [alert setAlertStyle: NSWarningAlertStyle];
        
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
        
        if (fInfo->failed)
        {
            if (!fInfo->abortFlag)
            {
                NSAlert * alert = [[[NSAlert alloc] init] autorelease];
                [alert addButtonWithTitle: NSLocalizedString(@"OK", "Create torrent -> failed -> button")];
                [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"Creation of \"%@\" failed.",
                                                "Create torrent -> failed -> title"), [fLocation lastPathComponent]]];
                [alert setInformativeText: NSLocalizedString(@"There was an error parsing the data file. "
                                            "The torrent file was not created.", "Create torrent -> failed -> warning")];
                [alert setAlertStyle: NSWarningAlertStyle];
                
                [alert beginSheetModalForWindow: [self window] modalDelegate: self
                        didEndSelector: @selector(failureSheetClosed:returnCode:contextInfo:) contextInfo: nil];
                return;
            }
        }
        else
        {
            if (fOpenTorrent)
            {
                NSDictionary * dict = [[NSDictionary alloc] initWithObjectsAndKeys: fLocation, @"File",
                                        [fPath stringByDeletingLastPathComponent], @"Path", nil];
                [[NSNotificationCenter defaultCenter] postNotificationName: @"OpenCreatedTorrentFile" object: self userInfo: dict];
            }
        }
        
        [[self window] close];
    }
    else
    {
        [fProgressIndicator setDoubleValue: (double)fInfo->pieceIndex / fInfo->pieceCount];
        
        if (!fStarted)
        {
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
            
            fStarted = YES;
        }
    }
}

- (void) failureSheetClosed: (NSAlert *) alert returnCode: (int) code contextInfo: (void *) info
{
    [[alert window] orderOut: nil];
    [[self window] close];
}

@end
