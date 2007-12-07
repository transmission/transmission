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

#import "ButtonGroupToolbarItem.h"

@implementation ButtonGroupToolbarItem

- (void) dealloc
{
    [fIdentifiers release];
    [super dealloc];
}

- (void) setIdentifiers: (NSArray *) identifiers
{
    [fIdentifiers release];
    fIdentifiers = [identifiers retain];
}

- (void) validate
{
    NSSegmentedControl * control = (NSSegmentedControl *)[self view];
    
    int i;
    for (i = 0; i < [control segmentCount]; i++)
        [control setEnabled: [[self target] validateToolbarItem:
            [[[NSToolbarItem alloc] initWithItemIdentifier: [fIdentifiers objectAtIndex: i]] autorelease]] forSegment: i];
}

- (void) createMenu: (NSArray *) labels
{
    NSMenuItem * menuItem = [[NSMenuItem alloc] initWithTitle: [self label] action: NULL keyEquivalent: @""];
    NSMenu * menu = [[NSMenu alloc] initWithTitle: [self label]];
    [menuItem setSubmenu: menu];
    
    [menu setAutoenablesItems: NO];
    
    NSMenuItem * addItem;
    int i, count = [(NSSegmentedControl *)[self view] segmentCount];
    for (i = 0; i < count; i++)
    {
        addItem = [[NSMenuItem alloc] initWithTitle: [labels objectAtIndex: i] action: [self action] keyEquivalent: @""];
        [addItem setTarget: [self target]];
        [addItem setTag: i];
        
        [menu addItem: addItem];
        [addItem release];
    }
    
    [menu release];
    [self setMenuFormRepresentation: menuItem];
    [menuItem release];
}

- (NSMenuItem *) menuFormRepresentation
{
    NSMenuItem * menuItem = [super menuFormRepresentation];
    
    int i, count = [(NSSegmentedControl *)[self view] segmentCount];
    for (i = 0; i < count; i++)
        [[[menuItem submenu] itemAtIndex: i] setEnabled: [[self target] validateToolbarItem:
            [[[NSToolbarItem alloc] initWithItemIdentifier: [fIdentifiers objectAtIndex: i]] autorelease]]];
    
    return menuItem;
}

@end
