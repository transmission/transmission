//
//  Badger.m
//  Transmission
//
//  Created by Mitchell Livingston on 12/21/05.
//

#import "Badger.h"

@interface Badger (Private)

- (NSImage *) badgeWithNum: (int) num;

@end

@implementation Badger

- (id) init
{
	if ((self = [super init]))
	{
		fBadge = [NSImage imageNamed: @"Badge"];
		fDockIcon = [[NSApp applicationIconImage] copy];
		fBadgedDockIcon = [fDockIcon copy];
		
		fBadgeAttributes = [[NSDictionary dictionaryWithObjectsAndKeys:
								[NSColor whiteColor], NSForegroundColorAttributeName,
								[NSFont fontWithName: @"Helvetica-Bold" size: 24], NSFontAttributeName,
								nil] retain];
		
		fStringAttributes = [[NSDictionary dictionaryWithObjectsAndKeys:
								[NSColor whiteColor], NSForegroundColorAttributeName,
								[NSFont fontWithName: @"Helvetica-Bold" size: 20], NSFontAttributeName,
								nil] retain];
								
		fUploadingColor = [[[NSColor greenColor] colorWithAlphaComponent: 0.65] retain];
		fDownloadingColor = [[[NSColor blueColor] colorWithAlphaComponent: 0.65] retain];
		
		fCompleted = 0;
	}
	
	return self;
}

- (void) dealloc
{
	[fDockIcon release];
	[fBadgedDockIcon release];

	[fBadgeAttributes release];
	[fStringAttributes release];
	
	[fUploadingColor release];
	[fDownloadingColor release];

	[super dealloc];
}

- (void) updateBadgeWithCompleted: (int) completed
					uploadRate: (NSString *) uploadRate
					downloadRate: (NSString *) downloadRate
{
	NSImage * dockIcon;
	NSSize iconSize = [fDockIcon size];
			
	//set seeding and downloading badges if there was a change
	if (completed != fCompleted)
	{
		fCompleted = completed;
		
		dockIcon = [fDockIcon copy];
		[dockIcon lockFocus];
		
		//set completed badge to top right
		if (completed > 0)
		{
			NSSize badgeSize = [fBadge size];
			[[self badgeWithNum: completed]
					compositeToPoint: NSMakePoint(iconSize.width - badgeSize.width,
										iconSize.height - badgeSize.height)
							operation: NSCompositeSourceOver];
		}

		[dockIcon unlockFocus];
		
		[fBadgedDockIcon release];
		fBadgedDockIcon = [dockIcon copy];
	}
	else
		dockIcon = [fBadgedDockIcon copy];
	
	if (uploadRate || downloadRate)
	{
		//upload rate at bottom
		float mainY = 5,
			mainHeight = 25;
		NSRect shapeRect = NSMakeRect(12.5, mainY, iconSize.width - 25, mainHeight);
		
		NSRect leftRect, rightRect;
		leftRect.origin.x = 0;
		leftRect.origin.y = mainY;
		leftRect.size.width = shapeRect.origin.x * 2.0;
		leftRect.size.height = mainHeight;
		
		rightRect = leftRect;
		rightRect.origin.x = iconSize.width - rightRect.size.width;
		
		NSRect textRect;
		textRect.origin.y = mainY;
		textRect.size.height = mainHeight;
		
		[dockIcon lockFocus];
		
		if (uploadRate)
		{
			float width = [uploadRate sizeWithAttributes: fStringAttributes].width;
			textRect.origin.x = (iconSize.width - width) * 0.5;
			textRect.size.width = width;
		
			NSBezierPath * uploadOval = [NSBezierPath bezierPathWithRect: shapeRect];
			[uploadOval appendBezierPathWithOvalInRect: leftRect];
			[uploadOval appendBezierPathWithOvalInRect: rightRect];
		
			[fUploadingColor set];
			[uploadOval fill];
			[uploadRate drawInRect: textRect withAttributes: fStringAttributes];
			
			//shift up for download rate if there is an upload rate
			float heightDiff = 27;
			shapeRect.origin.y += heightDiff;
			leftRect.origin.y += heightDiff;
			rightRect.origin.y += heightDiff;
			textRect.origin.y += heightDiff;
		}
		
		//download rate above upload rate
		if (downloadRate)
		{
			float width = [downloadRate sizeWithAttributes: fStringAttributes].width;
			textRect.origin.x = (iconSize.width - width) * 0.5;
			textRect.size.width = width;
		
			NSBezierPath * downloadOval = [NSBezierPath bezierPathWithRect: shapeRect];
			[downloadOval appendBezierPathWithOvalInRect: leftRect];
			[downloadOval appendBezierPathWithOvalInRect: rightRect];
		
			[fDownloadingColor set];
			[downloadOval fill];
			[downloadRate drawInRect: textRect withAttributes: fStringAttributes];
		}
		
		[dockIcon unlockFocus];
	}
	
	[NSApp setApplicationIconImage: dockIcon];
	[dockIcon release];
}

- (void) clearBadge
{
	[fBadgedDockIcon release];
	fBadgedDockIcon = [fDockIcon copy];

	[NSApp setApplicationIconImage: fDockIcon];
}

@end

@implementation Badger (Private)

- (NSImage *) badgeWithNum: (int) num
{	
	NSImage * badge = [[fBadge copy] autorelease];
	NSString * numString = [NSString stringWithFormat: @"%d", num];
	
	//number is in center of image
	NSRect badgeRect;
	NSSize numSize = [numString sizeWithAttributes: fBadgeAttributes];
	badgeRect.size = [badge size];
	badgeRect.origin.x = (badgeRect.size.width - numSize.width) * 0.5;
	badgeRect.origin.y = badgeRect.size.height * 0.5 - numSize.height * 1.2;

	[badge lockFocus];
	[numString drawInRect: badgeRect withAttributes: fBadgeAttributes];
	[badge unlockFocus];
	
	return badge;
}

@end
