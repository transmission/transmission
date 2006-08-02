/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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

#import "MenuButton.h"

@implementation MenuButton

- (void) mouseDown: (NSEvent *) event
{
    NSImage * image = [self image];
    [self setImage: [self alternateImage]];

    //approximate height of menu...alright for now
    /*float menuHeight = ((float)[[self menu] numberOfItems] + 2.0) * [NSMenuView menuBarHeight];
    BOOL moveToTop = menuHeight > [[self window] frame].origin.y;*/

    NSPoint point = NSMakePoint(3.0, /*moveToTop ? [self frame].size.height + menuHeight :*/ -2.0);

    NSEvent * newEvent= [NSEvent mouseEventWithType: [event type]
                            location: point
                            modifierFlags: [event modifierFlags]
                            timestamp: [event timestamp]
                            windowNumber: [event windowNumber]
                            context: [event context]
                            eventNumber: [event eventNumber]
                            clickCount: [event clickCount]
                            pressure: [event pressure]];

    [NSMenu popUpContextMenu: [self menu] withEvent: newEvent forView: self];

    [self setImage: image];
}

- (NSMenu *) menuForEvent: (NSEvent *) event
{
    return nil;
}

@end
