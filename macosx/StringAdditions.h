//
//  StringAdditions.h
//  Transmission
//
//  Created by Mitchell Livingston on 1/16/06.
//  Copyright 2006 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface NSString (StringAdditions)

+ (NSString *) stringForFileSize: (uint64_t) size;
+ (NSString *) stringForSpeed: (float) speed;
- (NSString *) stringFittingInWidth: (float) width
					withAttributes: (NSDictionary *) attributes;

@end
