/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2010-2012 Transmission authors and contributors
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
#import "Torrent.h"

@class Controller;
@class Torrent;

@interface AddMagnetWindowController : NSWindowController
{
    IBOutlet NSImageView * fLocationImageView;
    IBOutlet NSTextField * fNameField, * fLocationField;
    IBOutlet NSButton * fStartCheck;
    IBOutlet NSPopUpButton * fGroupPopUp, * fPriorityPopUp;
    
    //remove these when switching to auto layout
    IBOutlet NSTextField * fMagnetLinkLabel;
    IBOutlet NSTextField * fDownloadToLabel, * fGroupLabel, * fPriorityLabel;
    IBOutlet NSButton * fChangeDestinationButton;
    IBOutlet NSBox * fDownloadToBox;
    IBOutlet NSButton * fAddButton, * fCancelButton;
    
    Controller * fController;
    
    Torrent * fTorrent;
    NSString * fDestination;
    
    NSInteger fGroupValue;
    TorrentDeterminationType fGroupDeterminationType;
}

- (id) initWithTorrent: (Torrent *) torrent destination: (NSString *) path controller: (Controller *) controller;

- (Torrent *) torrent;

- (void) setDestination: (id) sender;

- (void) add: (id) sender;
- (void) cancelAdd: (id) sender;

- (void) changePriority: (id) sender;

- (void) updateGroupMenu: (NSNotification *) notification;

@end
