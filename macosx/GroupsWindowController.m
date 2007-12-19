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

#import "GroupsWindowController.h"
#import "GradientCell.h"
#import "CTGradient.h"
#import "NSBezierPathAdditions.h"
#import "NSApplicationAdditions.h"

#define GROUP_TABLE_VIEW_DATA_TYPE @"GroupTableViewDataType"

typedef enum
{
    ADD_TAG = 0,
    REMOVE_TAG = 1
} controlTag;

@interface GroupsWindowController (Private)

- (void) saveGroups;

- (CTGradient *) gradientForColor: (NSColor *) color;
- (void) changeColor: (id) sender;

@end

@implementation GroupsWindowController

GroupsWindowController * fGroupsWindowInstance = nil;
+ (GroupsWindowController *) groupsController
{
    if (!fGroupsWindowInstance)
        fGroupsWindowInstance = [[GroupsWindowController alloc] init];
    return fGroupsWindowInstance;
}

- (id) init
{
    if ((self = [super initWithWindowNibName: @"GroupsWindow"]))
    {
        NSData * data;
        if ((data = [[NSUserDefaults standardUserDefaults] dataForKey: @"Groups"]))
            fGroups = [[NSUnarchiver unarchiveObjectWithData: data] retain];
        else
        {
            //default groups
            NSMutableDictionary * red = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor redColor], @"Color",
                                            NSLocalizedString(@"Red", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 0], @"Index", nil];
            
            NSMutableDictionary * orange = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor orangeColor], @"Color",
                                            NSLocalizedString(@"Orange", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 1], @"Index", nil];
            
            NSMutableDictionary * yellow = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor yellowColor], @"Color",
                                            NSLocalizedString(@"Yellow", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 2], @"Index", nil];
            
            NSMutableDictionary * green = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor greenColor], @"Color",
                                            NSLocalizedString(@"Green", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 3], @"Index", nil];
            
            NSMutableDictionary * blue = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor blueColor], @"Color",
                                            NSLocalizedString(@"Blue", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 4], @"Index", nil];
            
            NSMutableDictionary * purple = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor purpleColor], @"Color",
                                            NSLocalizedString(@"Purple", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 5], @"Index", nil];
            
            NSMutableDictionary * gray = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor grayColor], @"Color",
                                            NSLocalizedString(@"Gray", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInt: 6], @"Index", nil];
            
            fGroups = [[NSMutableArray alloc] initWithObjects: red, orange, yellow, green, blue, purple, gray, nil];
            [self saveGroups]; //make sure this is saved right away
        }
    }
    
    return self;
}

- (void) awakeFromNib
{
    GradientCell * cell = [[GradientCell alloc] init];
    [[fTableView tableColumnWithIdentifier: @"Color"] setDataCell: cell];
    [cell release];
    
    [fTableView registerForDraggedTypes: [NSArray arrayWithObject: GROUP_TABLE_VIEW_DATA_TYPE]];
    
    if ([NSApp isOnLeopardOrBetter])
        [[self window] setContentBorderThickness: [[fTableView enclosingScrollView] frame].origin.y forEdge: NSMinYEdge];
    else
    {
        [fAddRemoveControl setLabel: @"+" forSegment: 0];
        [fAddRemoveControl setLabel: @"-" forSegment: 1];
    }
    
    [fAddRemoveControl setEnabled: NO forSegment: REMOVE_TAG];
}

- (void) dealloc
{
    [fGroups release];
    [super dealloc];
}

- (CTGradient *) gradientForIndex: (int) index
{
    if (index < 0)
        return nil;
    
    NSEnumerator * enumerator = [fGroups objectEnumerator];
    NSDictionary * dict;
    while ((dict = [enumerator nextObject]))
        if ([[dict objectForKey: @"Index"] intValue] == index)
            return [self gradientForColor: [dict objectForKey: @"Color"]];
    
    return nil;
}

- (int) orderValueForIndex: (int) index
{
    if (index != -1)
    {
        int i;
        for (i = 0; i < [fGroups count]; i++)
            if (index == [[[fGroups objectAtIndex: i] objectForKey: @"Index"] intValue])
                return i;
    }
    return -1;
}

- (NSInteger) numberOfRowsInTableView: (NSTableView *) tableview
{
    return [fGroups count];
}

- (id) tableView: (NSTableView *) tableView objectValueForTableColumn: (NSTableColumn *) tableColumn row: (NSInteger) row
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Color"])
        return [self gradientForColor: [[fGroups objectAtIndex: row] objectForKey: @"Color"]];
    else
        return [[fGroups objectAtIndex: row] objectForKey: @"Name"];
}

- (void) tableView: (NSTableView *) tableView setObjectValue: (id) object forTableColumn: (NSTableColumn *) tableColumn
    row: (NSInteger) row
{
    NSString * identifier = [tableColumn identifier];
    if ([identifier isEqualToString: @"Name"])
    {
        [[fGroups objectAtIndex: row] setObject: object forKey: @"Name"];
        [self saveGroups];
    }
    else if ([identifier isEqualToString: @"Button"])
    {
        fCurrentColorDict = [fGroups objectAtIndex: row];
        
        NSColorPanel * colorPanel = [NSColorPanel sharedColorPanel];
        [colorPanel setContinuous: YES];
        [colorPanel setColor: [[fGroups objectAtIndex: row] objectForKey: @"Color"]];
        
        [colorPanel setTarget: self];
        [colorPanel setAction: @selector(changeColor:)];
        
        [colorPanel orderFront: self];
    }
    else;
}

- (void) tableViewSelectionDidChange: (NSNotification *) notification
{
    [fAddRemoveControl setEnabled: [fTableView numberOfSelectedRows] > 0 forSegment: REMOVE_TAG];
}

- (BOOL) tableView: (NSTableView *) tableView writeRowsWithIndexes: (NSIndexSet *) rowIndexes toPasteboard: (NSPasteboard *) pboard
{
    [pboard declareTypes: [NSArray arrayWithObject: GROUP_TABLE_VIEW_DATA_TYPE] owner: self];
    [pboard setData: [NSKeyedArchiver archivedDataWithRootObject: rowIndexes] forType: GROUP_TABLE_VIEW_DATA_TYPE];
    return YES;
}

- (NSDragOperation) tableView: (NSTableView *) tableView validateDrop: (id <NSDraggingInfo>) info
    proposedRow: (int) row proposedDropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: GROUP_TABLE_VIEW_DATA_TYPE])
    {
        [fTableView setDropRow: row dropOperation: NSTableViewDropAbove];
        return NSDragOperationGeneric;
    }
    
    return NSDragOperationNone;
}

- (BOOL) tableView: (NSTableView *) t acceptDrop: (id <NSDraggingInfo>) info
    row: (int) newRow dropOperation: (NSTableViewDropOperation) operation
{
    NSPasteboard * pasteboard = [info draggingPasteboard];
    if ([[pasteboard types] containsObject: GROUP_TABLE_VIEW_DATA_TYPE])
    {
        NSIndexSet * indexes = [NSKeyedUnarchiver unarchiveObjectWithData: [pasteboard dataForType: GROUP_TABLE_VIEW_DATA_TYPE]];
        
        NSArray * selectedGroups = [fGroups objectsAtIndexes: [fTableView selectedRowIndexes]];
        
        //determine where to move them
        int i;
        for (i = [indexes firstIndex]; i < newRow && i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
            newRow--;
        
        //remove objects to reinsert
        NSArray * movingGroups = [[fGroups objectsAtIndexes: indexes] retain];
        [fGroups removeObjectsAtIndexes: indexes];
        
        //insert objects at new location
        for (i = 0; i < [movingGroups count]; i++)
            [fGroups insertObject: [movingGroups objectAtIndex: i] atIndex: newRow + i];
        
        [movingGroups release];
        
        if ([selectedGroups count] > 0)
        {
            NSEnumerator * enumerator = [selectedGroups objectEnumerator];
            NSMutableIndexSet * indexSet = [[NSMutableIndexSet alloc] init];
            NSDictionary * dict;
            while ((dict = [enumerator nextObject]))
                [indexSet addIndex: [fGroups indexOfObject: dict]];
            
            [fTableView selectRowIndexes: indexSet byExtendingSelection: NO];
            [indexSet release];
        }
        
        [fTableView reloadData];
        
        [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: self];
    }
    
    return YES;
}

- (void) addRemoveGroup: (id) sender
{
    NSEnumerator * enumerator;
    NSDictionary * dict;
    int index;
    BOOL found;
    NSIndexSet * rowIndexes;
    NSMutableIndexSet * indexes;
    
    switch ([[sender cell] tagForSegment: [sender selectedSegment]])
    {
        case ADD_TAG:
            
            //find the lowest index
            for (index = 0; index < [fGroups count]; index++)
            {
                found = NO;
                enumerator = [fGroups objectEnumerator];
                while ((dict = [enumerator nextObject]))
                    if ([[dict objectForKey: @"Index"] intValue] == index)
                    {
                        found = YES;
                        break;
                    }
                
                if (!found)
                    break;
            }
            
            [fGroups addObject: [NSMutableDictionary dictionaryWithObjectsAndKeys: [NSNumber numberWithInt: index], @"Index",
                                    [NSColor cyanColor], @"Color", @"", @"Name", nil]];
            [fTableView reloadData];
            [fTableView deselectAll: self];
            
            [fTableView editColumn: [fTableView columnWithIdentifier: @"Name"] row: [fTableView numberOfRows]-1 withEvent: nil
                        select: NO];
            break;
        
        case REMOVE_TAG:
            
            rowIndexes = [fTableView selectedRowIndexes];
            indexes = [NSMutableIndexSet indexSet];
            for (index = [rowIndexes firstIndex]; index != NSNotFound; index = [rowIndexes indexGreaterThanIndex: index])
                [indexes addIndex: [[[fGroups objectAtIndex: index] objectForKey: @"Index"] intValue]];
            
            [fGroups removeObjectsAtIndexes: rowIndexes];
            [fTableView deselectAll: self];
            [fTableView reloadData];
            
            [[NSNotificationCenter defaultCenter] postNotificationName: @"GroupValueRemoved" object: self userInfo:
                [NSDictionary dictionaryWithObject: indexes forKey: @"Indexes"]];
            break;
        
        default:
            return;
    }
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: self];
    [self saveGroups];
}

- (NSMenu *) groupMenuWithTarget: (id) target action: (SEL) action
{
    NSMenu * menu = [[NSMenu alloc] initWithTitle: @"Groups"];
    
    NSMenuItem * item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"None", "Groups -> Menu") action: action
                            keyEquivalent: @""];
    [item setTarget: target];
    [item setTag: -1];
    [menu addItem: item];
    [item release];
    
    NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: NSMakeRect(0.0, 0.0, 16.0, 16.0) radius: 4.0];
    
    NSEnumerator * enumerator = [fGroups objectEnumerator];
    NSDictionary * dict;
    while ((dict = [enumerator nextObject]))
    {
        item = [[NSMenuItem alloc] initWithTitle: [dict objectForKey: @"Name"] action: action keyEquivalent: @""];
        [item setTarget: target];
        
        NSImage * icon = [[NSImage alloc] initWithSize: [bp bounds].size];
        
        [icon lockFocus];
        [[self gradientForColor: [dict objectForKey: @"Color"]] fillBezierPath: bp angle: 270.0];
        [icon unlockFocus];
        
        [item setImage: icon];
        [icon release];
        
        [item setTag: [[dict objectForKey: @"Index"] intValue]];
        
        [menu addItem: item];
        [item release];
    }
    
    return [menu autorelease];
}

@end

@implementation GroupsWindowController (Private)

- (void) saveGroups
{
    [[NSUserDefaults standardUserDefaults] setObject: [NSArchiver archivedDataWithRootObject: fGroups] forKey: @"Groups"];
}

- (CTGradient *) gradientForColor: (NSColor *) color
{
    return [CTGradient gradientWithBeginningColor: [color blendedColorWithFraction: 0.65 ofColor: [NSColor whiteColor]]
            endingColor: [color blendedColorWithFraction: 0.2 ofColor: [NSColor whiteColor]]];
}

- (void) changeColor: (id) sender
{
    [fCurrentColorDict setObject: [sender color] forKey: @"Color"];
    [fTableView reloadData];
    
    [self saveGroups];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateUI" object: self];
}

@end
