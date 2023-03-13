// This file Copyright © 2007-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#include <libtransmission/transmission.h>

@interface CreatorWindowController : NSWindowController

+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle;
+ (CreatorWindowController*)createTorrentFile:(tr_session*)handle forFile:(NSURL*)file;

- (instancetype)initWithHandle:(tr_session*)handle path:(NSURL*)path;

- (IBAction)setLocation:(id)sender;
- (IBAction)create:(id)sender;
- (IBAction)cancelCreateWindow:(id)sender;
- (IBAction)cancelCreateProgress:(id)sender;
- (IBAction)incrementOrDecrementPieceSize:(id)sender;
- (IBAction)addRemoveTracker:(id)sender;

- (void)copy:(id)sender;
- (void)paste:(id)sender;

@end
