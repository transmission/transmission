// This file Copyright © 2012-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

@interface WebSeedTableView : NSTableView

@property(nonatomic, weak) NSArray<NSDictionary*>* webSeeds;

- (void)copy:(id)sender;

@end
