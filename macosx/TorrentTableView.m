#include "TorrentTableView.h"
#include "Controller.h"

@implementation TorrentTableView

- (void) updateUI: (tr_stat_t *) stat
{
    fStat = stat;
    [self reloadData];
}

- (void) revealInFinder: (int) row
{
    NSString * string;
    NSAppleScript * appleScript;
    NSDictionary * error;

    string = [NSString stringWithFormat: @"tell application "
        "\"Finder\"\nactivate\nreveal (POSIX file \"%@/%@\")\nend tell",
        [NSString stringWithUTF8String: fStat[row].folder],
        [NSString stringWithUTF8String: fStat[row].info.name]];
    appleScript = [[NSAppleScript alloc] initWithSource: string];
    if( ![appleScript executeAndReturnError: &error] )
    {
        printf( "Reveal in Finder: AppleScript failed\n" );
    }
    [appleScript release];
}

- (void) pauseOrResume: (int) row
{
    if( fStat[row].status & TR_STATUS_PAUSE )
    {
        [fController resumeTorrentWithIndex: row];
    }
    else if( fStat[row].status & ( TR_STATUS_CHECK |
              TR_STATUS_DOWNLOAD | TR_STATUS_SEED ) )
    {
        [fController stopTorrentWithIndex: row];
    }                                                                   
}

- (void) mouseDown: (NSEvent *) e
{
    fClickPoint = [self convertPoint: [e locationInWindow] fromView: NULL];
    [self display];
}

- (NSRect) pauseRectForRow: (int) row
{
    int col;
    NSRect cellRect, rect;

    col      = [self columnWithIdentifier: @"Name"];
    cellRect = [self frameOfCellAtColumn: col row: row];
    rect     = NSMakeRect( cellRect.origin.x + cellRect.size.width - 19,
                           cellRect.origin.y + cellRect.size.height - 38,
                           14, 14 );

    return rect;
}

- (NSRect) revealRectForRow: (int) row
{
    int col;
    NSRect cellRect, rect;

    col      = [self columnWithIdentifier: @"Name"];
    cellRect = [self frameOfCellAtColumn: col row: row];
    rect     = NSMakeRect( cellRect.origin.x + cellRect.size.width - 19,
                           cellRect.origin.y + cellRect.size.height - 19,
                           14, 14 );

    return rect;
}


- (BOOL) pointInPauseRect: (NSPoint) point
{
    return NSPointInRect( point, [self pauseRectForRow:
                                    [self rowAtPoint: point]] );
}

- (BOOL) pointInRevealRect: (NSPoint) point
{
    return NSPointInRect( point, [self revealRectForRow:
                                    [self rowAtPoint: point]] );
}

- (void) mouseUp: (NSEvent *) e
{
    NSPoint point;
    int row, col;

    point = [self convertPoint: [e locationInWindow] fromView: NULL];
    row   = [self rowAtPoint: point];
    col   = [self columnAtPoint: point];

    if( row < 0 )
    {
        [self deselectAll: NULL];
    }
    else if( [self pointInPauseRect: point] )
    {
        [self pauseOrResume: row];
    }
    else if( [self pointInRevealRect: point] )
    {
        [fController revealInFinder: [NSString stringWithFormat:
            @"%@/%@", [NSString stringWithUTF8String: fStat[row].folder],
            [NSString stringWithUTF8String: fStat[row].info.name]]];
        [self display];
    }
    else if( row >= 0 && col == [self columnWithIdentifier: @"Progress"]
             && ( [e modifierFlags] & NSAlternateKeyMask ) )
    {
        [fController advancedChanged: NULL];
    }
    else
    {
        [self selectRowIndexes: [NSIndexSet indexSetWithIndex: row]
            byExtendingSelection: NO];
    }

    fClickPoint = NSMakePoint( 0, 0 );
}

- (NSMenu *) menuForEvent: (NSEvent *) e
{
    NSPoint point;
    int row;

    point = [self convertPoint: [e locationInWindow] fromView: NULL];
    row = [self rowAtPoint: point];

    if( row < 0 )
    {
        return NULL;
    }

    [self selectRowIndexes: [NSIndexSet indexSetWithIndex: row]
        byExtendingSelection: NO];
    return [fController menuForIndex: row];
}

- (void) drawRect: (NSRect) r
{
    int i;
    NSRect rect;
    NSPoint point;
    NSImage * image;

    [super drawRect: r];

    for( i = 0; i < [self numberOfRows]; i++ )
    {
        rect  = [self pauseRectForRow: i];
        image = NULL;
        if( fStat[i].status & TR_STATUS_PAUSE )
        {
            image = NSPointInRect( fClickPoint, rect ) ?
                [NSImage imageNamed: @"ResumeOn.png"] :
                [NSImage imageNamed: @"ResumeOff.png"];
        }
        else if( fStat[i].status &
                 ( TR_STATUS_CHECK | TR_STATUS_DOWNLOAD | TR_STATUS_SEED ) )
        {
            image = NSPointInRect( fClickPoint, rect ) ?
                [NSImage imageNamed: @"PauseOn.png"] :
                [NSImage imageNamed: @"PauseOff.png"];
        }
        if( image )
        {
            point = NSMakePoint( rect.origin.x, rect.origin.y + 14 );
            [image compositeToPoint: point operation: NSCompositeSourceOver];
        }

        rect  = [self revealRectForRow: i];
        image = NSPointInRect( fClickPoint, rect ) ?
            [NSImage imageNamed: @"RevealOn.png"] :
            [NSImage imageNamed: @"RevealOff.png"];
        point = NSMakePoint( rect.origin.x, rect.origin.y + 14 );
        [image compositeToPoint: point operation: NSCompositeSourceOver];
    }
}

@end
