/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2008-2009 Transmission authors and contributors
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

@interface QuickLookController (Private)

- (id) initWithMainController: (Controller *) controller infoController: (InfoWindowController *) infoController;

@end

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

//QuickLook delegate method
//returns the frame for the item represented by the URL, or an empty frame to fade in/out instead
- (NSRect) previewPanel: (NSPanel *) panel frameForURL: (NSURL *) url
{
    if ([fInfoController shouldQuickLookFileView])
        return [fInfoController quickLookFrameWithURL: url];
    else
        return [fMainController quickLookFrameWithURL: url];
}

- (BOOL) quickLookSelectItems
{
    if (!fQuickLookAvailable)
        return NO;
    
    NSArray * urlArray;
    if ([fInfoController shouldQuickLookFileView])
        urlArray = [fInfoController quickLookURLs];
    else
        urlArray = [fMainController quickLookURLs];
    
    if (urlArray && [urlArray count] > 0)
    {
        [[QLPreviewPanel sharedPreviewPanel] setURLs: urlArray];
        return YES;
    }
    else
        return NO;
}

- (BOOL) canQuickLook
{
    if (!fQuickLookAvailable)
        return NO;
    
    if ([fInfoController shouldQuickLookFileView])
        return [fInfoController canQuickLook];
    else
        return [fMainController canQuickLook];
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
            NSWindow * keyWindow = [NSApp keyWindow];
            
            [[QLPreviewPanel sharedPreviewPanel] makeKeyAndOrderFrontWithEffect: 2];
            
            //restore the focus to previous key window
            [keyWindow makeKeyWindow];
        }
    }
}

- (void) updateQuickLook
{
    //only update when window is open or in the middle of opening (visible)
    if (!fQuickLookAvailable || !([[QLPreviewPanel sharedPreviewPanel] isOpen] || [[QLPreviewPanel sharedPreviewPanel] isVisible]))
        return;
    
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

@implementation QuickLookController (Private)

- (id) initWithMainController: (Controller *) controller infoController: (InfoWindowController *) infoController
{
    if ((self = [super init]))
    {
        fMainController = controller;
        fInfoController = infoController;
        
        //load the QuickLook framework and set the delegate
        //animation types: 0 = none; 1 = fade; 2 = zoom
        fQuickLookAvailable = [[NSBundle bundleWithPath: @"/System/Library/PrivateFrameworks/QuickLookUI.framework"] load];
        if (fQuickLookAvailable)
            [[[QLPreviewPanel sharedPreviewPanel] windowController] setDelegate: self];
    }
    
    return self;
}

@end
