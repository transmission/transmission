// This file Copyright © 2012-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import "WebSeedTableView.h"

@implementation WebSeedTableView

- (void)mouseDown:(NSEvent*)event
{
    [self.window makeKeyWindow];
    [super mouseDown:event];
}

- (void)copy:(id)sender
{
    NSIndexSet* indexes = self.selectedRowIndexes;
    NSMutableArray* addresses = [NSMutableArray arrayWithCapacity:indexes.count];
    [self.webSeeds enumerateObjectsAtIndexes:indexes options:0
                                  usingBlock:^(NSDictionary* webSeed, NSUInteger /*idx*/, BOOL* /*stop*/) {
                                      [addresses addObject:webSeed[@"Address"]];
                                  }];

    NSString* text = [addresses componentsJoinedByString:@"\n"];

    NSPasteboard* pb = NSPasteboard.generalPasteboard;
    [pb clearContents];
    [pb writeObjects:@[ text ]];
}

- (BOOL)validateMenuItem:(NSMenuItem*)menuItem
{
    SEL const action = menuItem.action;

    if (action == @selector(copy:))
    {
        return self.numberOfSelectedRows > 0;
    }

    return YES;
}

@end
