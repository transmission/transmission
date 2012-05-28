/******************************************************************************
 * $Id$
 *
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

#import <Cocoa/Cocoa.h>
#import "transmission.h"
#import "makemeta.h"

@interface CreatorWindowController : NSWindowController
{
    IBOutlet NSImageView * fIconView;
    IBOutlet NSTextField * fNameField, * fStatusField, * fPiecesField, * fLocationField;
    IBOutlet NSTableView * fTrackerTable;
    IBOutlet NSSegmentedControl * fTrackerAddRemoveControl;
    IBOutlet NSTextView * fCommentView;
    IBOutlet NSButton * fPrivateCheck, * fOpenCheck;
    
    IBOutlet NSView * fProgressView;
    IBOutlet NSProgressIndicator * fProgressIndicator;
    
    tr_metainfo_builder * fInfo;
    NSURL * fPath, * fLocation;
    NSMutableArray * fTrackers;
    
    NSTimer * fTimer;
    BOOL fStarted, fOpenWhenCreated;
    
    NSUserDefaults * fDefaults;
}

+ (CreatorWindowController *) createTorrentFile: (tr_session *) handle;
+ (CreatorWindowController *) createTorrentFile: (tr_session *) handle forFile: (NSURL *) file;

- (id) initWithHandle: (tr_session *) handle path: (NSURL *) path;

- (IBAction) setLocation: (id) sender;
- (IBAction) create: (id) sender;
- (IBAction) cancelCreateWindow: (id) sender;
- (IBAction) cancelCreateProgress: (id) sender;

- (IBAction) addRemoveTracker: (id) sender;

- (void) copy: (id) sender;
- (void) paste: (id) sender;

@end
