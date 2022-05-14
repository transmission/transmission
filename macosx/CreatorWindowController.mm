// This file Copyright © 2007-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/utils.h>
#include <libtransmission/web-utils.h> // tr_urlIsValidTracker()
#include <cmath>

#import "CreatorWindowController.h"
#import "Controller.h"
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"

#define TRACKER_ADD_TAG 0
#define TRACKER_REMOVE_TAG 1

@interface CreatorWindowController ()

@property(nonatomic) IBOutlet NSImageView* fIconView;
@property(nonatomic) IBOutlet NSTextField* fNameField;
@property(nonatomic) IBOutlet NSTextField* fStatusField;
@property(nonatomic) IBOutlet NSTextField* fPiecesField;
@property(nonatomic) IBOutlet NSTextField* fLocationField;
@property(nonatomic) IBOutlet NSTableView* fTrackerTable;
@property(nonatomic) IBOutlet NSSegmentedControl* fTrackerAddRemoveControl;
@property(nonatomic) IBOutlet NSTextView* fCommentView;
@property(nonatomic) IBOutlet NSButton* fPrivateCheck;
@property(nonatomic) IBOutlet NSButton* fOpenCheck;
@property(nonatomic) IBOutlet NSTextField* fSource;
@property(nonatomic) IBOutlet NSStepper* fPieceSizeStepper;

@property(nonatomic) IBOutlet NSView* fProgressView;
@property(nonatomic) IBOutlet NSProgressIndicator* fProgressIndicator;

@property(nonatomic, readonly) tr_metainfo_builder* fInfo;
@property(nonatomic, readonly) NSURL* fPath;
@property(nonatomic) NSURL* fLocation;
@property(nonatomic) NSMutableArray<NSString*>* fTrackers;

@property(nonatomic) NSTimer* fTimer;
@property(nonatomic) BOOL fStarted;
@property(nonatomic) BOOL fOpenWhenCreated;

@property(nonatomic, readonly) NSUserDefaults* fDefaults;

+ (NSURL*)chooseFile;

- (void)updateLocationField;
- (void)createReal;
- (void)checkProgress;

@end

NSMutableSet* creatorWindowControllerSet = nil;

@implementation CreatorWindowController

+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle
{
    //get file/folder for torrent
    NSURL* path;
    if (!(path = [CreatorWindowController chooseFile]))
    {
        return nil;
    }

    CreatorWindowController* creator = [[self alloc] initWithHandle:handle path:path];
    [creator showWindow:nil];
    return creator;
}

+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle forFile:(NSURL*)file
{
    CreatorWindowController* creator = [[self alloc] initWithHandle:handle path:file];
    [creator showWindow:nil];
    return creator;
}

- (instancetype)initWithHandle:(tr_session*)handle path:(NSURL*)path
{
    if ((self = [super initWithWindowNibName:@"Creator"]))
    {
        if (!creatorWindowControllerSet)
        {
            creatorWindowControllerSet = [NSMutableSet set];
        }

        _fStarted = NO;

        _fPath = path;
        _fInfo = tr_metaInfoBuilderCreate(_fPath.path.UTF8String);

        if (_fInfo->fileCount == 0)
        {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> no files -> button")];
            alert.messageText = NSLocalizedString(@"This folder contains no files.", "Create torrent -> no files -> title");
            alert.informativeText = NSLocalizedString(
                @"There must be at least one file in a folder to create a torrent file.",
                "Create torrent -> no files -> warning");
            alert.alertStyle = NSAlertStyleWarning;

            [alert runModal];

            return nil;
        }
        if (_fInfo->totalSize == 0)
        {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> zero size -> button")];
            alert.messageText = NSLocalizedString(@"The total file size is zero bytes.", "Create torrent -> zero size -> title");
            alert.informativeText = NSLocalizedString(@"A torrent file cannot be created for files with no size.", "Create torrent -> zero size -> warning");
            alert.alertStyle = NSAlertStyleWarning;

            [alert runModal];

            return nil;
        }

        _fDefaults = NSUserDefaults.standardUserDefaults;

        //get list of trackers
        if (!(_fTrackers = [[_fDefaults arrayForKey:@"CreatorTrackers"] mutableCopy]))
        {
            _fTrackers = [[NSMutableArray alloc] init];

            //check for single tracker from versions before 1.3
            NSString* tracker;
            if ((tracker = [_fDefaults stringForKey:@"CreatorTracker"]))
            {
                [_fDefaults removeObjectForKey:@"CreatorTracker"];
                if (![tracker isEqualToString:@""])
                {
                    [_fTrackers addObject:tracker];
                    [_fDefaults setObject:_fTrackers forKey:@"CreatorTrackers"];
                }
            }
        }

        //remove potentially invalid addresses
        for (NSInteger i = _fTrackers.count - 1; i >= 0; i--)
        {
            if (!tr_urlIsValidTracker([_fTrackers[i] UTF8String]))
            {
                [_fTrackers removeObjectAtIndex:i];
            }
        }

        [creatorWindowControllerSet addObject:self];
    }
    return self;
}

- (void)awakeFromNib
{
    self.window.restorationClass = [self class];

    NSString* name = self.fPath.lastPathComponent;

    self.window.title = name;

    self.fNameField.stringValue = name;
    self.fNameField.toolTip = self.fPath.path;

    BOOL const multifile = self.fInfo->isFolder;

    NSImage* icon = [NSWorkspace.sharedWorkspace
        iconForFileType:multifile ? NSFileTypeForHFSTypeCode(kGenericFolderIcon) : self.fPath.pathExtension];
    icon.size = self.fIconView.frame.size;
    self.fIconView.image = icon;

    NSString* statusString = [NSString stringForFileSize:self.fInfo->totalSize];
    if (multifile)
    {
        NSString* fileString;
        NSUInteger count = self.fInfo->fileCount;
        if (count != 1)
        {
            fileString = [NSString stringWithFormat:NSLocalizedString(@"%lu files", "Create torrent -> info"), count];
        }
        else
        {
            fileString = NSLocalizedString(@"1 file", "Create torrent -> info");
        }
        statusString = [NSString stringWithFormat:@"%@, %@", fileString, statusString];
    }
    self.fStatusField.stringValue = statusString;

    [self updatePiecesField];
    [self.fPieceSizeStepper setIntValue:(int)log2((double)self.fInfo->pieceSize)];

    self.fLocation = [[self.fDefaults URLForKey:@"CreatorLocationURL"]
        URLByAppendingPathComponent:[name stringByAppendingPathExtension:@"torrent"]];
    if (!self.fLocation)
    {
        //for 2.5 and earlier
#warning we still store "CreatorLocation" in Defaults.plist, and not "CreatorLocationURL"
        NSString* location = [self.fDefaults stringForKey:@"CreatorLocation"];
        self.fLocation = [[NSURL alloc]
            initFileURLWithPath:[location.stringByExpandingTildeInPath
                                    stringByAppendingPathComponent:[name stringByAppendingPathExtension:@"torrent"]]];
    }
    [self updateLocationField];

    //set previously saved values
    if ([self.fDefaults objectForKey:@"CreatorPrivate"])
    {
        self.fPrivateCheck.state = [self.fDefaults boolForKey:@"CreatorPrivate"] ? NSControlStateValueOn : NSControlStateValueOff;
    }

    if ([self.fDefaults objectForKey:@"CreatorSource"])
    {
        self.fSource.stringValue = [self.fDefaults stringForKey:@"CreatorSource"];
    }

    self.fOpenCheck.state = [self.fDefaults boolForKey:@"CreatorOpen"] ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)dealloc
{
    if (_fInfo)
    {
        tr_metaInfoBuilderFree(_fInfo);
    }

    [_fTimer invalidate];
}

+ (void)restoreWindowWithIdentifier:(NSString*)identifier
                              state:(NSCoder*)state
                  completionHandler:(void (^)(NSWindow*, NSError*))completionHandler
{
    NSURL* path = [state decodeObjectForKey:@"TRCreatorPath"];
    if (!path || ![path checkResourceIsReachableAndReturnError:nil])
    {
        completionHandler(nil, [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotOpenFile userInfo:nil]);
        return;
    }

    NSWindow* window = [self createTorrentFile:((Controller*)NSApp.delegate).sessionHandle forFile:path].window;
    completionHandler(window, nil);
}

- (void)window:(NSWindow*)window willEncodeRestorableState:(NSCoder*)state
{
    [state encodeObject:self.fPath forKey:@"TRCreatorPath"];
    [state encodeObject:self.fLocation forKey:@"TRCreatorLocation"];
    [state encodeObject:self.fTrackers forKey:@"TRCreatorTrackers"];
    [state encodeInteger:self.fOpenCheck.state forKey:@"TRCreatorOpenCheck"];
    [state encodeInteger:self.fPrivateCheck.state forKey:@"TRCreatorPrivateCheck"];
    [state encodeObject:self.fSource.stringValue forKey:@"TRCreatorSource"];
    [state encodeObject:self.fCommentView.string forKey:@"TRCreatorPrivateComment"];
}

- (void)window:(NSWindow*)window didDecodeRestorableState:(NSCoder*)coder
{
    self.fLocation = [coder decodeObjectForKey:@"TRCreatorLocation"];
    [self updateLocationField];

    self.fTrackers = [coder decodeObjectForKey:@"TRCreatorTrackers"];
    [self.fTrackerTable reloadData];

    self.fOpenCheck.state = [coder decodeIntegerForKey:@"TRCreatorOpenCheck"];
    self.fPrivateCheck.state = [coder decodeIntegerForKey:@"TRCreatorPrivateCheck"];
    self.fSource.stringValue = [coder decodeObjectForKey:@"TRCreatorSource"];
    self.fCommentView.string = [coder decodeObjectForKey:@"TRCreatorPrivateComment"];
}

- (IBAction)setLocation:(id)sender
{
    NSSavePanel* panel = [NSSavePanel savePanel];

    panel.prompt = NSLocalizedString(@"Select", "Create torrent -> location sheet -> button");
    panel.message = NSLocalizedString(@"Select the name and location for the torrent file.", "Create torrent -> location sheet -> message");

    panel.allowedFileTypes = @[ @"org.bittorrent.torrent", @"torrent" ];
    panel.canSelectHiddenExtension = YES;

    panel.directoryURL = self.fLocation.URLByDeletingLastPathComponent;
    panel.nameFieldStringValue = self.fLocation.lastPathComponent;

    [panel beginSheetModalForWindow:self.window completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK)
        {
            self.fLocation = panel.URL;
            [self updateLocationField];
        }
    }];
}

- (IBAction)create:(id)sender
{
    //make sure the trackers are no longer being verified
    if (self.fTrackerTable.editedRow != -1)
    {
        [self.window endEditingFor:self.fTrackerTable];
    }

    BOOL const isPrivate = self.fPrivateCheck.state == NSControlStateValueOn;
    if (self.fTrackers.count == 0 && [self.fDefaults boolForKey:isPrivate ? @"WarningCreatorPrivateBlankAddress" : @"WarningCreatorBlankAddress"])
    {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = NSLocalizedString(@"There are no tracker addresses.", "Create torrent -> blank address -> title");

        NSString* infoString = isPrivate ?
            NSLocalizedString(
                @"A transfer marked as private with no tracker addresses will be unable to connect to peers."
                 " The torrent file will only be useful if you plan to upload the file to a tracker website"
                 " that will add the addresses for you.",
                "Create torrent -> blank address -> message") :
            NSLocalizedString(
                @"The transfer will not contact trackers for peers, and will have to rely solely on"
                 " non-tracker peer discovery methods such as PEX and DHT to download and seed.",
                "Create torrent -> blank address -> message");

        alert.informativeText = infoString;
        [alert addButtonWithTitle:NSLocalizedString(@"Create", "Create torrent -> blank address -> button")];
        [alert addButtonWithTitle:NSLocalizedString(@"Cancel", "Create torrent -> blank address -> button")];
        alert.showsSuppressionButton = YES;

        [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
            if (alert.suppressionButton.state == NSControlStateValueOn)
            {
                [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningCreatorBlankAddress"]; //set regardless of private/public
                if (self.fPrivateCheck.state == NSControlStateValueOn)
                {
                    [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"WarningCreatorPrivateBlankAddress"];
                }
            }

            if (returnCode == NSAlertFirstButtonReturn)
            {
                [self performSelectorOnMainThread:@selector(createReal) withObject:nil waitUntilDone:NO];
            }
        }];
    }
    else
    {
        [self createReal];
    }
}

- (IBAction)cancelCreateWindow:(id)sender
{
    [self.window close];
}

- (void)windowWillClose:(NSNotification*)notification
{
    [creatorWindowControllerSet removeObject:self];
}

- (IBAction)cancelCreateProgress:(id)sender
{
    self.fInfo->abortFlag = 1;
    [self.fTimer fire];
}

- (IBAction)incrementOrDecrementPieceSize:(id)sender
{
    uint32_t pieceSize = (uint32_t)pow(2.0, [sender intValue]);
    if (tr_metaInfoBuilderSetPieceSize(self.fInfo, pieceSize))
    {
        [self updatePiecesField];
    }
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.fTrackers.count;
}

- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row
{
    return self.fTrackers[row];
}

- (IBAction)addRemoveTracker:(id)sender
{
    //don't allow add/remove when currently adding - it leads to weird results
    if (self.fTrackerTable.editedRow != -1)
    {
        return;
    }

    if ([[sender cell] tagForSegment:[sender selectedSegment]] == TRACKER_REMOVE_TAG)
    {
        [self.fTrackers removeObjectsAtIndexes:self.fTrackerTable.selectedRowIndexes];

        [self.fTrackerTable deselectAll:self];
        [self.fTrackerTable reloadData];
    }
    else
    {
        [self.fTrackers addObject:@""];
        [self.fTrackerTable reloadData];

        NSInteger const row = self.fTrackers.count - 1;
        [self.fTrackerTable selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        [self.fTrackerTable editColumn:0 row:row withEvent:nil select:YES];
    }
}

- (void)tableView:(NSTableView*)tableView
    setObjectValue:(id)object
    forTableColumn:(NSTableColumn*)tableColumn
               row:(NSInteger)row
{
    NSString* tracker = (NSString*)object;

    tracker = [tracker stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];

    if ([tracker rangeOfString:@"://"].location == NSNotFound)
    {
        tracker = [@"http://" stringByAppendingString:tracker];
    }

    if (!tr_urlIsValidTracker(tracker.UTF8String))
    {
        NSBeep();
        [self.fTrackers removeObjectAtIndex:row];
    }
    else
    {
        self.fTrackers[row] = tracker;
    }

    [self.fTrackerTable deselectAll:self];
    [self.fTrackerTable reloadData];
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification
{
    [self.fTrackerAddRemoveControl setEnabled:self.fTrackerTable.numberOfSelectedRows > 0 forSegment:TRACKER_REMOVE_TAG];
}

- (void)copy:(id)sender
{
    NSArray* addresses = [self.fTrackers objectsAtIndexes:self.fTrackerTable.selectedRowIndexes];
    NSString* text = [addresses componentsJoinedByString:@"\n"];

    NSPasteboard* pb = NSPasteboard.generalPasteboard;
    [pb clearContents];
    [pb writeObjects:@[ text ]];
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL const action = menuItem.action;

    if (action == @selector(copy:))
    {
        return self.window.firstResponder == self.fTrackerTable && self.fTrackerTable.numberOfSelectedRows > 0;
    }

    if (action == @selector(paste:))
    {
        return self.window.firstResponder == self.fTrackerTable &&
            [NSPasteboard.generalPasteboard canReadObjectForClasses:@[ [NSString class] ] options:nil];
    }

    return YES;
}

- (void)paste:(id)sender
{
    NSMutableArray* tempTrackers = [NSMutableArray array];

    NSArray* items = [NSPasteboard.generalPasteboard readObjectsForClasses:@[ [NSString class] ] options:nil];
    NSAssert(items != nil, @"no string items to paste; should not be able to call this method");

    for (NSString* pbItem in items)
    {
        for (NSString* tracker in [pbItem componentsSeparatedByString:@"\n"])
        {
            [tempTrackers addObject:tracker];
        }
    }

    BOOL added = NO;

    for (__strong NSString* tracker in tempTrackers)
    {
        tracker = [tracker stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];

        if ([tracker rangeOfString:@"://"].location == NSNotFound)
        {
            tracker = [@"http://" stringByAppendingString:tracker];
        }

        if (tr_urlIsValidTracker(tracker.UTF8String))
        {
            [self.fTrackers addObject:tracker];
            added = YES;
        }
    }

    if (added)
    {
        [self.fTrackerTable deselectAll:self];
        [self.fTrackerTable reloadData];
    }
    else
    {
        NSBeep();
    }
}

#pragma mark - Private

- (void)updatePiecesField
{
    if (self.fInfo->pieceCount == 1)
    {
        self.fPiecesField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"1 piece, %@", "Create torrent -> info"),
                                                                   [NSString stringForFileSize:self.fInfo->pieceSize]];
    }
    else
    {
        self.fPiecesField.stringValue = [NSString stringWithFormat:NSLocalizedString(@"%u pieces, %@ each", "Create torrent -> info"),
                                                                   self.fInfo->pieceCount,
                                                                   [NSString stringForFileSize:self.fInfo->pieceSize]];
    }
}

- (void)updateLocationField
{
    NSString* pathString = self.fLocation.path;
    self.fLocationField.stringValue = pathString.stringByAbbreviatingWithTildeInPath;
    self.fLocationField.toolTip = pathString;
}

+ (NSURL*)chooseFile
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];

    panel.title = NSLocalizedString(@"Create Torrent File", "Create torrent -> select file");
    panel.prompt = NSLocalizedString(@"Select", "Create torrent -> select file");
    panel.allowsMultipleSelection = NO;
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.canCreateDirectories = NO;

    panel.message = NSLocalizedString(@"Select a file or folder for the torrent file.", "Create torrent -> select file");

    BOOL success = [panel runModal] == NSModalResponseOK;
    return success ? panel.URLs[0] : nil;
}

- (void)createReal
{
    //check if the location currently exists
    if (![self.fLocation.URLByDeletingLastPathComponent checkResourceIsReachableAndReturnError:NULL])
    {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> directory doesn't exist warning -> button")];
        alert.messageText = NSLocalizedString(@"The chosen torrent file location does not exist.", "Create torrent -> directory doesn't exist warning -> title");
        alert.informativeText = [NSString stringWithFormat:NSLocalizedString(
                                                               @"The directory \"%@\" does not currently exist. "
                                                                "Create this directory or choose a different one to create the torrent file.",
                                                               "Create torrent -> directory doesn't exist warning -> warning"),
                                                           self.fLocation.URLByDeletingLastPathComponent.path];
        alert.alertStyle = NSAlertStyleWarning;

        [alert beginSheetModalForWindow:self.window completionHandler:nil];
        return;
    }

    //check if a file with the same name and location already exists
    if ([self.fLocation checkResourceIsReachableAndReturnError:NULL])
    {
        NSArray* pathComponents = self.fLocation.pathComponents;
        NSInteger count = pathComponents.count;

        NSAlert* alert = [[NSAlert alloc] init];
        [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> file already exists warning -> button")];
        alert.messageText = NSLocalizedString(
            @"A torrent file with this name and directory cannot be created.",
            "Create torrent -> file already exists warning -> title");
        alert.informativeText = [NSString stringWithFormat:NSLocalizedString(
                                                               @"A file with the name \"%@\" already exists in the directory \"%@\". "
                                                                "Choose a new name or directory to create the torrent file.",
                                                               "Create torrent -> file already exists warning -> warning"),
                                                           pathComponents[count - 1],
                                                           pathComponents[count - 2]];
        alert.alertStyle = NSAlertStyleWarning;

        [alert beginSheetModalForWindow:self.window completionHandler:nil];
        return;
    }

    //parse non-empty tracker strings
    tr_tracker_info* trackerInfo = tr_new0(tr_tracker_info, self.fTrackers.count);

    for (NSUInteger i = 0; i < self.fTrackers.count; i++)
    {
        trackerInfo[i].announce = (char*)[self.fTrackers[i] UTF8String];
        trackerInfo[i].tier = i;
    }

    //store values
    [self.fDefaults setObject:self.fTrackers forKey:@"CreatorTrackers"];
    [self.fDefaults setBool:self.fPrivateCheck.state == NSControlStateValueOn forKey:@"CreatorPrivate"];
    [self.fDefaults setObject:[self.fSource stringValue] forKey:@"CreatorSource"];
    [self.fDefaults setBool:self.fOpenCheck.state == NSControlStateValueOn forKey:@"CreatorOpen"];
    self.fOpenWhenCreated = self.fOpenCheck.state ==
        NSControlStateValueOn; //need this since the check box might not exist, and value in prefs might have changed from another creator window
    [self.fDefaults setURL:self.fLocation.URLByDeletingLastPathComponent forKey:@"CreatorLocationURL"];

    self.window.restorable = NO;

    [NSNotificationCenter.defaultCenter postNotificationName:@"BeginCreateTorrentFile" object:self.fLocation userInfo:nil];
    tr_makeMetaInfo(
        self.fInfo,
        self.fLocation.path.UTF8String,
        trackerInfo,
        self.fTrackers.count,
        nullptr,
        0,
        self.fCommentView.string.UTF8String,
        self.fPrivateCheck.state == NSControlStateValueOn,
        self.fSource.stringValue.UTF8String);
    tr_free(trackerInfo);

    self.fTimer = [NSTimer scheduledTimerWithTimeInterval:0.1 target:self selector:@selector(checkProgress) userInfo:nil
                                                  repeats:YES];
}

- (void)checkProgress
{
    if (self.fInfo->isDone)
    {
        [self.fTimer invalidate];
        self.fTimer = nil;

        NSAlert* alert;
        switch (self.fInfo->result)
        {
        case TrMakemetaResult::OK:
            if (self.fOpenWhenCreated)
            {
                NSDictionary* dict = @{
                    @"File" : self.fLocation.path,
                    @"Path" : self.fPath.URLByDeletingLastPathComponent.path
                };
                [NSNotificationCenter.defaultCenter postNotificationName:@"OpenCreatedTorrentFile" object:self userInfo:dict];
            }

            [self.window close];
            break;

        case TrMakemetaResult::CANCELLED:
            [self.window close];
            break;

        default:
            alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle:NSLocalizedString(@"OK", "Create torrent -> failed -> button")];
            alert.messageText = [NSString stringWithFormat:NSLocalizedString(@"Creation of \"%@\" failed.", "Create torrent -> failed -> title"),
                                                           self.fLocation.lastPathComponent];
            alert.alertStyle = NSAlertStyleWarning;

            if (self.fInfo->result == TrMakemetaResult::ERR_IO_READ)
            {
                alert.informativeText = [NSString
                    stringWithFormat:NSLocalizedString(@"Could not read \"%s\": %s.", "Create torrent -> failed -> warning"),
                                     self.fInfo->errfile,
                                     strerror(self.fInfo->my_errno)];
            }
            else if (self.fInfo->result == TrMakemetaResult::ERR_IO_WRITE)
            {
                alert.informativeText = [NSString
                    stringWithFormat:NSLocalizedString(@"Could not write \"%s\": %s.", "Create torrent -> failed -> warning"),
                                     self.fInfo->errfile,
                                     strerror(self.fInfo->my_errno)];
            }
            else //invalid url should have been caught before creating
            {
                alert.informativeText = [NSString
                    stringWithFormat:@"%@ (%d)",
                                     NSLocalizedString(@"An unknown error has occurred.", "Create torrent -> failed -> warning"),
                                     self.fInfo->result];
            }

            [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse returnCode) {
                [alert.window orderOut:nil];
                [self.window close];
            }];
        }
    }
    else
    {
        self.fProgressIndicator.doubleValue = (double)self.fInfo->pieceIndex / self.fInfo->pieceCount;

        if (!self.fStarted)
        {
            self.fStarted = YES;

            self.fProgressView.hidden = YES;

            NSWindow* window = self.window;
            window.frameAutosaveName = @"";

            NSRect windowRect = window.frame;
            CGFloat difference = self.fProgressView.frame.size.height - window.contentView.frame.size.height;
            windowRect.origin.y -= difference;
            windowRect.size.height += difference;

            //don't allow vertical resizing
            CGFloat height = windowRect.size.height;
            window.minSize = NSMakeSize(window.minSize.width, height);
            window.maxSize = NSMakeSize(window.maxSize.width, height);

            window.contentView = self.fProgressView;
            [window setFrame:windowRect display:YES animate:YES];
            self.fProgressView.hidden = NO;

            [window standardWindowButton:NSWindowCloseButton].enabled = NO;
        }
    }
}

@end
