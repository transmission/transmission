// This file Copyright (c) 2012-2022 Transmission authors and contributors
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <Cocoa/Cocoa.h>

@interface WebSeedTableView : NSTableView
{
    //weak references
    NSArray* fWebSeeds;
}

- (void)setWebSeeds:(NSArray*)webSeeds;

- (void)copy:(id)sender;

@end
