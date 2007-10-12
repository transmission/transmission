/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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

#import "MessageWindowController.h"
#import <transmission.h>

#define LEVEL_ERROR 0
#define LEVEL_INFO  1
#define LEVEL_DEBUG 2

#define UPDATE_SECONDS  0.6
#define MAX_MESSAGES    4000

@interface MessageWindowController (Private)

- (NSString *) stringForMessage: (NSDictionary *) message;
- (void) setDebugWarningHidden: (BOOL) hide;

@end

@implementation MessageWindowController

- (id) init
{
    if ((self = [super initWithWindowNibName: @"MessageWindow"]))
    {
        fMessages = [[NSMutableArray alloc] init];
        
        fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_SECONDS target: self
                    selector: @selector(updateLog:) userInfo: nil repeats: YES];
        
        tr_setMessageLevel([[NSUserDefaults standardUserDefaults] integerForKey: @"MessageLevel"]);
        tr_setMessageQueuing(1);
    }
    return self;
}

- (void) dealloc
{
    [fTimer invalidate];
    [fMessages release];
    
    [super dealloc];
}

- (void) awakeFromNib
{
    NSWindow * window = [self window];
    [window setFrameAutosaveName: @"MessageWindowFrame"];
    [window setFrameUsingName: @"MessageWindowFrame"];
    
    //initially sort peer table by IP
    if ([[fMessageTable sortDescriptors] count] == 0)
    {
        [fMessageTable setSortDescriptors: [NSArray arrayWithObject: [[fMessageTable tableColumnWithIdentifier: @"Date"]
                                            sortDescriptorPrototype]]];
        [self updateLog: nil];
    }
    
    fErrorImage = [NSImage imageNamed: @"RedDot.tiff"];
    fInfoImage = [NSImage imageNamed: @"YellowDot.tiff"];
    fDebugImage = [NSImage imageNamed: @"PurpleDot.png"];
    
    //set images to popup button items
    [[fLevelButton itemAtIndex: LEVEL_ERROR] setImage: fErrorImage];
    [[fLevelButton itemAtIndex: LEVEL_INFO] setImage: fInfoImage];
    [[fLevelButton itemAtIndex: LEVEL_DEBUG] setImage: fDebugImage];
    
    //select proper level in popup button
    int level = tr_getMessageLevel();
    switch (level)
    {
        case TR_MSG_ERR:
            [fLevelButton selectItemAtIndex: LEVEL_ERROR];
            break;
        case TR_MSG_INF:
            [fLevelButton selectItemAtIndex: LEVEL_INFO];
            break;
        case TR_MSG_DBG:
            [fLevelButton selectItemAtIndex: LEVEL_DEBUG];
    }
    
    [self setDebugWarningHidden: level != TR_MSG_DBG];
}

- (void) updateLog: (NSTimer *) timer
{
    tr_msg_list * messages, * currentMessage;
    if ((messages = tr_getQueuedMessages()) == NULL)
        return;
    
    for (currentMessage = messages; currentMessage != NULL; currentMessage = currentMessage->next)
        [fMessages addObject: [NSDictionary dictionaryWithObjectsAndKeys:
                                [NSString stringWithUTF8String: currentMessage->message], @"Message",
                                [NSDate dateWithTimeIntervalSince1970: currentMessage->when], @"Date",
                                [NSNumber numberWithInt: currentMessage->level], @"Level", nil]];
    
    tr_freeMessageList(messages);
    
    int total = [fMessages count];
    if (total > MAX_MESSAGES)
    {
        //remove the oldest
        NSSortDescriptor * descriptor = [[[NSSortDescriptor alloc] initWithKey: @"Date" ascending: YES] autorelease];
        NSArray * descriptors = [[NSArray alloc] initWithObjects: descriptor, nil];
        [fMessages sortUsingDescriptors: descriptors];
        [descriptors release];
        
        [fMessages removeObjectsInRange: NSMakeRange(0, total-MAX_MESSAGES)];
    }
    
    [fMessages sortUsingDescriptors: [fMessageTable sortDescriptors]];
    
    [fMessageTable reloadData];
}

- (int) numberOfRowsInTableView: (NSTableView *) tableView
{
    return [fMessages count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) column row: (int) row
{
    NSString * ident = [column identifier];
    NSDictionary * message = [fMessages objectAtIndex: row];

    if ([ident isEqualToString: @"Date"])
        return [message objectForKey: @"Date"];
    else if ([ident isEqualToString: @"Level"])
    {
        switch ([[message objectForKey: @"Level"] intValue])
        {
            case TR_MSG_ERR:
                return fErrorImage;
            case TR_MSG_INF:
                return fInfoImage;
            case TR_MSG_DBG:
                return fDebugImage;
            default:
                return nil;
        }
    }
    else
        return [message objectForKey: @"Message"];
}

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column row: (int) row mouseLocation: (NSPoint) mouseLocation
{
    return [self stringForMessage: [fMessages objectAtIndex: row]];
}

- (void) tableView: (NSTableView *) tableView sortDescriptorsDidChange: (NSArray *) oldDescriptors
{
    [fMessages sortUsingDescriptors: [fMessageTable sortDescriptors]];
    [fMessageTable reloadData];
}

- (void) copy: (id) sender
{
    NSPasteboard * pb = [NSPasteboard generalPasteboard];
    [pb declareTypes: [NSArray arrayWithObject: NSStringPboardType] owner: self];
    
    NSIndexSet * indexes = [fMessageTable selectedRowIndexes];
    NSMutableArray * messageStrings = [NSMutableArray arrayWithCapacity: [indexes count]];
    
    NSEnumerator * enumerator = [[fMessages objectsAtIndexes: indexes] objectEnumerator];
    NSDictionary * message;
    while ((message = [enumerator nextObject]))
        [messageStrings addObject: [self stringForMessage: message]];
    
    [pb setString: [messageStrings componentsJoinedByString: @"\n"] forType: NSStringPboardType];
}

- (BOOL) validateMenuItem: (NSMenuItem *) menuItem
{
    SEL action = [menuItem action];
    
    if (action == @selector(copy:))
        return [fMessageTable numberOfSelectedRows] > 0;
    
    return YES;
}

- (void) changeLevel: (id) sender
{
    [self updateLog: nil];
    
    int level;
    switch ([fLevelButton indexOfSelectedItem])
    {
        case LEVEL_ERROR:
            level = TR_MSG_ERR;
            break;
        case LEVEL_INFO:
            level = TR_MSG_INF;
            break;
        case LEVEL_DEBUG:
            level = TR_MSG_DBG;
            break;
    }
    
    [self setDebugWarningHidden: level != TR_MSG_DBG];
    
    tr_setMessageLevel(level);
    [[NSUserDefaults standardUserDefaults] setInteger: level forKey: @"MessageLevel"];
}

- (void) clearLog: (id) sender
{
    [fMessages removeAllObjects];
    [fMessageTable reloadData];
}

- (void) writeToFile: (id) sender
{
    //make the array sorted by date
    NSSortDescriptor * descriptor = [[[NSSortDescriptor alloc] initWithKey: @"Date" ascending: YES] autorelease];
    NSArray * descriptors = [[NSArray alloc] initWithObjects: descriptor, nil];
    NSArray * sortedMessages = [fMessages sortedArrayUsingDescriptors: descriptors];
    [descriptors release];
    
    //create the text to output
    NSMutableArray * messageStrings = [NSMutableArray arrayWithCapacity: [fMessages count]];
    NSEnumerator * enumerator = [sortedMessages objectEnumerator];
    NSDictionary * message;
    while ((message = [enumerator nextObject]))
        [messageStrings addObject: [self stringForMessage: message]];
    
    NSString * fileString = [[messageStrings componentsJoinedByString: @"\n"] retain];
    
    NSSavePanel * panel = [NSSavePanel savePanel];
    [panel setRequiredFileType: @"txt"];
    [panel setCanSelectHiddenExtension: YES];
    
    [panel beginSheetForDirectory: nil file: NSLocalizedString(@"untitled", "Save log panel -> default file name")
            modalForWindow: [self window] modalDelegate: self
            didEndSelector: @selector(writeToFileSheetClosed:returnCode:contextInfo:) contextInfo: fileString];
}

- (void) writeToFileSheetClosed: (NSSavePanel *) panel returnCode: (int) code contextInfo: (NSString *) string
{
    if (code == NSOKButton)
    {
        if (![string writeToFile: [panel filename] atomically: YES encoding: NSUTF8StringEncoding error: nil])
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Save log alert panel -> button")];
            [alert setMessageText: [NSString stringWithFormat: NSLocalizedString(@"Log Could Not Be Saved",
                                    "Save log alert panel -> title")]];
            [alert setInformativeText: [NSString stringWithFormat: 
                    NSLocalizedString(@"There was a problem creating the file \"%@\".",
                                        "Save log alert panel -> message"), [[panel filename] lastPathComponent]]];
            [alert setAlertStyle: NSWarningAlertStyle];
            
            [alert runModal];
            [alert release];
        }
    }
    
    [string release];
}

@end

@implementation MessageWindowController (Private)

- (NSString *) stringForMessage: (NSDictionary *) message
{
    NSString * level;
    switch ([[message objectForKey: @"Level"] intValue])
    {
        case TR_MSG_ERR:
            level = @"Error";
            break;
        case TR_MSG_INF:
            level = @"Info";
            break;
        case TR_MSG_DBG:
            level = @"Debug";
            break;
        default:
            level = @"";
    }
    
    return [NSString stringWithFormat: @"%@ [%@] %@", [message objectForKey: @"Date"], level, [message objectForKey: @"Message"]];
}

- (void) setDebugWarningHidden: (BOOL) hide
{
    [fDebugWarningField setHidden: hide];
    [fDebugWarningIcon setHidden: hide];
}

@end
