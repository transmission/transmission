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

#import "GroupsController.h"
#import "CTGradient.h"
#import "NSBezierPathAdditions.h"

#define ICON_WIDTH 16.0
#define ICON_WIDTH_SMALL 12.0

@interface GroupsController (Private)

- (void) saveGroups;

- (CTGradient *) gradientForColor: (NSColor *) color;
- (NSImage *) imageForGroup: (NSDictionary *) dict isSmall: (BOOL) small;

@end

@implementation GroupsController

GroupsController * fGroupsInstance = nil;
+ (GroupsController *) groups
{
    if (!fGroupsInstance)
        fGroupsInstance = [[GroupsController alloc] init];
    return fGroupsInstance;
}

- (id) init
{
    if ((self = [super init]))
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

- (void) dealloc
{
    [fGroups release];
    [super dealloc];
}

- (int) numberOfGroups
{
    return [fGroups count];
}

- (int) rowValueForIndex: (int) index
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

- (int) indexForRow: (int) row
{
    return [[[fGroups objectAtIndex: row] objectForKey: @"Index"] intValue];
}

- (CTGradient *) gradientForIndex: (int) index
{
    int orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [self gradientForColor: [[fGroups objectAtIndex: orderIndex] objectForKey: @"Color"]] : nil;
}

- (NSString *) nameForIndex: (int) index
{
    int orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [self nameForRowIndex: orderIndex] : nil;
}

- (NSString *) nameForRowIndex: (int) index
{
    return [[fGroups objectAtIndex: index] objectForKey: @"Name"];
}

- (void) setName: (NSString *) name forRowIndex: (int) row
{
    [[fGroups objectAtIndex: row] setObject: name forKey: @"Name"];
    [self saveGroups];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (NSImage *) imageForIndex: (int) index isSmall: (BOOL) small
{
    int orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [self imageForRowIndex: orderIndex isSmall: small] : nil;
}

- (NSImage *) imageForRowIndex: (int) row isSmall: (BOOL) small
{
    return [self imageForGroup: [fGroups objectAtIndex: row] isSmall: small];
}

- (NSColor *) colorForIndex: (int) index
{
    int orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [[fGroups objectAtIndex: orderIndex] objectForKey: @"Color"] : nil;
}

- (NSColor *) setColor: (NSColor *) color forIndex: (int) index
{
    [[fGroups objectAtIndex: [self rowValueForIndex: index]] setObject: color forKey: @"Color"];
    
    [[GroupsController groups] saveGroups];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (void) addGroupWithName: (NSString *) name color: (NSColor *) color
{
    //find the lowest index
    int index;
    for (index = 0; index < [fGroups count]; index++)
    {
        BOOL found = NO;
        NSEnumerator * enumerator = [fGroups objectEnumerator];
        NSDictionary * dict;
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
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
    [self saveGroups];
}

- (void) removeGroupWithRowIndexes: (NSIndexSet *) rowIndexes
{
    NSMutableIndexSet * indexes = [NSMutableIndexSet indexSet];
    int index;
    for (index = [rowIndexes firstIndex]; index != NSNotFound; index = [rowIndexes indexGreaterThanIndex: index])
        [indexes addIndex: [[[fGroups objectAtIndex: index] objectForKey: @"Index"] intValue]];
    
    [fGroups removeObjectsAtIndexes: rowIndexes];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"GroupValueRemoved" object: self userInfo:
        [NSDictionary dictionaryWithObject: indexes forKey: @"Indexes"]];
    
    if ([indexes containsIndex: [[NSUserDefaults standardUserDefaults] integerForKey: @"FilterGroup"]])
        [[NSUserDefaults standardUserDefaults] setInteger: -2 forKey: @"FilterGroup"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
    [self saveGroups];
}

- (NSIndexSet *) moveGroupsAtIndexes: (NSIndexSet *) indexes toRow: (int) newRow oldSelected: (NSIndexSet *) selectedIndexes
{
    NSArray * selectedGroups = [fGroups objectsAtIndexes: selectedIndexes];
    
    //determine where to move them
    int i, originalRow = newRow;
    for (i = [indexes firstIndex]; i < originalRow && i != NSNotFound; i = [indexes indexGreaterThanIndex: i])
        newRow--;
    
    //remove objects to reinsert
    NSArray * movingGroups = [[fGroups objectsAtIndexes: indexes] retain];
    [fGroups removeObjectsAtIndexes: indexes];
    
    //insert objects at new location
    for (i = 0; i < [movingGroups count]; i++)
        [fGroups insertObject: [movingGroups objectAtIndex: i] atIndex: newRow + i];
    
    [movingGroups release];
    
    [self saveGroups];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
    
    NSMutableIndexSet * newSelectedIndexes = nil;
    if ([selectedGroups count] > 0)
    {
        newSelectedIndexes = [NSMutableIndexSet indexSet];
        NSEnumerator * enumerator = [selectedGroups objectEnumerator];
        NSDictionary * dict;
        while ((dict = [enumerator nextObject]))
            [newSelectedIndexes addIndex: [fGroups indexOfObject: dict]];
    }
    
    return newSelectedIndexes;
}

- (NSMenu *) groupMenuWithTarget: (id) target action: (SEL) action isSmall: (BOOL) small
{
    NSMenu * menu = [[NSMenu alloc] initWithTitle: @"Groups"];
    
    NSMenuItem * item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"None", "Groups -> Menu") action: action
                            keyEquivalent: @""];
    [item setTarget: target];
    [item setTag: -1];
    [menu addItem: item];
    [item release];
    
    NSEnumerator * enumerator = [fGroups objectEnumerator];
    NSDictionary * dict;
    while ((dict = [enumerator nextObject]))
    {
        item = [[NSMenuItem alloc] initWithTitle: [dict objectForKey: @"Name"] action: action keyEquivalent: @""];
        [item setTarget: target];
        
        [item setImage: [self imageForGroup: dict isSmall: small]];
        [item setTag: [[dict objectForKey: @"Index"] intValue]];
        
        [menu addItem: item];
        [item release];
    }
    
    return [menu autorelease];
}

@end

@implementation GroupsController (Private)

- (void) saveGroups
{
    [[NSUserDefaults standardUserDefaults] setObject: [NSArchiver archivedDataWithRootObject: fGroups] forKey: @"Groups"];
}

- (CTGradient *) gradientForColor: (NSColor *) color
{
    return [CTGradient gradientWithBeginningColor: [color blendedColorWithFraction: 0.65 ofColor: [NSColor whiteColor]]
            endingColor: [color blendedColorWithFraction: 0.2 ofColor: [NSColor whiteColor]]];
}

- (NSImage *) imageForGroup: (NSDictionary *) dict isSmall: (BOOL) small
{
    float width = small ? ICON_WIDTH_SMALL : ICON_WIDTH;
    NSRect rect = NSMakeRect(0.0, 0.0, width, width);
    NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: rect radius: 4.0];
    NSImage * icon = [[NSImage alloc] initWithSize: rect.size];
    
    [icon lockFocus];
    [[self gradientForColor: [dict objectForKey: @"Color"]] fillBezierPath: bp angle: 270.0];
    [icon unlockFocus];
    
    return [icon autorelease];
}

@end
