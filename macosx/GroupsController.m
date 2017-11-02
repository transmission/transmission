/******************************************************************************
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
            fGroups = [NSKeyedUnarchiver unarchiveObjectWithData: data];
        else if ((data = [[NSUserDefaults standardUserDefaults] dataForKey: @"Groups"])) //handle old groups
        {
            fGroups = [NSUnarchiver unarchiveObjectWithData: data];
            [[NSUserDefaults standardUserDefaults] removeObjectForKey: @"Groups"];
            [self saveGroups];
        }
        else
        {
            //default groups
            NSMutableDictionary * red = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor redColor], @"Color",
                                            NSLocalizedString(@"Red", "Groups -> Name"), @"Name",
                                            @0, @"Index", nil];

            NSMutableDictionary * orange = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor orangeColor], @"Color",
                                            NSLocalizedString(@"Orange", "Groups -> Name"), @"Name",
                                            @1, @"Index", nil];

            NSMutableDictionary * yellow = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor yellowColor], @"Color",
                                            NSLocalizedString(@"Yellow", "Groups -> Name"), @"Name",
                                            @2, @"Index", nil];

            NSMutableDictionary * green = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor greenColor], @"Color",
                                            NSLocalizedString(@"Green", "Groups -> Name"), @"Name",
                                            @3, @"Index", nil];

            NSMutableDictionary * blue = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor blueColor], @"Color",
                                            NSLocalizedString(@"Blue", "Groups -> Name"), @"Name",
                                            @4, @"Index", nil];

            NSMutableDictionary * purple = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor purpleColor], @"Color",
                                            NSLocalizedString(@"Purple", "Groups -> Name"), @"Name",
                                            @5, @"Index", nil];

            NSMutableDictionary * gray = [NSMutableDictionary dictionaryWithObjectsAndKeys:
                                            [NSColor grayColor], @"Color",
                                            NSLocalizedString(@"Gray", "Groups -> Name"), @"Name",
                                            @6, @"Index", nil];

            fGroups = [[NSMutableArray alloc] initWithObjects: red, orange, yellow, green, blue, purple, gray, nil];
            [self saveGroups]; //make sure this is saved right away
        }
    }

    return self;
}


- (NSInteger) numberOfGroups
{
    return [fGroups count];
}

- (NSInteger) rowValueForIndex: (NSInteger) index
{
    if (index != -1)
    {
        for (NSUInteger i = 0; i < [fGroups count]; i++)
            if (index == [fGroups[i][@"Index"] integerValue])
                return i;
    }
    return -1;
}

- (NSInteger) indexForRow: (NSInteger) row
{
    return [fGroups[row][@"Index"] integerValue];
}

- (NSString *) nameForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? fGroups[orderIndex][@"Name"] : nil;
}

- (void) setName: (NSString *) name forIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    fGroups[orderIndex][@"Name"] = name;
    [self saveGroups];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (NSImage *) imageForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? [self imageForGroup: fGroups[orderIndex]]
                            : [NSImage imageNamed: @"GroupsNoneTemplate"];
}

- (NSColor *) colorForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? fGroups[orderIndex][@"Color"] : nil;
}

- (void) setColor: (NSColor *) color forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = fGroups[[self rowValueForIndex: index]];
    [dict removeObjectForKey: @"Icon"];

    dict[@"Color"] = color;

    [[GroupsController groups] saveGroups];
    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
}

- (BOOL) usesCustomDownloadLocationForIndex: (NSInteger) index
{
    if (![self customDownloadLocationForIndex: index])
        return NO;

    NSInteger orderIndex = [self rowValueForIndex: index];
    return [fGroups[orderIndex][@"UsesCustomDownloadLocation"] boolValue];
}

- (void) setUsesCustomDownloadLocation: (BOOL) useCustomLocation forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = fGroups[[self rowValueForIndex: index]];

    dict[@"UsesCustomDownloadLocation"] = @(useCustomLocation);

    [[GroupsController groups] saveGroups];
}

- (NSString *) customDownloadLocationForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    return orderIndex != -1 ? fGroups[orderIndex][@"CustomDownloadLocation"] : nil;
}

- (void) setCustomDownloadLocation: (NSString *) location forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = fGroups[[self rowValueForIndex: index]];
    dict[@"CustomDownloadLocation"] = location;

    [[GroupsController groups] saveGroups];
}

- (BOOL) usesAutoAssignRulesForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    if (orderIndex == -1)
        return NO;

    NSNumber * assignRules = fGroups[orderIndex][@"UsesAutoGroupRules"];
    return assignRules && [assignRules boolValue];
}

- (void) setUsesAutoAssignRules: (BOOL) useAutoAssignRules forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = fGroups[[self rowValueForIndex: index]];

    dict[@"UsesAutoGroupRules"] = @(useAutoAssignRules);

    [[GroupsController groups] saveGroups];
}

- (NSPredicate *) autoAssignRulesForIndex: (NSInteger) index
{
    NSInteger orderIndex = [self rowValueForIndex: index];
    if (orderIndex == -1)
        return nil;

    return fGroups[orderIndex][@"AutoGroupRules"];
}

- (void) setAutoAssignRules: (NSPredicate *) predicate forIndex: (NSInteger) index
{
    NSMutableDictionary * dict = fGroups[[self rowValueForIndex: index]];

    if (predicate)
    {
        dict[@"AutoGroupRules"] = predicate;
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
        [candidates removeIndex: [dict[@"Index"] integerValue]];

    const NSInteger index = [candidates firstIndex];

    [fGroups addObject: [NSMutableDictionary dictionaryWithObjectsAndKeys: @(index), @"Index",
                            [NSColor colorWithCalibratedRed: 0.0 green: 0.65 blue: 1.0 alpha: 1.0], @"Color", @"", @"Name", nil]];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"UpdateGroups" object: self];
    [self saveGroups];
}

- (void) removeGroupWithRowIndex: (NSInteger) row
{
    NSInteger index = [fGroups[row][@"Index"] integerValue];
    [fGroups removeObjectAtIndex: row];

    [[NSNotificationCenter defaultCenter] postNotificationName: @"GroupValueRemoved" object: self userInfo:
     @{@"Index": @(index)}];

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
    }
    else
        [item setImage: icon];

    [menu addItem: item];

    for (NSMutableDictionary * dict in fGroups)
    {
        item = [[NSMenuItem alloc] initWithTitle: dict[@"Name"] action: action keyEquivalent: @""];
        [item setTarget: target];

        [item setTag: [dict[@"Index"] integerValue]];

        NSImage * icon = [self imageForGroup: dict];
        if (small)
        {
            icon = [icon copy];
            [icon setSize: NSMakeSize(ICON_WIDTH_SMALL, ICON_WIDTH_SMALL)];

            [item setImage: icon];
        }
        else
            [item setImage: icon];

        [menu addItem: item];
    }

    return menu;
}

- (NSInteger) groupIndexForTorrent: (Torrent *) torrent
{
    for (NSDictionary * group in fGroups)
    {
        NSInteger row = [group[@"Index"] integerValue];
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
    }

    [[NSUserDefaults standardUserDefaults] setObject: [NSKeyedArchiver archivedDataWithRootObject: groups] forKey: @"GroupDicts"];
}

- (NSImage *) imageForGroup: (NSMutableDictionary *) dict
{
    NSImage * image;
    if ((image = dict[@"Icon"]))
        return image;

    NSRect rect = NSMakeRect(0.0, 0.0, ICON_WIDTH, ICON_WIDTH);

    NSBezierPath * bp = [NSBezierPath bezierPathWithRoundedRect: rect xRadius: 3.0 yRadius: 3.0];
    NSImage * icon = [[NSImage alloc] initWithSize: rect.size];

    NSColor * color = dict[@"Color"];

    [icon lockFocus];

    //border
    NSGradient * gradient = [[NSGradient alloc] initWithStartingColor: [color blendedColorWithFraction: 0.45 ofColor:
                                [NSColor whiteColor]] endingColor: color];
    [gradient drawInBezierPath: bp angle: 270.0];

    //inside
    bp = [NSBezierPath bezierPathWithRoundedRect: NSInsetRect(rect, 1.0, 1.0) xRadius: 3.0 yRadius: 3.0];
    gradient = [[NSGradient alloc] initWithStartingColor: [color blendedColorWithFraction: 0.75 ofColor: [NSColor whiteColor]]
                endingColor: [color blendedColorWithFraction: 0.2 ofColor: [NSColor whiteColor]]];
    [gradient drawInBezierPath: bp angle: 270.0];

    [icon unlockFocus];

    dict[@"Icon"] = icon;

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
