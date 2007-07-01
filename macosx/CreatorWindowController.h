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

#import <Cocoa/Cocoa.h>
#import "transmission.h"
#import "makemeta.h"

@interface CreatorWindowController : NSWindowController
{
    IBOutlet NSImageView * fIconView;
    IBOutlet NSTextField * fNameField, * fStatusField, * fPiecesField, * fTrackerField, * fLocationField;
    IBOutlet NSTextView * fCommentView;
    IBOutlet NSButton * fPrivateCheck, * fOpenCheck;
    
    IBOutlet NSView * fProgressView;
    IBOutlet NSProgressIndicator * fProgressIndicator;
    
    tr_metainfo_builder_t * fInfo;
    NSString * fPath, * fLocation;
    BOOL fOpenTorrent;
    
    NSTimer * fTimer;
    BOOL fStarted;
    
    NSUserDefaults * fDefaults;
}

+ (void) createTorrentFile: (tr_handle_t *) handle;
+ (void) createTorrentFile: (tr_handle_t *) handle forFile: (NSString *) file;

- (id) initWithWindowNibName: (NSString *) name handle: (tr_handle_t *) handle path: (NSString *) path;

- (void) setLocation: (id) sender;
- (void) create: (id) sender;
- (void) cancelCreateWindow: (id) sender;
- (void) cancelCreateProgress: (id) sender;

@end
