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

#import "MessageWindowController.h"
#import "NSStringAdditions.h"
#import <transmission.h>

#define LEVEL_ERROR 0
#define LEVEL_INFO  1
#define LEVEL_DEBUG 2

#define UPDATE_SECONDS  0.6
#define MAX_MESSAGES    4000

@interface MessageWindowController (Private)

- (void) resizeColumn;
- (NSString *) stringForMessage: (NSDictionary *) message;
- (NSString *) fileForMessage: (NSDictionary *) message;

@end

@implementation MessageWindowController

- (id) init
{
    return [super initWithWindowNibName: @"MessageWindow"];
}

- (void) dealloc
{
    [fTimer invalidate];
    [fMessages release];
    
    [fAttributes release];
    
    [super dealloc];
}

- (void) awakeFromNib
{
    NSWindow * window = [self window];
    [window setFrameAutosaveName: @"MessageWindowFrame"];
    [window setFrameUsingName: @"MessageWindowFrame"];
    
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(resizeColumn)
                    name: @"NSTableViewColumnDidResizeNotification" object: fMessageTable];
    
    //initially sort peer table by IP
    if ([[fMessageTable sortDescriptors] count] == 0)
        [fMessageTable setSortDescriptors: [NSArray arrayWithObject: [[fMessageTable tableColumnWithIdentifier: @"Date"]
                                            sortDescriptorPrototype]]];
    
    fErrorImage = [NSImage imageNamed: @"RedDot.png"];
    fInfoImage = [NSImage imageNamed: @"YellowDot.png"];
    fDebugImage = [NSImage imageNamed: @"PurpleDot.png"];
    
    [[self window] setTitle: NSLocalizedString(@"Message Log", "Message window -> title")];
    
    //set images and text for popup button items
    [[fLevelButton itemAtIndex: LEVEL_ERROR] setImage: fErrorImage];
    [[fLevelButton itemAtIndex: LEVEL_ERROR] setTitle: NSLocalizedString(@"Error", "Message window -> level string")];
    [[fLevelButton itemAtIndex: LEVEL_INFO] setImage: fInfoImage];
    [[fLevelButton itemAtIndex: LEVEL_INFO] setTitle: NSLocalizedString(@"Info", "Message window -> level string")];
    [[fLevelButton itemAtIndex: LEVEL_DEBUG] setImage: fDebugImage];
    [[fLevelButton itemAtIndex: LEVEL_DEBUG] setTitle: NSLocalizedString(@"Debug", "Message window -> level string")];
    
    //set table column text
    [[[fMessageTable tableColumnWithIdentifier: @"Date"] headerCell] setTitle: NSLocalizedString(@"Date",
        "Message window -> table column")];
    [[[fMessageTable tableColumnWithIdentifier: @"Name"] headerCell] setTitle: NSLocalizedString(@"Process",
        "Message window -> table column")];
    [[[fMessageTable tableColumnWithIdentifier: @"Message"] headerCell] setTitle: NSLocalizedString(@"Message",
        "Message window -> table column")];
    
    //set and size buttons
    [fSaveButton setTitle: [NSLocalizedString(@"Save", "Message window -> save button") stringByAppendingEllipsis]];
    [fSaveButton sizeToFit];
    
    NSRect saveButtonFrame = [fSaveButton frame];
    saveButtonFrame.size.width += 10.0;
    [fSaveButton setFrame: saveButtonFrame];
    
    float oldClearButtonWidth = [fClearButton frame].size.width;
    
    [fClearButton setTitle: NSLocalizedString(@"Clear", "Message window -> save button")];
    [fClearButton sizeToFit];
    
    NSRect clearButtonFrame = [fClearButton frame];
    clearButtonFrame.size.width = MAX(clearButtonFrame.size.width + 10.0, saveButtonFrame.size.width);
    clearButtonFrame.origin.x -= clearButtonFrame.size.width - oldClearButtonWidth;
    [fClearButton setFrame: clearButtonFrame];
    
    //select proper level in popup button
    switch (tr_getMessageLevel())
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
    
    fMessages = [[NSMutableArray alloc] init];
}

- (void) windowDidBecomeKey: (NSNotification *) notification
{
    if (!fTimer)
        fTimer = [NSTimer scheduledTimerWithTimeInterval: UPDATE_SECONDS target: self
                    selector: @selector(updateLog:) userInfo: nil repeats: YES];
    [self updateLog: nil];
}

- (void) windowWillClose: (id)sender
{
    [fTimer invalidate];
    fTimer = nil;
}

- (void) updateLog: (NSTimer *) timer
{
    tr_msg_list * messages, * currentMessage;
    if ((messages = tr_getQueuedMessages()) == NULL)
        return;
    
    for (currentMessage = messages; currentMessage != NULL; currentMessage = currentMessage->next)
    {
        NSMutableDictionary * message  = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                        [NSString stringWithUTF8String: currentMessage->message], @"Message",
                                        [NSDate dateWithTimeIntervalSince1970: currentMessage->when], @"Date",
                                        [NSNumber numberWithInt: currentMessage->level], @"Level", nil];
        
        if (currentMessage->file != NULL)
        {
            [message setObject: [NSString stringWithUTF8String: currentMessage->file] forKey: @"File"];
            [message setObject: [NSNumber numberWithInt: currentMessage->line] forKey: @"Line"];
        }
        
        if (currentMessage->name != NULL)
            [message setObject: [NSString stringWithUTF8String: currentMessage->name] forKey: @"Name"];
                                
        [fMessages addObject: message];
    }
    
    tr_freeMessageList(messages);
    
    NSScroller * scroller = [[fMessageTable enclosingScrollView] verticalScroller];
    BOOL shouldScroll = [scroller floatValue] == 1.0 || [scroller isHidden] || [scroller knobProportion] == 1.0;
    
    int total = [fMessages count];
    if (total > MAX_MESSAGES)
    {
        //remove the oldest
        NSSortDescriptor * descriptor = [[[NSSortDescriptor alloc] initWithKey: @"Date" ascending: YES] autorelease];
        [fMessages sortUsingDescriptors: [NSArray arrayWithObject: descriptor]];
        
        [fMessages removeObjectsInRange: NSMakeRange(0, total-MAX_MESSAGES)];
        
        [fMessageTable noteHeightOfRowsWithIndexesChanged: [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, MAX_MESSAGES)]];
        total = MAX_MESSAGES;
    }
    
    [fMessages sortUsingDescriptors: [fMessageTable sortDescriptors]];
    
    [fMessageTable reloadData];
    if (shouldScroll)
        [fMessageTable scrollRowToVisible: total-1];
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
    else if ([ident isEqualToString: @"Name"])
        return [message objectForKey: @"Name"];
    else
        return [message objectForKey: @"Message"];
}

#warning don't cut off end
- (float) tableView: (NSTableView *) tableView heightOfRow: (int) row
{
    NSTableColumn * column = [tableView tableColumnWithIdentifier: @"Message"];
    
    if (!fAttributes)
        fAttributes = [[[[column dataCell] attributedStringValue] attributesAtIndex: 0 effectiveRange: NULL] retain];
    
    int count = [[[fMessages objectAtIndex: row] objectForKey: @"Message"] sizeWithAttributes: fAttributes].width / [column width];
    return [tableView rowHeight] * (float)(count+1);
}

- (void) tableView: (NSTableView *) tableView sortDescriptorsDidChange: (NSArray *) oldDescriptors
{
    [fMessages sortUsingDescriptors: [fMessageTable sortDescriptors]];
    [fMessageTable reloadData];
}

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column row: (int) row mouseLocation: (NSPoint) mouseLocation
{
    return [self fileForMessage: [fMessages objectAtIndex: row]];
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

- (void) resizeColumn
{
    [fMessageTable noteHeightOfRowsWithIndexesChanged: [NSIndexSet indexSetWithIndexesInRange:
                    NSMakeRange(0, [fMessageTable numberOfRows])]];
}

- (NSString *) stringForMessage: (NSDictionary *) message
{
    NSString * level;
    switch ([[message objectForKey: @"Level"] intValue])
    {
        case TR_MSG_ERR:
            level = @"[Error]";
            break;
        case TR_MSG_INF:
            level = @"[Info]";
            break;
        case TR_MSG_DBG:
            level = @"[Debug]";
            break;
        default:
            level = @"";
    }
    
    NSMutableArray * strings = [NSMutableArray arrayWithObjects: [message objectForKey: @"Date"], level,
                                [message objectForKey: @"Message"], nil];
    
    NSString * torrent;
    if ((torrent = [message objectForKey: @"Name"]))
        [strings insertObject: [torrent stringByAppendingString: @":"] atIndex: 2];
    
    NSString * file;
    if ((file = [self fileForMessage: message]))
        [strings insertObject: file atIndex: 1];
    
    return [strings componentsJoinedByString: @" "];
}

- (NSString *) fileForMessage: (NSDictionary *) message
{
    NSString * file;
    if ((file = [message objectForKey: @"File"]))
        return [NSString stringWithFormat: @"%@:%@", [[message objectForKey: @"File"] lastPathComponent],
                [message objectForKey: @"Line"]];
    else
        return nil;
}

@end
