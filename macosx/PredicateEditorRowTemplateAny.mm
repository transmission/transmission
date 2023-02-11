// This file Copyright © 2009-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "PredicateEditorRowTemplateAny.h"

@implementation PredicateEditorRowTemplateAny

- (NSPredicate*)predicateWithSubpredicates:(NSArray*)subpredicates
{
    //we only make NSComparisonPredicates
    NSComparisonPredicate* predicate = (NSComparisonPredicate*)[super predicateWithSubpredicates:subpredicates];

    //construct a near-identical predicate
    return [NSComparisonPredicate predicateWithLeftExpression:predicate.leftExpression rightExpression:predicate.rightExpression
                                                     modifier:NSAnyPredicateModifier
                                                         type:predicate.predicateOperatorType
                                                      options:predicate.options];
}

@end
