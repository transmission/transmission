#import "FilePriorityCell.h"
#import "InfoWindowController.h"
#import "Torrent.h"

@implementation FilePriorityCell

- (id) init
{
    if ((self = [super init]))
    {
        [self setTrackingMode: NSSegmentSwitchTrackingSelectAny];
        [self setControlSize: NSMiniControlSize];
        [self setSegmentCount: 3];
        
        int i;
        for (i = 0; i < [self segmentCount]; i++)
        {
            [self setLabel: @"" forSegment: i];
            [self setWidth: 7.0 forSegment: i];
        }
    }
    return self;
}

- (void) setItem: (NSMutableDictionary *) item
{
    fItem = item;
}

- (void) setSelected: (BOOL) flag forSegment: (int) segment
{
    [super setSelected: flag forSegment: segment];
    
    //only for when clicking manually
    Torrent * torrent = [[[[self controlView] window] windowController] selectedTorrent];
    NSIndexSet * indexes = [fItem objectForKey: @"Indexes"];
    
    int priority;
    if (segment == 0)
        priority = TR_PRI_LOW;
    else if (segment == 2)
        priority = TR_PRI_HIGH;
    else
        priority = TR_PRI_NORMAL;
    
    [torrent setFilePriority: priority forIndexes: indexes];
    [(FileOutlineView *)[self controlView] reloadData];
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) controlView
{
    Torrent * torrent = [(InfoWindowController *)[[[self controlView] window] windowController] selectedTorrent];
    NSSet * priorities = [torrent filePrioritiesForIndexes: [fItem objectForKey: @"Indexes"]];
    
    int count = [priorities count];
    
    FileOutlineView * view = (FileOutlineView *)[self controlView];
    int row = [view hoverRow];
    if (count > 0 && row != -1 && [view itemAtRow: row] == fItem)
    {
        [super setSelected: [priorities containsObject: [NSNumber numberWithInt: TR_PRI_LOW]] forSegment: 0];
        [super setSelected: [priorities containsObject: [NSNumber numberWithInt: TR_PRI_NORMAL]] forSegment: 1];
        [super setSelected: [priorities containsObject: [NSNumber numberWithInt: TR_PRI_HIGH]] forSegment: 2];
        
        [super drawWithFrame: cellFrame inView: controlView];
    }
    else
    {
        NSImage * image;
        if (count == 0)
        {
            if (!fNoneImage)
                fNoneImage = [NSImage imageNamed: @"PriorityNone.png"];
            image = fNoneImage;
        }
        else if (count > 1)
        {
            if (!fMixedImage)
                fMixedImage = [NSImage imageNamed: @"PriorityMixed.png"];
            image = fMixedImage;
        }
        
        else if ([priorities containsObject: [NSNumber numberWithInt: TR_PRI_NORMAL]])
        {
            if (!fNormalImage)
                fNormalImage = [NSImage imageNamed: @"PriorityNormal.png"];
            image = fNormalImage;
        }
        else if ([priorities containsObject: [NSNumber numberWithInt: TR_PRI_LOW]])
        {
            if (!fLowImage)
                fLowImage = [NSImage imageNamed: @"PriorityLow.png"];
            image = fLowImage;
        }
        else
        {
            if (!fHighImage)
                fHighImage = [NSImage imageNamed: @"PriorityHigh.png"];
            image = fHighImage;
        }
        
        NSSize imageSize = [image size];
        [image compositeToPoint: NSMakePoint(cellFrame.origin.x + (cellFrame.size.width - imageSize.width) * 0.5,
                cellFrame.origin.y + (cellFrame.size.height + imageSize.height) * 0.5) operation: NSCompositeSourceOver];
    }
}

@end
