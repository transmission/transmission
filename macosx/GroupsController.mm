// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "GroupsController.h"
#import "NSImageAdditions.h"
#import "NSMutableArrayAdditions.h"

static CGFloat const kIconWidth = 16.0;
static CGFloat const kBorderWidth = 1.25;
static CGFloat const kIconWidthSmall = 12.0;

#pragma mark - Safe auto-assign rule (de)serialization

// GHSA-v7mp-fpv3-jf5r: auto-assign rules used to be archived as live NSPredicate object graphs, which let a
// crafted preferences plist smuggle in a function/custom-selector expression and trigger arbitrary Objective-C
// message dispatch on evaluation. The functions below only ever build/accept the fixed AND/OR/NOT-of-comparisons
// shape GroupRules.xib's rule editor produces, as a plain property-list schema - nothing else is stored or trusted.
// remainingNodeBudget caps total nodes visited per tree, since depth/fan-out limits alone don't bound their product.
static NSUInteger const kAutoGroupRuleMaxDepth = 8;
static NSUInteger const kAutoGroupRuleMaxChildren = 32;
static NSUInteger const kAutoGroupRuleMaxTotalNodes = 256;

static NSSet<NSString*>* allowedAutoGroupRuleKeyPaths(void)
{
    static NSSet<NSString*>* keyPaths;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        //keep in sync with the key paths offered by GroupRules.xib's predicate row templates
        keyPaths = [NSSet setWithObjects:@"name", @"comment", @"flatFileList.name", @"allTrackersFlat", nil];
    });
    return keyPaths;
}

static BOOL autoGroupRuleOperatorIsAllowed(NSPredicateOperatorType type)
{
    //no default case: -Wswitch will flag it if a new NSPredicateOperatorType is ever added, forcing an explicit
    //allow/deny decision here instead of silently falling through
    switch (type)
    {
    case NSEqualToPredicateOperatorType:
    case NSNotEqualToPredicateOperatorType:
    case NSBeginsWithPredicateOperatorType:
    case NSEndsWithPredicateOperatorType:
    case NSContainsPredicateOperatorType:
    case NSMatchesPredicateOperatorType:
        return YES;
    case NSLessThanPredicateOperatorType:
    case NSLessThanOrEqualToPredicateOperatorType:
    case NSGreaterThanPredicateOperatorType:
    case NSGreaterThanOrEqualToPredicateOperatorType:
    case NSLikePredicateOperatorType:
    case NSInPredicateOperatorType:
    case NSCustomSelectorPredicateOperatorType:
    case NSBetweenPredicateOperatorType:
        return NO;
    }
}

static BOOL autoGroupRuleModifierIsAllowed(NSComparisonPredicateModifier modifier)
{
    return modifier == NSDirectPredicateModifier || modifier == NSAllPredicateModifier || modifier == NSAnyPredicateModifier;
}

static BOOL autoGroupRuleOptionsAreAllowed(NSUInteger options)
{
    return (options & ~(NSCaseInsensitivePredicateOption | NSDiacriticInsensitivePredicateOption)) == 0;
}

static BOOL autoGroupRuleCompoundTypeIsAllowed(NSCompoundPredicateType type)
{
    return type == NSNotPredicateType || type == NSAndPredicateType || type == NSOrPredicateType;
}

static NSDictionary* safeSchemaFromAutoGroupPredicate(NSPredicate* predicate, NSUInteger depth, NSUInteger* remainingNodeBudget)
{
    if (depth > kAutoGroupRuleMaxDepth || *remainingNodeBudget == 0)
    {
        return nil;
    }
    (*remainingNodeBudget)--;

    if ([predicate isKindOfClass:NSCompoundPredicate.class])
    {
        NSCompoundPredicate* compound = (NSCompoundPredicate*)predicate;
        if (!autoGroupRuleCompoundTypeIsAllowed(compound.compoundPredicateType) || compound.subpredicates.count > kAutoGroupRuleMaxChildren)
        {
            return nil;
        }

        NSMutableArray<NSDictionary*>* subSchemas = [NSMutableArray arrayWithCapacity:compound.subpredicates.count];
        for (NSPredicate* subPredicate in compound.subpredicates)
        {
            NSDictionary* subSchema = safeSchemaFromAutoGroupPredicate(subPredicate, depth + 1, remainingNodeBudget);
            if (!subSchema)
            {
                return nil;
            }
            [subSchemas addObject:subSchema];
        }

        //mutableCopy to match the Comparison case: keeps old builds able to decode the archive (just not this rule)
        return
            [@{@"Type" : @"Compound",
               @"Compound" : @(compound.compoundPredicateType),
               @"Subpredicates" : subSchemas} mutableCopy];
    }

    if ([predicate isKindOfClass:NSComparisonPredicate.class])
    {
        NSComparisonPredicate* comparison = (NSComparisonPredicate*)predicate;
        if (!autoGroupRuleOperatorIsAllowed(comparison.predicateOperatorType) ||
            !autoGroupRuleModifierIsAllowed(comparison.comparisonPredicateModifier) ||
            !autoGroupRuleOptionsAreAllowed(comparison.options))
        {
            return nil;
        }

        NSExpression* left = comparison.leftExpression;
        NSExpression* right = comparison.rightExpression;
        if (left.expressionType != NSKeyPathExpressionType || !left.keyPath || ![allowedAutoGroupRuleKeyPaths() containsObject:left.keyPath])
        {
            return nil;
        }
        if (right.expressionType != NSConstantValueExpressionType ||
            (right.constantValue && ![right.constantValue isKindOfClass:NSString.class] &&
             ![right.constantValue isKindOfClass:NSNumber.class]))
        {
            return nil;
        }

        NSMutableDictionary* schema = [@{
            @"Type" : @"Comparison",
            @"KeyPath" : left.keyPath,
            @"Modifier" : @(comparison.comparisonPredicateModifier),
            @"Operator" : @(comparison.predicateOperatorType),
            @"Options" : @(comparison.options)
        } mutableCopy];
        if (right.constantValue)
        {
            schema[@"Value"] = right.constantValue;
        }
        return schema;
    }

    return nil;
}

static NSPredicate* autoGroupPredicateFromSafeSchema(NSDictionary* schema, NSUInteger depth, NSUInteger* remainingNodeBudget)
{
    if (depth > kAutoGroupRuleMaxDepth || *remainingNodeBudget == 0 || ![schema isKindOfClass:NSDictionary.class])
    {
        return nil;
    }
    (*remainingNodeBudget)--;

    NSString* type = schema[@"Type"];
    if ([type isEqualToString:@"Compound"])
    {
        NSNumber* compoundTypeNumber = schema[@"Compound"];
        NSArray* subSchemas = schema[@"Subpredicates"];
        if (![compoundTypeNumber isKindOfClass:NSNumber.class] || ![subSchemas isKindOfClass:NSArray.class] || subSchemas.count > kAutoGroupRuleMaxChildren)
        {
            return nil;
        }

        NSCompoundPredicateType compoundType = (NSCompoundPredicateType)compoundTypeNumber.integerValue;
        if (!autoGroupRuleCompoundTypeIsAllowed(compoundType))
        {
            return nil;
        }

        NSMutableArray<NSPredicate*>* subPredicates = [NSMutableArray arrayWithCapacity:subSchemas.count];
        for (NSDictionary* subSchema in subSchemas)
        {
            NSPredicate* subPredicate = autoGroupPredicateFromSafeSchema(subSchema, depth + 1, remainingNodeBudget);
            if (!subPredicate)
            {
                return nil;
            }
            [subPredicates addObject:subPredicate];
        }

        if (compoundType == NSNotPredicateType)
        {
            return subPredicates.count == 1 ? [NSCompoundPredicate notPredicateWithSubpredicate:subPredicates.firstObject] : nil;
        }
        return [[NSCompoundPredicate alloc] initWithType:compoundType subpredicates:subPredicates];
    }

    if ([type isEqualToString:@"Comparison"])
    {
        NSString* keyPath = schema[@"KeyPath"];
        NSNumber* modifierNumber = schema[@"Modifier"];
        NSNumber* operatorNumber = schema[@"Operator"];
        NSNumber* optionsNumber = schema[@"Options"];
        id value = schema[@"Value"];

        if (![keyPath isKindOfClass:NSString.class] || ![allowedAutoGroupRuleKeyPaths() containsObject:keyPath] ||
            ![modifierNumber isKindOfClass:NSNumber.class] || ![operatorNumber isKindOfClass:NSNumber.class] ||
            ![optionsNumber isKindOfClass:NSNumber.class] ||
            (value && ![value isKindOfClass:NSString.class] && ![value isKindOfClass:NSNumber.class]))
        {
            return nil;
        }

        NSComparisonPredicateModifier modifier = (NSComparisonPredicateModifier)modifierNumber.integerValue;
        NSPredicateOperatorType operatorType = (NSPredicateOperatorType)operatorNumber.integerValue;
        NSUInteger options = optionsNumber.unsignedIntegerValue;
        if (!autoGroupRuleModifierIsAllowed(modifier) || !autoGroupRuleOperatorIsAllowed(operatorType) ||
            !autoGroupRuleOptionsAreAllowed(options))
        {
            return nil;
        }

        NSExpression* left = [NSExpression expressionForKeyPath:keyPath];
        NSExpression* right = [NSExpression expressionForConstantValue:value];
        return [NSComparisonPredicate predicateWithLeftExpression:left rightExpression:right modifier:modifier type:operatorType
                                                          options:options];
    }

    return nil;
}

@interface GroupsController ()

@property(nonatomic, readonly) NSMutableArray<NSMutableDictionary*>* fGroups;

- (NSMutableArray<NSMutableDictionary*>*)decodedGroupsFromArchivedData:(NSData*)data
                                                    migratedLegacyRule:(BOOL*)migratedLegacyRule;
- (void)saveGroups;

@end

@implementation GroupsController

GroupsController* fGroupsInstance = nil;

+ (GroupsController*)groups
{
    if (!fGroupsInstance)
    {
        fGroupsInstance = [[GroupsController alloc] init];
    }
    return fGroupsInstance;
}

- (instancetype)init
{
    if ((self = [super init]))
    {
        NSData* data;
        if ((data = [NSUserDefaults.standardUserDefaults dataForKey:@"GroupDicts"]))
        {
            BOOL migratedLegacyRule = NO;
            _fGroups = [self decodedGroupsFromArchivedData:data migratedLegacyRule:&migratedLegacyRule];
            if (_fGroups != nil && migratedLegacyRule)
            {
                //rewrite now so no raw predicate object graphs are archived going forward
                [self saveGroups];
            }
        }
        else if ([NSUserDefaults.standardUserDefaults objectForKey:@"Groups"])
        {
            //ancient pre-GroupDicts key; used to migrate via unrestricted NSUnarchiver, an even bigger deserialization
            //hole than GHSA-v7mp-fpv3-jf5r, so it's discarded rather than read
            [NSUserDefaults.standardUserDefaults removeObjectForKey:@"Groups"];
        }
        if (_fGroups == nil)
        {
            //default groups
            NSMutableDictionary* red = [NSMutableDictionary
                dictionaryWithObjectsAndKeys:NSColor.systemRedColor, @"Color", NSLocalizedString(@"Red", "Groups -> Name"), @"Name", @0, @"Index", nil];

            NSMutableDictionary* orange = [NSMutableDictionary
                dictionaryWithObjectsAndKeys:NSColor.systemOrangeColor, @"Color", NSLocalizedString(@"Orange", "Groups -> Name"), @"Name", @1, @"Index", nil];

            NSMutableDictionary* yellow = [NSMutableDictionary
                dictionaryWithObjectsAndKeys:NSColor.systemYellowColor, @"Color", NSLocalizedString(@"Yellow", "Groups -> Name"), @"Name", @2, @"Index", nil];

            NSMutableDictionary* green = [NSMutableDictionary
                dictionaryWithObjectsAndKeys:NSColor.systemGreenColor, @"Color", NSLocalizedString(@"Green", "Groups -> Name"), @"Name", @3, @"Index", nil];

            NSMutableDictionary* blue = [NSMutableDictionary
                dictionaryWithObjectsAndKeys:NSColor.systemBlueColor, @"Color", NSLocalizedString(@"Blue", "Groups -> Name"), @"Name", @4, @"Index", nil];

            NSMutableDictionary* purple = [NSMutableDictionary
                dictionaryWithObjectsAndKeys:NSColor.systemPurpleColor, @"Color", NSLocalizedString(@"Purple", "Groups -> Name"), @"Name", @5, @"Index", nil];

            NSMutableDictionary* gray = [NSMutableDictionary
                dictionaryWithObjectsAndKeys:NSColor.systemGrayColor, @"Color", NSLocalizedString(@"Gray", "Groups -> Name"), @"Name", @6, @"Index", nil];

            _fGroups = [[NSMutableArray alloc] initWithObjects:red, orange, yellow, green, blue, purple, gray, nil];
            [self saveGroups]; //make sure this is saved right away
        }
    }

    return self;
}

- (NSInteger)numberOfGroups
{
    return self.fGroups.count;
}

- (NSInteger)rowValueForIndex:(NSInteger)index
{
    if (index != -1)
    {
        for (NSUInteger i = 0; i < self.fGroups.count; i++)
        {
            if (index == [self.fGroups[i][@"Index"] integerValue])
            {
                return i;
            }
        }
    }
    return -1;
}

- (NSInteger)indexForRow:(NSInteger)row
{
    return [self.fGroups[row][@"Index"] integerValue];
}

- (NSString*)nameForIndex:(NSInteger)index
{
    NSInteger orderIndex = [self rowValueForIndex:index];
    return orderIndex != -1 ? self.fGroups[orderIndex][@"Name"] : nil;
}

- (void)setName:(NSString*)name forIndex:(NSInteger)index
{
    NSInteger orderIndex = [self rowValueForIndex:index];
    self.fGroups[orderIndex][@"Name"] = name;
    [self saveGroups];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGroups" object:self];
}

- (NSImage*)imageForIndex:(NSInteger)index
{
    NSInteger orderIndex = [self rowValueForIndex:index];
    return orderIndex != -1 ? [self imageForGroup:self.fGroups[orderIndex]] : [self imageForGroupNone];
}

- (NSColor*)colorForIndex:(NSInteger)index
{
    NSInteger orderIndex = [self rowValueForIndex:index];
    return orderIndex != -1 ? self.fGroups[orderIndex][@"Color"] : nil;
}

- (void)setColor:(NSColor*)color forIndex:(NSInteger)index
{
    NSMutableDictionary* dict = self.fGroups[[self rowValueForIndex:index]];
    [dict removeObjectForKey:@"Icon"];

    dict[@"Color"] = color;

    [GroupsController.groups saveGroups];
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGroups" object:self];
}

- (BOOL)usesCustomDownloadLocationForIndex:(NSInteger)index
{
    if (![self customDownloadLocationForIndex:index])
    {
        return NO;
    }

    NSInteger orderIndex = [self rowValueForIndex:index];
    return [self.fGroups[orderIndex][@"UsesCustomDownloadLocation"] boolValue];
}

- (void)setUsesCustomDownloadLocation:(BOOL)useCustomLocation forIndex:(NSInteger)index
{
    NSMutableDictionary* dict = self.fGroups[[self rowValueForIndex:index]];

    dict[@"UsesCustomDownloadLocation"] = @(useCustomLocation);

    [GroupsController.groups saveGroups];
}

- (NSString*)customDownloadLocationForIndex:(NSInteger)index
{
    NSInteger orderIndex = [self rowValueForIndex:index];
    return orderIndex != -1 ? self.fGroups[orderIndex][@"CustomDownloadLocation"] : nil;
}

- (void)setCustomDownloadLocation:(NSString*)location forIndex:(NSInteger)index
{
    NSMutableDictionary* dict = self.fGroups[[self rowValueForIndex:index]];
    dict[@"CustomDownloadLocation"] = location;

    [GroupsController.groups saveGroups];
}

- (BOOL)usesAutoAssignRulesForIndex:(NSInteger)index
{
    NSInteger orderIndex = [self rowValueForIndex:index];
    if (orderIndex == -1)
    {
        return NO;
    }

    NSNumber* assignRules = self.fGroups[orderIndex][@"UsesAutoGroupRules"];
    return assignRules && assignRules.boolValue;
}

- (void)setUsesAutoAssignRules:(BOOL)useAutoAssignRules forIndex:(NSInteger)index
{
    NSMutableDictionary* dict = self.fGroups[[self rowValueForIndex:index]];

    dict[@"UsesAutoGroupRules"] = @(useAutoAssignRules);

    [GroupsController.groups saveGroups];
}

- (NSPredicate*)autoAssignRulesForIndex:(NSInteger)index
{
    NSInteger orderIndex = [self rowValueForIndex:index];
    if (orderIndex == -1)
    {
        return nil;
    }

    return self.fGroups[orderIndex][@"AutoGroupRules"];
}

- (void)setAutoAssignRules:(NSPredicate*)predicate forIndex:(NSInteger)index
{
    NSMutableDictionary* dict = self.fGroups[[self rowValueForIndex:index]];

    if (predicate)
    {
        //catches the rule editor producing a shape saveGroups would otherwise silently drop later
        NSUInteger assertNodeBudget = kAutoGroupRuleMaxTotalNodes;
        (void)assertNodeBudget; //only used inside NSAssert, which compiles out in Release
        NSAssert(
            safeSchemaFromAutoGroupPredicate(predicate, 0, &assertNodeBudget) != nil,
            @"Auto-assign rule predicate (%@) doesn't match the allowlisted schema and would be silently discarded on save",
            predicate);

        dict[@"AutoGroupRules"] = predicate;
        [GroupsController.groups saveGroups];
    }
    else
    {
        [dict removeObjectForKey:@"AutoGroupRules"];
        [self setUsesAutoAssignRules:NO forIndex:index];
    }
}

- (void)addNewGroup
{
    //find the lowest index
    NSMutableIndexSet* candidates = [NSMutableIndexSet indexSetWithIndexesInRange:NSMakeRange(0, self.fGroups.count + 1)];
    for (NSDictionary* dict in self.fGroups)
    {
        [candidates removeIndex:[dict[@"Index"] integerValue]];
    }

    NSInteger const index = candidates.firstIndex;

    [self.fGroups addObject:[NSMutableDictionary dictionaryWithObjectsAndKeys:@(index),
                                                                              @"Index",
                                                                              [NSColor colorWithCalibratedRed:0.0 green:0.65
                                                                                                         blue:1.0
                                                                                                        alpha:1.0],
                                                                              @"Color",
                                                                              @"",
                                                                              @"Name",
                                                                              nil]];

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGroups" object:self];
    [self saveGroups];
}

- (void)removeGroupWithRowIndex:(NSInteger)row
{
    NSInteger index = [self.fGroups[row][@"Index"] integerValue];
    [self.fGroups removeObjectAtIndex:row];

    [NSNotificationCenter.defaultCenter postNotificationName:@"GroupValueRemoved" object:self
                                                    userInfo:@{ @"Index" : @(index) }];

    if (index == [NSUserDefaults.standardUserDefaults integerForKey:@"FilterGroup"])
    {
        [NSUserDefaults.standardUserDefaults setInteger:-2 forKey:@"FilterGroup"];
    }

    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGroups" object:self];
    [self saveGroups];
}

- (void)moveGroupAtRow:(NSInteger)oldRow toRow:(NSInteger)newRow
{
    [self.fGroups moveObjectAtIndex:oldRow toIndex:newRow];

    [self saveGroups];
    [NSNotificationCenter.defaultCenter postNotificationName:@"UpdateGroups" object:self];
}

- (NSMenu*)groupMenuWithTarget:(id)target action:(SEL)action isSmall:(BOOL)small
{
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];

    void (^addItemWithTitleTagIcon)(NSString*, NSInteger, NSImage*) = ^void(NSString* title, NSInteger tag, NSImage* icon) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:@""];
        item.target = target;
        item.tag = tag;

        if (small)
        {
            icon = [icon copy];
            icon.size = NSMakeSize(kIconWidthSmall, kIconWidthSmall);

            item.image = icon;
        }
        else
        {
            item.image = icon;
        }

        [menu addItem:item];
    };

    addItemWithTitleTagIcon(NSLocalizedString(@"None", "Groups -> Menu"), -1, [self imageForGroupNone]);

    for (NSMutableDictionary* dict in self.fGroups)
    {
        addItemWithTitleTagIcon(dict[@"Name"], [dict[@"Index"] integerValue], [self imageForGroup:dict]);
    }

    return menu;
}

- (NSInteger)groupIndexForTorrent:(Torrent*)torrent
{
    for (NSDictionary* group in self.fGroups)
    {
        NSInteger row = [group[@"Index"] integerValue];
        if ([self torrent:torrent doesMatchRulesForGroupAtIndex:row])
        {
            return row;
        }
    }
    return -1;
}

#pragma mark - Private

- (NSMutableArray<NSMutableDictionary*>*)decodedGroupsFromArchivedData:(NSData*)data
                                                    migratedLegacyRule:(BOOL*)migratedLegacyRule
{
    id decoded;
    @try
    {
        //NSPredicate.class is only for decoding old archives; below, any decoded predicate is validated and rebuilt
        //from scratch (or discarded) rather than trusted or evaluated directly
        decoded = [NSKeyedUnarchiver unarchivedObjectOfClasses:[NSSet setWithObjects:NSMutableArray.class,
                                                                                     NSArray.class,
                                                                                     NSMutableDictionary.class,
                                                                                     NSDictionary.class,
                                                                                     NSNumber.class,
                                                                                     NSColor.class,
                                                                                     NSString.class,
                                                                                     NSPredicate.class,
                                                                                     nil]
                                                      fromData:data
                                                         error:nil];
    }
    @catch (NSException* exception)
    {
        NSLog(@"Error when decoding groups (%@)", exception);
        return nil;
    }

    if (![decoded isKindOfClass:NSArray.class])
    {
        return nil;
    }

    NSMutableArray<NSMutableDictionary*>* validatedGroups = [NSMutableArray arrayWithCapacity:[(NSArray*)decoded count]];
    for (id groupObject in (NSArray*)decoded)
    {
        if (![groupObject isKindOfClass:NSDictionary.class] || ![((NSDictionary*)groupObject)[@"Index"] isKindOfClass:NSNumber.class])
        {
            continue; //drop malformed entries rather than fail the whole load
        }

        NSMutableDictionary* group = [groupObject mutableCopy];

        NSDictionary* schema = group[@"AutoGroupRulesSchema"];
        id legacyPredicate = group[@"AutoGroupRules"];
        [group removeObjectForKey:@"AutoGroupRulesSchema"];

        NSPredicate* rebuilt = nil;
        if ([schema isKindOfClass:NSDictionary.class])
        {
            NSUInteger nodeBudget = kAutoGroupRuleMaxTotalNodes;
            rebuilt = autoGroupPredicateFromSafeSchema(schema, 0, &nodeBudget);
        }
        else if ([legacyPredicate isKindOfClass:NSPredicate.class])
        {
            NSUInteger schemaNodeBudget = kAutoGroupRuleMaxTotalNodes;
            NSDictionary* legacySchema = safeSchemaFromAutoGroupPredicate(legacyPredicate, 0, &schemaNodeBudget);
            NSUInteger rebuildNodeBudget = kAutoGroupRuleMaxTotalNodes;
            rebuilt = legacySchema ? autoGroupPredicateFromSafeSchema(legacySchema, 0, &rebuildNodeBudget) : nil;
            *migratedLegacyRule = YES;
        }

        if ([schema isKindOfClass:NSDictionary.class] || [legacyPredicate isKindOfClass:NSPredicate.class])
        {
            if (rebuilt)
            {
                group[@"AutoGroupRules"] = rebuilt;
            }
            else
            {
                //unrecognized shape - drop the rule rather than risk evaluating it
                [group removeObjectForKey:@"AutoGroupRules"];
                group[@"UsesAutoGroupRules"] = @NO;
                NSLog(@"Transmission: discarded an auto-assign rule with an unsupported predicate structure");
            }
        }
        else if (legacyPredicate != nil)
        {
            [group removeObjectForKey:@"AutoGroupRules"];
            group[@"UsesAutoGroupRules"] = @NO;
        }

        [validatedGroups addObject:group];
    }

    return validatedGroups;
}

- (void)saveGroups
{
    //don't archive the icon, and never archive a live NSPredicate - only its safe schema representation
    NSMutableArray* groups = [NSMutableArray arrayWithCapacity:self.fGroups.count];
    for (NSDictionary* dict in self.fGroups)
    {
        NSMutableDictionary* tempDict = [dict mutableCopy];
        [tempDict removeObjectForKey:@"Icon"];

        NSPredicate* predicate = tempDict[@"AutoGroupRules"];
        if (predicate)
        {
            NSUInteger nodeBudget = kAutoGroupRuleMaxTotalNodes;
            NSDictionary* schema = safeSchemaFromAutoGroupPredicate(predicate, 0, &nodeBudget);
            if (schema)
            {
                tempDict[@"AutoGroupRulesSchema"] = schema;
            }
            [tempDict removeObjectForKey:@"AutoGroupRules"];
        }

        [groups addObject:tempDict];
    }

    [NSUserDefaults.standardUserDefaults setObject:[NSKeyedArchiver archivedDataWithRootObject:groups requiringSecureCoding:YES
                                                                                         error:nil]
                                            forKey:@"GroupDicts"];
}

- (NSImage*)imageForGroupNone
{
    static NSImage* icon;
    if (icon)
    {
        return icon;
    }

    icon = [NSImage imageWithSize:NSMakeSize(kIconWidth, kIconWidth) flipped:NO drawingHandler:^BOOL(NSRect rect) {
        //shape
        rect = NSInsetRect(rect, kBorderWidth / 2, kBorderWidth / 2);
        NSBezierPath* bp = [NSBezierPath bezierPathWithOvalInRect:rect];
        bp.lineWidth = kBorderWidth;

        //border
        // code reference for dashed style
        //CGFloat dashAndGapLength = M_PI * rect.size.width / 8;
        //CGFloat pattern[2] = { dashAndGapLength * .5, dashAndGapLength * .5 };
        //[bp setLineDash:pattern count:2 phase:0];

        [NSColor.controlTextColor setStroke];
        [bp stroke];

        return YES;
    }];
    [icon setTemplate:YES];

    return icon;
}

- (NSImage*)imageForGroup:(NSMutableDictionary*)dict
{
    NSImage* icon;
    if ((icon = dict[@"Icon"]))
    {
        return icon;
    }

    icon = [NSImage discIconWithColor:dict[@"Color"] insetFactor:0];

    dict[@"Icon"] = icon;

    return icon;
}

- (BOOL)torrent:(Torrent*)torrent doesMatchRulesForGroupAtIndex:(NSInteger)index
{
    if (![self usesAutoAssignRulesForIndex:index])
    {
        return NO;
    }

    //always freshly built, never a raw decoded predicate, so it needs no allowEvaluation
    NSPredicate* predicate = [self autoAssignRulesForIndex:index];
    BOOL eval = NO;
    @try
    {
        eval = [predicate evaluateWithObject:torrent];
    }
    @catch (NSException* exception)
    {
        NSLog(@"Error when evaluating predicate (%@) - %@", predicate, exception);
    }
    @finally
    {
        return eval;
    }
}

@end
