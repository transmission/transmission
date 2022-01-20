// This file Copyright (c) 2010-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

#import "Torrent.h"

@class Controller;
@class Torrent;

@interface AddMagnetWindowController : NSWindowController
{
    IBOutlet NSImageView* fLocationImageView;
    IBOutlet NSTextField* fNameField;
    IBOutlet NSTextField* fLocationField;
    IBOutlet NSButton* fStartCheck;
    IBOutlet NSPopUpButton* fGroupPopUp;
    IBOutlet NSPopUpButton* fPriorityPopUp;

    //remove these when switching to auto layout
    IBOutlet NSTextField* fMagnetLinkLabel;
    IBOutlet NSTextField* fDownloadToLabel;
    IBOutlet NSTextField* fGroupLabel;
    IBOutlet NSTextField* fPriorityLabel;
    IBOutlet NSButton* fChangeDestinationButton;
    IBOutlet NSBox* fDownloadToBox;
    IBOutlet NSButton* fAddButton;
    IBOutlet NSButton* fCancelButton;

    Controller* fController;

    Torrent* fTorrent;
    NSString* fDestination;

    NSInteger fGroupValue;
    TorrentDeterminationType fGroupDeterminationType;
}

- (instancetype)initWithTorrent:(Torrent*)torrent destination:(NSString*)path controller:(Controller*)controller;

@property(nonatomic, readonly) Torrent* torrent;

- (void)setDestination:(id)sender;

- (void)add:(id)sender;
- (void)cancelAdd:(id)sender;

- (void)changePriority:(id)sender;

- (void)updateGroupMenu:(NSNotification*)notification;

@end
