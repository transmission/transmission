// This file Copyright © 2007-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#include <libtransmission/transmission.h>
#include <libtransmission/makemeta.h>

@interface CreatorWindowController : NSWindowController
{
    IBOutlet NSImageView* fIconView;
    IBOutlet NSTextField* fNameField;
    IBOutlet NSTextField* fStatusField;
    IBOutlet NSTextField* fPiecesField;
    IBOutlet NSTextField* fLocationField;
    IBOutlet NSTableView* fTrackerTable;
    IBOutlet NSSegmentedControl* fTrackerAddRemoveControl;
    IBOutlet NSTextView* fCommentView;
    IBOutlet NSButton* fPrivateCheck;
    IBOutlet NSButton* fOpenCheck;
    IBOutlet NSTextField* fSource;

    IBOutlet NSView* fProgressView;
    IBOutlet NSProgressIndicator* fProgressIndicator;

    tr_metainfo_builder* fInfo;
    NSURL* fPath;
    NSURL* fLocation;
    NSMutableArray* fTrackers;

    NSTimer* fTimer;
    BOOL fStarted;
    BOOL fOpenWhenCreated;

    NSUserDefaults* fDefaults;
}

+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle;
+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle forFile:(NSURL*)file;

- (instancetype)initWithHandle:(tr_session*)handle path:(NSURL*)path;

- (IBAction)setLocation:(id)sender;
- (IBAction)create:(id)sender;
- (IBAction)cancelCreateWindow:(id)sender;
- (IBAction)cancelCreateProgress:(id)sender;

- (IBAction)addRemoveTracker:(id)sender;

- (void)copy:(id)sender;
- (void)paste:(id)sender;

@end
