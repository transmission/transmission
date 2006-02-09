/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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

#import "NameCell.h"
#import "StringAdditions.h"
#import "Utils.h"

@implementation NameCell

- (id) init
{
    [super init];

    fFileTypes = [[NSMutableArray alloc] initWithCapacity: 10];
    fIcons     = [[NSMutableArray alloc] initWithCapacity: 10];

    return self;
}

- (NSImage *) iconForFileType: (NSString *) type
{
    unsigned i;

    /* See if we have this icon cached */
    for( i = 0; i < [fFileTypes count]; i++ )
        if( [[fFileTypes objectAtIndex: i] isEqualToString: type] )
            break;

    if( i == [fFileTypes count] )
    {
        /* Unknown file type, get its icon and cache it */
        NSImage * icon;
        icon = [[NSWorkspace sharedWorkspace] iconForFileType: type];
        [icon setFlipped: YES];
        [fFileTypes addObject: type];
        [fIcons     addObject: icon];
    }

    return [fIcons objectAtIndex: i];
}

- (void) setStat: (tr_stat_t *) stat whiteText: (BOOL) w
{
    fWhiteText = w;

    fNameString  = [NSString stringWithUTF8String: stat->info.name];
    fSizeString  = [NSString stringWithFormat: @" (%@)",
                    [NSString stringForFileSize: stat->info.totalSize]];

    fCurrentIcon = [self iconForFileType: stat->info.fileCount > 1 ?
        NSFileTypeForHFSTypeCode('fldr') : [fNameString pathExtension]];

    fTimeString  = @"";
    fPeersString = @"";

    if( stat->status & TR_STATUS_PAUSE )
    {
        fTimeString = [NSString stringWithFormat:
            @"Paused (%.2f %%)", 100 * stat->progress];
    }
    else if( stat->status & TR_STATUS_CHECK )
    {
        fTimeString = [NSString stringWithFormat:
            @"Checking existing files (%.2f %%)", 100 * stat->progress];
    }
    else if( stat->status & TR_STATUS_DOWNLOAD )
    {
        if( stat->eta < 0 )
        {
            fTimeString = [NSString stringWithFormat:
                @"Finishing in --:--:-- (%.2f %%)", 100 * stat->progress];
        }
        else
        {
            fTimeString = [NSString stringWithFormat:
                @"Finishing in %02d:%02d:%02d (%.2f %%)",
                stat->eta / 3600, ( stat->eta / 60 ) % 60,
                stat->eta % 60, 100 * stat->progress];
        }
        fPeersString = [NSString stringWithFormat:
            @"Downloading from %d of %d peer%s",
            stat->peersUploading, stat->peersTotal,
            ( stat->peersTotal == 1 ) ? "" : "s"];
    }
    else if( stat->status & TR_STATUS_SEED )
    {
        fTimeString  = [NSString stringWithFormat:
            @"Seeding, uploading to %d of %d peer%s",
            stat->peersDownloading, stat->peersTotal,
            ( stat->peersTotal == 1 ) ? "" : "s"];
    }
    else if( stat->status & TR_STATUS_STOPPING )
    {
        fTimeString  = @"Stopping...";
    }

    if( ( stat->status & ( TR_STATUS_DOWNLOAD | TR_STATUS_SEED ) ) &&
        ( stat->status & TR_TRACKER_ERROR ) )
    {
        fPeersString = [NSString stringWithFormat: @"%@%@",
    	    @"Error: ", [NSString stringWithUTF8String: stat->error]];
    }
}

- (void) drawWithFrame: (NSRect) cellFrame inView: (NSView *) view
{
    NSString * string;
    NSPoint pen;
    NSMutableDictionary * attributes;

    if( ![view lockFocusIfCanDraw] )
    {
        return;
    }

    pen = cellFrame.origin;
    float cellWidth = cellFrame.size.width;

    pen.x += 5;
    pen.y += 5;                                                                 
    [fCurrentIcon drawAtPoint: pen fromRect:
        NSMakeRect(0,0,[fCurrentIcon size].width,[fCurrentIcon size].height)
        operation: NSCompositeSourceOver fraction: 1.0];

    attributes = [NSMutableDictionary dictionaryWithCapacity: 2];
    [attributes setObject: fWhiteText ? [NSColor whiteColor] :
        [NSColor blackColor] forKey: NSForegroundColorAttributeName];

    [attributes setObject: [NSFont messageFontOfSize: 12.0]
        forKey: NSFontAttributeName];

    pen.x += 37;
    string = [[fNameString stringFittingInWidth: cellWidth -
        72 - [fSizeString sizeWithAttributes: attributes].width
        withAttributes: attributes] stringByAppendingString: fSizeString];
    [string drawAtPoint: pen withAttributes: attributes];

    [attributes setObject: [NSFont messageFontOfSize: 10.0]
        forKey: NSFontAttributeName];

    pen.x += 5; pen.y += 20;
    [fTimeString drawAtPoint: pen withAttributes: attributes];

    pen.x += 0; pen.y += 15;
    string = [fPeersString stringFittingInWidth: cellFrame.size.width -
        77 withAttributes: attributes];
    [string drawAtPoint: pen withAttributes: attributes];

    [view unlockFocus];
}

@end
