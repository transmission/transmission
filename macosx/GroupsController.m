/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007-2012 Transmission authors and contributors
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
#import "NSMutableArrayAdditions.h"

#define ICON_WIDTH 16.0
#define ICON_WIDTH_SMALL 12.0

@interface GroupsController (Private)

- (void) saveGroups;

- (NSImage *) imageForGroup: (NSMutableDictionary *) dict;

- (BOOL) torrent: (Torrent *) torrent doesMatchRulesForGroupAtIndex: (NSInteger) index;

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
        if ((data = [[NSUserDefaults standardUserDefaults] dataForKey: @"GroupDicts"]))
            fGroups = [[NSKeyedUnarchiver unarchiveObjectWithData: data] retain];
        else if ((data = [[NSUserDefaults standardUserDefaults] dataForKey: @"Groups"])) //handle old groups
        {
            fGroups = [[NSUnarchiver unarchiveObjectWithData: data] retain];
            [[NSUserDefaults standardUserDefaults] removeObjectForKey: @"Groups"];
            [self saveGroups];
        }
        else
        {
            //default groups
            NSMutableDictionary * red = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor redColor], @"Color",
                                            NSLocalizedString(@"Red", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInteger: 0], @"Index", nil];
            
            NSMutableDictionary * orange = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor orangeColor], @"Color",
                                            NSLocalizedString(@"Orange", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInteger: 1], @"Index", nil];
            
            NSMutableDictionary * yellow = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor yellowColor], @"Color",
                                            NSLocalizedString(@"Yellow", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInteger: 2], @"Index", nil];
            
            NSMutableDictionary * green = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor greenColor], @"Color",
                                            NSLocalizedString(@"Green", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInteger: 3], @"Index", nil];
            
            NSMutableDictionary * blue = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor blueColor], @"Color",
                                            NSLocalizedString(@"Blue", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInteger: 4], @"Index", nil];
            
            NSMutableDictionary * purple = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor purpleColor], @"Color",
                                            NSLocalizedString(@"Purple", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInteger: 5], @"Index", nil];
            
            NSMutableDictionary * gray = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor grayColor], @"Color",
                                            NSLocalizedString(@"Gray", "Groups -> Name"), @"Name",
                                            [NSNumber numberWithInteger: 6], @"Index", nil];
            
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

- (NSInteger) numberOfGroups
{
    return [fGroups count];
}

- (NSInteger) rowValueForIndex: (NSInteger) index
{
    if (index != -1)
    {
        for (NSInteger i = 0; i < [fGroups count]; i++)
            if (index == [[[fGroups objectAtIndex: i] objectForKey: @"Index"] integerValue])
                return i;
    }
    return -1;
}

- (NSInteger) indexForRow: (NSInteger) row
{
    return [[[fGroups objectAtIndex: row] objectForKey: @"Index"] integerValue];
}

- (NSString *) nameForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [[fGroups objectAtIndex: orderIndex] objectForKey: @"Name"] : nil;
}

- (void) setName: (NSString *) name forIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    [[fGroups objectAtIndex: orderIndex] setObject: name forKey: @"Name"];
    [self saveGroups];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (NSImage *) imageForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [self imageForGroup: [fGroups objectAtIndex: orderIndex]]
                            : [NSImage imageNamed: @"GroupsNoneTemplate"];
}

- (NSColor *) colorForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [[fGroups objectAtIndex: orderIndex] objectForKey: @"Color"] : nil;
}

- (void) setColor: (NSColor *) color forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    [dict removeObjectForKey: @"Icon"];
    
    [dict setObject: color forKey: @"Color"];
    
    [[GroupsController groups] saveGroups];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (BOOL) usesCustomDownloadLocationForIndex: (NSInteger) index
{
    if (![self customDownloadLocationForIndex: index])
        return NO;

    NSInteger orderIndex = [self rowValueForIndex: index];
    return [[[fGroups objectAtIndex: orderIndex] objectForKey: @"UsesCustomDownloadLocation"] boolValue];
}

- (void) setUsesCustomDownloadLocation: (BOOL) useCustomLocation forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    
    [dict setObject: [NSNumber numberWithBool: useCustomLocation] forKey: @"UsesCustomDownloadLocation"];
    
    [[GroupsController groups] saveGroups];
}

- (NSString *) customDownloadLocationForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [[fGroups objectAtIndex: orderIndex] objectForKey: @"CustomDownloadLocation"] : nil;
}

- (void) setCustomDownloadLocation: (NSString *) location forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    [dict setObject: location forKey: @"CustomDownloadLocation"];
    
    [[GroupsController groups] saveGroups];
}

- (BOOL) usesAutoAssignRulesForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    if (orderIndex == -1)
        return NO;
    
    NSNumber * assignRules = [[fGroups objectAtIndex: orderIndex] objectForKey: @"UsesAutoGroupRules"];
    return assignRules && [assignRules boolValue];
}

- (void) setUsesAutoAssignRules: (BOOL) useAutoAssignRules forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    
    [dict setObject: [NSNumber numberWithBool: useAutoAssignRules] forKey: @"UsesAutoGroupRules"];
    
    [[GroupsController groups] saveGroups];
}

- (NSPredicate *) autoAssignRulesForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    if (orderIndex == -1)
		return nil;
	
	return [[fGroups objectAtIndex: orderIndex] objectForKey: @"AutoGroupRules"];
}

- (void) setAutoAssignRules: (NSPredicate *) predicate forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = [fGroups objectAtIndex: [self rowValueForIndex: index]];
    
    if (predicate)
    {
        [dict setObject: predicate forKey: @"AutoGroupRules"];
        [[GroupsController groups] saveGroups];
    }
    else
    {
        [dict removeObjectForKey: @"AutoGroupRules"];
        [self setUsesAutoAssignRules: NO forIndex: index];
    }
}

- (void) addNewGroup
{
    //find the lowest index
    NSMutableIndexSet * candidates = [NSMutableIndexSet indexSetWithIndexesInRange: NSMakeRange(0, [fGroups count]+1)];
    for (NSDictionary * dict in fGroups)
        [candidates removeIndex: [[dict objectForKey: @"Index"] integerValue]];
    
    const NSInteger index = [candidates firstIndex];
    
    [fGroups addObject: [NSMutableDictionary dictionaryWithObjectsAndKeys: [NSNumber numberWithInteger: index], @"Index",
                            [NSColor colorWithCalibratedRed: 0.0 green: 0.65 blue: 1.0 alpha: 1.0], @"Color", @"", @"Name", nil]];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
    [self saveGroups];
}

- (void) removeGroupWithRowIndex: (NSInteger) row
{
    NSInteger index = [[[fGroups objectAtIndex: row] objectForKey: @"Index"] integerValue];
    [fGroups removeObjectAtIndex: row];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"GroupValueRemoved" object: self userInfo:
        [NSDictionary dictionaryWithObject: [NSNumber numberWithInteger: index] forKey: @"Index"]];
    
    if (index == [[NSUserDefaults standardUserDefaults] integerForKey: @"FilterGroup"])
        [[NSUserDefaults standardUserDefaults] setInteger: -2 forKey: @"FilterGroup"];
    
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
    [self saveGroups];
}

- (void) moveGroupAtRow: (NSInteger) oldRow toRow: (NSInteger) newRow
{
    [fGroups moveObjectAtIndex: oldRow toIndex: newRow];
    
    [self saveGroups];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (NSMenu *) groupMenuWithTarget: (id) target action: (SEL) action isSmall: (BOOL) small
{
    NSMenu * menu = [[NSMenu alloc] initWithTitle: @"Groups"];
    
    NSMenuItem * item = [[NSMenuItem alloc] initWithTitle: NSLocalizedString(@"None", "Groups -> Menu") action: action
                            keyEquivalent: @""];
    [item setTarget: target];
    [item setTag: -1];
    
    NSImage * icon = [NSImage imageNamed: @"GroupsNoneTemplate"];
    if (small)
    {
        icon = [icon copy];
        [icon setSize: NSMakeSize(ICON_WIDTH_SMALL, ICON_WIDTH_SMALL)];
        
        [item setImage: icon];
        [icon release];
    }
    else
        [item setImage: icon];
    
    [menu addItem: item];
    [item release];
    
    for (NSMutableDictionary * dict in fGroups)
    {
        item = [[NSMenuItem alloc] initWithTitle: [dict objectForKey: @"Name"] action: action keyEquivalent: @""];
        [item setTarget: target];
        
        [item setTag: [[dict objectForKey: @"Index"] integerValue]];
        
        NSImage * icon = [self imageForGroup: dict];
        if (small)
        {
            icon = [icon copy];
            [icon setSize: NSMakeSize(ICON_WIDTH_SMALL, ICON_WIDTH_SMALL)];
            
            [item setImage: icon];
            [icon release];
        }
        else
            [item setImage: icon];
        
        [menu addItem: item];
        [item release];
    }
    
    return [menu autorelease];
}

- (NSInteger) groupIndexForTorrent: (Torrent *) torrent;
{
    for (NSDictionary * group in fGroups)
    {
        NSInteger row = [[group objectForKey: @"Index"] integerValue];
        if ([self torrent: torrent doesMatchRulesForGroupAtIndex: row])
            return row;
    }
    return -1;
}

@end

@implementation GroupsController (Private)

- (void) saveGroups
{
    //don't archive the icon
    NSMutableArray * groups = [NSMutableArray arrayWithCapacity: [fGroups count]];
    for (NSDictionary * dict in fGroups)
    {
        NSMutableDictionary * tempDict = [dict mutableCopy];
        [tempDict removeObjectForKey: @"Icon"];
        [groups addObject: tempDict];
        [tempDict release];
    }
    
    [[NSUserDefaults standardUserDefaults] setObject: [NSKeyedArchiver archivedDataWithRootObject: groups] forKey: @"GroupDicts"];
}

- (NSImage *) imageForGroup: (NSMutableDictionary *) dict
{
    NSImage * image;
    if ((image = [dict objectForKey: @"Icon"]))
        return image;
    
    NSRect rect = NSMakeRect(0.0, 0.0, ICON_WIDTH, ICON_WIDTH);
    
    NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: rect xRadius: 3.0 yRadius: 3.0];
    NSImage * icon = [[NSImage alloc] initWithSize: rect.size];
    
    NSColor * color = [dict objectForKey: @"Color"];
    
    [icon lockFocus];
    
    //border
    NSGradient * gradient = [[NSGradient alloc] initWithStartingColor: [color blendedColorWithFraction: 0.45 ofColor:
                                [NSColor whiteColor]] endingColor: color];
    [gradient drawInBezierPath: bp angle: 270.0];
    [gradient release];
    
    //inside
    bp = [NSBezierPath bezierPathWithRoundedRect: NSInsetRect(rect, 1.0, 1.0) xRadius: 3.0 yRadius: 3.0];
    gradient = [[NSGradient alloc] initWithStartingColor: [color blendedColorWithFraction: 0.75 ofColor: [NSColor whiteColor]]
                endingColor: [color blendedColorWithFraction: 0.2 ofColor: [NSColor whiteColor]]];
    [gradient drawInBezierPath: bp angle: 270.0];
    [gradient release];
    
    [icon unlockFocus];
    
    [dict setObject: icon forKey: @"Icon"];
    [icon release];
    
    return icon;
}

- (BOOL) torrent: (Torrent *) torrent doesMatchRulesForGroupAtIndex: (NSInteger) index
{
    if (![self usesAutoAssignRulesForIndex: index])
        return NO;
	
    NSPredicate * predicate = [self autoAssignRulesForIndex: index];
    BOOL eval = NO;
    @try
    {
        eval = [predicate evaluateWithObject: torrent];
    }
    @catch (NSException * exception)
    {
        NSLog(@"Error when evaluating predicate (%@) - %@", predicate, exception);
    }
    @finally
    {
        return eval;
    }
}

@end
