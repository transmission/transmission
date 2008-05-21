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

#import "QuickLookController.h"
#import "QuickLook.h"
#define QLPreviewPanel NSClassFromString(@"QLPreviewPanel")

#import "Controller.h"
#import "InfoWindowController.h"

@implementation QuickLookController

QuickLookController * fQuickLookInstance = nil;
+ (void) quickLookControllerInitializeWithController: (Controller *) controller infoController: (InfoWindowController *) infoController
{
    if (!fQuickLookInstance)
        fQuickLookInstance = [[QuickLookController alloc] initWithMainController: controller infoController: infoController];
}

+ (QuickLookController *) quickLook
{
    return fQuickLookInstance;
}

- (id) initWithMainController: (Controller *) controller infoController: (InfoWindowController *) infoController
{
    if ((self = [super init]))
    {
        fMainController = controller;
        fInfoController = infoController;
        
        //load the QuickLook framework and set the delegate, no point on trying this on Tiger
        //animation types: 0 = none; 1 = fade; 2 = zoom
        fQuickLookAvailable = [[NSBundle bundleWithPath: @"/System/Library/PrivateFrameworks/QuickLookUI.framework"] load];
        if (fQuickLookAvailable)
            [[[QLPreviewPanel sharedPreviewPanel] windowController] setDelegate: self];
    }
    
    return self;
}

// This is the QuickLook delegate method
// It should return the frame for the item represented by the URL
// If an empty frame is returned then the panel will fade in/out instead
- (NSRect) previewPanel: (NSPanel *) panel frameForURL: (NSURL *) url
{
    if ([fInfoController shouldQuickLookFileView])
        return [fInfoController quickLookFrameWithURL: url];
    else
    {
        /*NSRect frame = [fImageView frame];
        frame.origin = [[self window] convertBaseToScreen: frame.origin];
        return frame;*/
    }
    
    return NSZeroRect;
}

- (BOOL) quickLookSelectItems
{
    if (!fQuickLookAvailable)
        return NO;
    
    NSArray * urlArray = nil;
    
    if ([fInfoController shouldQuickLookFileView])
        urlArray = [fInfoController quickLookURLs];
    else
    {
        /*if ([fTorrents count] > 0)
        {
            urlArray = [NSMutableArray arrayWithCapacity: [fTorrents count]];
            NSEnumerator * enumerator = [fTorrents objectEnumerator];
            Torrent * torrent;
            while ((torrent = [enumerator nextObject]))
            {
                if ([torrent folder] || [torrent progress] == 1.0)
                    [urlArray addObject: [NSURL fileURLWithPath: [torrent dataLocation]]];
            }
        }*/
    }
    
    if (urlArray && [urlArray count] > 0)
    {
        [[QLPreviewPanel sharedPreviewPanel] setURLs: urlArray currentIndex: 0 preservingDisplayState: YES];
        return YES;
    }
    else
        return NO;
}

- (void) toggleQuickLook
{
    if (!fQuickLookAvailable)
        return;
    
    if ([[QLPreviewPanel sharedPreviewPanel] isOpen])
        [[QLPreviewPanel sharedPreviewPanel] closeWithEffect: 2];
    else
    {
        if ([self quickLookSelectItems])
        {
            [[QLPreviewPanel sharedPreviewPanel] makeKeyAndOrderFrontWithEffect: 2];
            // Restore the focus to our window to demo the selection changing, scrolling (left/right)
            // and closing (space) functionality
            //[[self window] makeKeyWindow];
        }
    }
}

- (void) updateQuickLook
{
    if (!fQuickLookAvailable || ![[QLPreviewPanel sharedPreviewPanel] isOpen])
        return;
    
    //if the user changes the selection while the panel is open then update the current items
    if (![self quickLookSelectItems])
        [[QLPreviewPanel sharedPreviewPanel] closeWithEffect: 1];
}

- (void) pressLeft
{
    if (fQuickLookAvailable && [[QLPreviewPanel sharedPreviewPanel] isOpen])
        [[QLPreviewPanel sharedPreviewPanel] selectPreviousItem];
}

- (void) pressRight
{
    if (fQuickLookAvailable && [[QLPreviewPanel sharedPreviewPanel] isOpen])
        [[QLPreviewPanel sharedPreviewPanel] selectNextItem];
}

@end
