/******************************************************************************
 * $Id$
 * 
 * Copyright (c) 2012 Transmission authors and contributors
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

#import "WebSeedTableView.h"

@implementation WebSeedTableView

- (void) mouseDown: (NSEvent *) event
{
    [[self window] makeKeyWindow];
    [super mouseDown: event];
}

- (void) setWebSeeds: (NSArray *) webSeeds
{
    fWebSeeds = webSeeds;
}

- (void) copy: (id) sender
{
    NSIndexSet * indexes = [self selectedRowIndexes];
    NSMutableArray * addresses = [NSMutableArray arrayWithCapacity: [indexes count]];
    [fWebSeeds enumerateObjectsAtIndexes: indexes options: 0 usingBlock: ^(NSDictionary * webSeed, NSUInteger idx, BOOL * stop) {
        [addresses addObject: [webSeed objectForKey: @"Address"]];
    }];
    
    NSString * text = [addresses componentsJoinedByString: @"\n"];
    
    NSPasteboard * pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    [pb writeObjects: [NSArray arrayWithObject: text]];
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    const SEL action = [menuItem action];
    
    if (action == @selector(copy:))
        return [self numberOfSelectedRows] > 0;
    
    return YES;
}

@end
