/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2009 Transmission authors and contributors
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
#import "NSApplicationAdditions.h"
#import "NSStringAdditions.h"
#import <transmission.h>
#import <utils.h>

#define LEVEL_ERROR 0
#define LEVEL_INFO  1
#define LEVEL_DEBUG 2

#define UPDATE_SECONDS  0.75

@interface MessageWindowController (Private)

- (void) resizeColumn;
- (NSString *) stringForMessage: (NSDictionary *) message;

@end

@implementation MessageWindowController

- (id) init
{
    return [super initWithWindowNibName: @"MessageWindow"];
}

- (void) dealloc
{
    [fTimer invalidate];
    [fLock release];
    
    [fMessages release];
    [fDisplayedMessages release];
    
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
    
    [window setContentBorderThickness: NSMinY([[fMessageTable enclosingScrollView] frame]) forEdge: NSMinYEdge];
    
    //initially sort peer table by date
    if ([[fMessageTable sortDescriptors] count] == 0)
        [fMessageTable setSortDescriptors: [NSArray arrayWithObject: [[fMessageTable tableColumnWithIdentifier: @"Date"]
                                            sortDescriptorPrototype]]];
    
    [[self window] setTitle: NSLocalizedString(@"Message Log", "Message window -> title")];
    
    //set images and text for popup button items
    [[fLevelButton itemAtIndex: LEVEL_ERROR] setTitle: NSLocalizedString(@"Error", "Message window -> level string")];
    [[fLevelButton itemAtIndex: LEVEL_INFO] setTitle: NSLocalizedString(@"Info", "Message window -> level string")];
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
    
    const CGFloat oldClearButtonWidth = [fClearButton frame].size.width;
    
    [fClearButton setTitle: NSLocalizedString(@"Clear", "Message window -> save button")];
    [fClearButton sizeToFit];
    
    NSRect clearButtonFrame = [fClearButton frame];
    clearButtonFrame.size.width = MAX(clearButtonFrame.size.width + 10.0, saveButtonFrame.size.width);
    clearButtonFrame.origin.x -= clearButtonFrame.size.width - oldClearButtonWidth;
    [fClearButton setFrame: clearButtonFrame];
    
    //select proper level in popup button
    switch ([[NSUserDefaults standardUserDefaults] integerForKey: @"MessageLevel"])
    {
        case TR_MSG_ERR:
            [fLevelButton selectItemAtIndex: LEVEL_ERROR];
            break;
        case TR_MSG_INF:
            [fLevelButton selectItemAtIndex: LEVEL_INFO];
            break;
        case TR_MSG_DBG:
            [fLevelButton selectItemAtIndex: LEVEL_DEBUG];
            break;
        default: //safety
            [[NSUserDefaults standardUserDefaults] setInteger: TR_MSG_ERR forKey: @"MessageLevel"];
            [fLevelButton selectItemAtIndex: LEVEL_ERROR];
    }
    
    fMessages = [[NSMutableArray alloc] init];
    fDisplayedMessages = [[NSMutableArray alloc] init];
    
    fLock = [[NSLock alloc] init];
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
    tr_msg_list * messages;
    if ((messages = tr_getQueuedMessages()) == NULL)
        return;
    
    static NSUInteger currentIndex = 0;
    
    [fLock lock];
    
    NSScroller * scroller = [[fMessageTable enclosingScrollView] verticalScroller];
    const BOOL shouldScroll = currentIndex == 0 || [scroller floatValue] == 1.0 || [scroller isHidden]
                                || [scroller knobProportion] == 1.0;
    
    const NSInteger maxLevel = [[NSUserDefaults standardUserDefaults] integerForKey: @"MessageLevel"];
    BOOL changed = NO;
    
    for (tr_msg_list * currentMessage = messages; currentMessage != NULL; currentMessage = currentMessage->next)
    {
        NSString * name = currentMessage->name != NULL ? [NSString stringWithUTF8String: currentMessage->name]
                            : [[NSProcessInfo processInfo] processName];
        
        NSString * file = [[[NSString stringWithUTF8String: currentMessage->file] lastPathComponent] stringByAppendingFormat: @":%d",
                            currentMessage->line];
        
        NSDictionary * message  = [NSDictionary dictionaryWithObjectsAndKeys:
                                    [NSString stringWithUTF8String: currentMessage->message], @"Message",
                                    [NSDate dateWithTimeIntervalSince1970: currentMessage->when], @"Date",
                                    [NSNumber numberWithUnsignedInteger: currentIndex++], @"Index", //more accurate when sorting by date
                                    [NSNumber numberWithInteger: currentMessage->level], @"Level",
                                    name, @"Name",
                                    file, @"File", nil];
        
        [fMessages addObject: message];
        
        if (currentMessage->level <= maxLevel)
        {
            [fDisplayedMessages addObject: message];
            changed = YES;
        }
    }
    
    if ([fMessages count] > TR_MAX_MSG_LOG)
    {
        NSIndexSet * removeIndexes = [NSIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fMessages count]-TR_MAX_MSG_LOG)];
        NSArray * itemsToRemove = [fMessages objectsAtIndexes: removeIndexes];
        
        [fMessages removeObjectsAtIndexes: removeIndexes];
        [fDisplayedMessages removeObjectsInArray: itemsToRemove];
        changed = YES;
    }
    
    if (changed)
    {
        [fDisplayedMessages sortUsingDescriptors: [fMessageTable sortDescriptors]];
        
        [fMessageTable reloadData];
        if (shouldScroll)
            [fMessageTable scrollRowToVisible: [fMessageTable numberOfRows]-1];
    }
    
    [fLock unlock];
    
    tr_freeMessageList(messages);
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableView
{
    return [fDisplayedMessages count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) column row: (NSInteger) row
{
    NSString * ident = [column identifier];
    NSDictionary * message = [fDisplayedMessages objectAtIndex: row];

    if ([ident isEqualToString: @"Date"])
        return [message objectForKey: @"Date"];
    else if ([ident isEqualToString: @"Level"])
    {
        const NSInteger level = [[message objectForKey: @"Level"] integerValue];
        switch (level)
        {
            case TR_MSG_ERR:
                return [NSImage imageNamed: @"RedDot.png"];
            case TR_MSG_INF:
                return [NSImage imageNamed: @"YellowDot.png"];
            case TR_MSG_DBG:
                return [NSImage imageNamed: @"PurpleDot.png"];
            default:
                NSAssert1(NO, @"Unknown message log level: %d", level);
                return nil;
        }
    }
    else if ([ident isEqualToString: @"Name"])
        return [message objectForKey: @"Name"];
    else
        return [message objectForKey: @"Message"];
}

#warning don't cut off end
- (CGFloat) tableView: (NSTableView *) tableView heightOfRow: (NSInteger) row
{
    NSTableColumn * column = [tableView tableColumnWithIdentifier: @"Message"];
    
    if (!fAttributes)
        fAttributes = [[[[column dataCell] attributedStringValue] attributesAtIndex: 0 effectiveRange: NULL] retain];
    
    NSString * message = [[fDisplayedMessages objectAtIndex: row] objectForKey: @"Message"];
    const CGFloat count = floorf([message sizeWithAttributes: fAttributes].width / [column width]);
    return [tableView rowHeight] * (count + 1.0);
}

- (void) tableView: (NSTableView *) tableView sortDescriptorsDidChange: (NSArray *) oldDescriptors
{
    [fDisplayedMessages sortUsingDescriptors: [fMessageTable sortDescriptors]];
    [fMessageTable reloadData];
}

- (NSString *) tableView: (NSTableView *) tableView toolTipForCell: (NSCell *) cell rect: (NSRectPointer) rect
                tableColumn: (NSTableColumn *) column row: (NSInteger) row mouseLocation: (NSPoint) mouseLocation
{
    NSDictionary * message = [fDisplayedMessages objectAtIndex: row];
    return [message objectForKey: @"File"];
}

- (void) copy: (id) sender
{
    NSIndexSet * indexes = [fMessageTable selectedRowIndexes];
    NSMutableArray * messageStrings = [NSMutableArray arrayWithCapacity: [indexes count]];
    
    for (NSDictionary * message in [fDisplayedMessages objectsAtIndexes: indexes])
        [messageStrings addObject: [self stringForMessage: message]];
    
    NSString * messageString = [messageStrings componentsJoinedByString: @"\n"];
    
    NSPasteboard * pb = [NSPasteboard generalPasteboard];
    if ([NSApp isOnSnowLeopardOrBetter])
    {
        [pb clearContents];
        [pb writeObjects: [NSArray arrayWithObject: messageString]];
    }
    else
    {
        [pb declareTypes: [NSArray arrayWithObject: NSStringPboardType] owner: nil];
        [pb setString: messageString forType: NSStringPboardType];
    }
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
    NSInteger level;
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
        default:
            NSAssert1(NO, @"Unknown message log level: %d", [fLevelButton indexOfSelectedItem]);
    }
    
    if ([[NSUserDefaults standardUserDefaults] integerForKey: @"MessageLevel"] == level)
        return;
    
    [fLock lock];
    
    [[NSUserDefaults standardUserDefaults] setInteger: level forKey: @"MessageLevel"];
    
    if (level == TR_MSG_DBG) //all messages at this level
        [fDisplayedMessages setArray: fMessages];
    else
    {
        [fDisplayedMessages removeAllObjects];
        for (NSDictionary * message in fMessages)
            if ([[message objectForKey: @"Level"] integerValue] <= level)
                [fDisplayedMessages addObject: message];
    }
    
    [fDisplayedMessages sortUsingDescriptors: [fMessageTable sortDescriptors]];
    
    [fMessageTable reloadData];
    
    if ([fDisplayedMessages count] > 0)
    {
        [fMessageTable deselectAll: self];
        [fMessageTable scrollRowToVisible: [fMessageTable numberOfRows]-1];
    }
    
    [fLock unlock];
}

- (void) clearLog: (id) sender
{
    [fLock lock];
    
    [fMessages removeAllObjects];
    [fDisplayedMessages removeAllObjects];
    [fMessageTable reloadData];
    
    [fLock unlock];
}

- (void) writeToFile: (id) sender
{
    //make the array sorted by date
    NSSortDescriptor * descriptor = [[[NSSortDescriptor alloc] initWithKey: @"Index" ascending: YES] autorelease];
    NSArray * descriptors = [[NSArray alloc] initWithObjects: descriptor, nil];
    NSArray * sortedMessages = [[fDisplayedMessages sortedArrayUsingDescriptors: descriptors] retain];
    [descriptors release];
    
    NSSavePanel * panel = [NSSavePanel savePanel];
    [panel setRequiredFileType: @"txt"];
    [panel setCanSelectHiddenExtension: YES];
    
    [panel beginSheetForDirectory: nil file: NSLocalizedString(@"untitled", "Save log panel -> default file name")
            modalForWindow: [self window] modalDelegate: self
            didEndSelector: @selector(writeToFileSheetClosed:returnCode:contextInfo:) contextInfo: sortedMessages];
}

- (void) writeToFileSheetClosed: (NSSavePanel *) panel returnCode: (NSInteger) code contextInfo: (NSArray *) messages
{
    if (code == NSOKButton)
    {
        //create the text to output
        NSMutableArray * messageStrings = [NSMutableArray arrayWithCapacity: [messages count]];
        for (NSDictionary * message in messages)
            [messageStrings addObject: [self stringForMessage: message]];
    
        NSString * fileString = [messageStrings componentsJoinedByString: @"\n"];
        
        if (![fileString writeToFile: [panel filename] atomically: YES encoding: NSUTF8StringEncoding error: nil])
        {
            NSAlert * alert = [[NSAlert alloc] init];
            [alert addButtonWithTitle: NSLocalizedString(@"OK", "Save log alert panel -> button")];
            [alert setMessageText: NSLocalizedString(@"Log Could Not Be Saved", "Save log alert panel -> title")];
            [alert setInformativeText: [NSString stringWithFormat:
                    NSLocalizedString(@"There was a problem creating the file \"%@\".",
                    "Save log alert panel -> message"), [[panel filename] lastPathComponent]]];
            [alert setAlertStyle: NSWarningAlertStyle];
            
            [alert runModal];
            [alert release];
        }
    }
    
    [messages release];
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
    NSString * levelString;
    const NSInteger level = [[message objectForKey: @"Level"] integerValue];
    switch (level)
    {
        case TR_MSG_ERR:
            levelString = NSLocalizedString(@"Error", "Message window -> level");
            break;
        case TR_MSG_INF:
            levelString = NSLocalizedString(@"Info", "Message window -> level");
            break;
        case TR_MSG_DBG:
            levelString = NSLocalizedString(@"Debug", "Message window -> level");
            break;
        default:
            NSAssert1(NO, @"Unknown message log level: %d", level);
    }
    
    return [NSString stringWithFormat: @"%@ %@ [%@] %@: %@", [message objectForKey: @"Date"],
            [message objectForKey: @"File"], levelString,
            [message objectForKey: @"Name"], [message objectForKey: @"Message"], nil];
}

@end
