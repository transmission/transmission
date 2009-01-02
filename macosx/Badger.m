/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

#import "Badger.h"
#import "BadgeView.h"

@implementation Badger

- (id) initWithLib: (tr_session *) lib
{
    if ((self = [super init]))
    {
        fLib = lib;
        
        fCompleted = 0;
        
        BadgeView * view = [[BadgeView alloc] initWithFrame: [[[NSApp dockTile] contentView] frame] lib: lib];
        [[NSApp dockTile] setContentView: view];
        [view release];
        
        //change that just impacts the dock badge
        [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(updateBadge) name: @"DockBadgeChange" object: nil];
    }
    
    return self;
}

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    [super dealloc];
}

- (void) updateBadge
{
    float downloadRate = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeDownloadRate"]
                            ? tr_sessionGetPieceSpeed(fLib, TR_DOWN) : 0.0;
    float uploadRate = [[NSUserDefaults standardUserDefaults] boolForKey: @"BadgeUploadRate"]
                        ? tr_sessionGetPieceSpeed(fLib, TR_UP) : 0.0;
    
    [(BadgeView *)[[NSApp dockTile] contentView] displayRatesWithDownload: downloadRate upload: uploadRate];
}

- (void) incrementCompleted
{
    fCompleted++;
    [[NSApp dockTile] setBadgeLabel: [NSString stringWithFormat: @"%d", fCompleted]];
}

- (void) clearCompleted
{
    if (fCompleted != 0)
    {
        fCompleted = 0;
        [[NSApp dockTile] setBadgeLabel: @""];
    }
}

- (void) setQuitting
{
    [self clearCompleted];
    [(BadgeView *)[[NSApp dockTile] contentView] setQuitting];
    [[NSApp dockTile] display];
}

@end
